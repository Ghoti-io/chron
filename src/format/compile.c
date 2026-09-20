/**
 * @file
 *
 * The pattern compiler: LDML and `strftime`, lowered to one item list.
 *
 * A pattern is compiled once and applied many times, the same shape as
 * `GRX_Regex` and for the same reasons - the pattern is checked once, its
 * errors carry an offset, and a hostile pattern is bounded at compile time.
 * In `ctang` a pattern comes from a template and a template may come from a
 * user, so that last one is not hypothetical.
 *
 * Reference: Unicode TR35 part 4, *Dates*, the Date Field Symbol Table;
 * `strftime(3)`.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/format.h>
#include <ghoti.io/chron/macros.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/cutil/safemath.h>

#include "../core/core_internal.h"
#include "format_internal.h"

/** A growable item list and literal pool, while compiling. */
typedef struct Builder {
  const GCHRON_Allocator * allocator;
  GCHRON_FormatItem * items;
  size_t item_count;
  size_t item_capacity;
  char * literals;
  size_t literal_bytes;
  size_t literal_capacity;
  size_t max_items;
} Builder;

/** Append an item, growing the list. */
static GCHRON_Result push_item(Builder * b, GCHRON_ItemKind kind, int count) {
  if (b->max_items != 0 && b->item_count >= b->max_items) {
    return GCHRON_ERR_LIMIT;
  }
  if (b->item_count == b->item_capacity) {
    size_t capacity = b->item_capacity == 0 ? 16 : b->item_capacity * 2;
    size_t bytes;
    GCHRON_FormatItem * grown;
    if (!gcu_safe_mul_size(capacity, sizeof(GCHRON_FormatItem), &bytes)) {
      return GCHRON_ERR_OOM;
    }
    grown = (GCHRON_FormatItem *)gcu_allocator_realloc(b->allocator, b->items,
        bytes);
    if (grown == NULL) {
      return GCHRON_ERR_OOM;
    }
    b->items = grown;
    b->item_capacity = capacity;
  }
  b->items[b->item_count].kind = kind;
  b->items[b->item_count].count = (uint16_t)count;
  b->items[b->item_count].literal_at = 0;
  b->items[b->item_count].literal_len = 0;
  b->item_count += 1;
  return GCHRON_OK;
}

/** Append literal bytes, merging with the previous item when it is one. */
static GCHRON_Result push_literal(Builder * b, const char * text,
    size_t len) {
  size_t needed;

  if (len == 0) {
    return GCHRON_OK;
  }
  if (!gcu_safe_add_size(b->literal_bytes, len, &needed)) {
    return GCHRON_ERR_OOM;
  }
  if (needed > b->literal_capacity) {
    size_t capacity = b->literal_capacity == 0 ? 32 : b->literal_capacity;
    char * grown;
    while (capacity < needed) {
      if (capacity > (size_t)-1 / 2) {
        return GCHRON_ERR_OOM;
      }
      capacity *= 2;
    }
    grown = (char *)gcu_allocator_realloc(b->allocator, b->literals, capacity);
    if (grown == NULL) {
      return GCHRON_ERR_OOM;
    }
    b->literals = grown;
    b->literal_capacity = capacity;
  }

  /* Merging runs rather than emitting one item per character: a pattern of
   * mostly text would otherwise compile to one item per byte, which costs a
   * bounded but silly amount of memory for something a hostile caller
   * chooses. */
  if (b->item_count > 0
      && b->items[b->item_count - 1].kind == GCHRON_ITEM_LITERAL
      && (size_t)(b->items[b->item_count - 1].literal_at
              + b->items[b->item_count - 1].literal_len)
          == b->literal_bytes) {
    memcpy(b->literals + b->literal_bytes, text, len);
    b->literal_bytes += len;
    b->items[b->item_count - 1].literal_len += (uint32_t)len;
    return GCHRON_OK;
  }

  {
    GCHRON_Result result = push_item(b, GCHRON_ITEM_LITERAL, 1);
    if (result != GCHRON_OK) {
      return result;
    }
  }
  memcpy(b->literals + b->literal_bytes, text, len);
  b->items[b->item_count - 1].literal_at = (uint32_t)b->literal_bytes;
  b->items[b->item_count - 1].literal_len = (uint32_t)len;
  b->literal_bytes += len;
  return GCHRON_OK;
}

/** Which item an LDML pattern letter produces, or -1 for an unknown one. */
static int ldml_kind(char letter) {
  switch (letter) {
    case 'G': return GCHRON_ITEM_ERA;
    case 'y': return GCHRON_ITEM_YEAR;
    case 'Y': return GCHRON_ITEM_WEEK_YEAR;
    case 'u': return GCHRON_ITEM_EXTENDED_YEAR;
    case 'Q': case 'q': return GCHRON_ITEM_QUARTER;
    case 'M': case 'L': return GCHRON_ITEM_MONTH;
    case 'w': return GCHRON_ITEM_WEEK_OF_YEAR;
    case 'W': return GCHRON_ITEM_WEEK_OF_MONTH;
    case 'd': return GCHRON_ITEM_DAY;
    case 'D': return GCHRON_ITEM_DAY_OF_YEAR;
    case 'F': return GCHRON_ITEM_WEEKDAY_IN_MONTH;
    case 'g': return GCHRON_ITEM_MODIFIED_JULIAN;
    case 'E': return GCHRON_ITEM_WEEKDAY;
    case 'e': case 'c': return GCHRON_ITEM_WEEKDAY_LOCAL;
    case 'a': return GCHRON_ITEM_DAY_PERIOD;
    case 'h': return GCHRON_ITEM_HOUR_1_12;
    case 'H': return GCHRON_ITEM_HOUR_0_23;
    case 'K': return GCHRON_ITEM_HOUR_0_11;
    case 'k': return GCHRON_ITEM_HOUR_1_24;
    case 'm': return GCHRON_ITEM_MINUTE;
    case 's': return GCHRON_ITEM_SECOND;
    case 'S': return GCHRON_ITEM_FRACTION;
    case 'A': return GCHRON_ITEM_MILLIS_OF_DAY;
    case 'z': return GCHRON_ITEM_ZONE_ABBREV;
    case 'V': case 'v': return GCHRON_ITEM_ZONE_ID;
    case 'X': return GCHRON_ITEM_OFFSET_ISO_Z;
    case 'x': return GCHRON_ITEM_OFFSET_ISO;
    case 'Z': return GCHRON_ITEM_OFFSET_RFC822;
    case 'O': return GCHRON_ITEM_OFFSET_LOCALISED;
    default: return -1;
  }
}

/** Whether a byte is a letter TR35 reserves as a pattern character. */
static bool is_pattern_letter(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/** Compile an LDML pattern. */
static GCHRON_Result compile_ldml(Builder * b, const char * pattern,
    size_t len, GCHRON_Error * err) {
  size_t i = 0;

  while (i < len) {
    char c = pattern[i];

    if (c == '\'') {
      /*
       * TR35's quoting: text between single quotes is literal, and two
       * single quotes are one literal quote. `''` outside a quoted run is
       * also one quote, which is the case a naive reader drops.
       */
      size_t start;
      if (i + 1 < len && pattern[i + 1] == '\'') {
        GCHRON_Result result = push_literal(b, "'", 1);
        if (result != GCHRON_OK) {
          return result;
        }
        i += 2;
        continue;
      }
      i += 1;
      start = i;
      while (i < len) {
        if (pattern[i] == '\'') {
          if (i + 1 < len && pattern[i + 1] == '\'') {
            GCHRON_Result result = push_literal(b, pattern + start,
                i - start + 1);
            if (result != GCHRON_OK) {
              return result;
            }
            i += 2;
            start = i;
            continue;
          }
          break;
        }
        i += 1;
      }
      if (i >= len) {
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_UNTERMINATED_QUOTE, start - 1, 1);
      }
      {
        GCHRON_Result result = push_literal(b, pattern + start, i - start);
        if (result != GCHRON_OK) {
          return result;
        }
      }
      i += 1;
      continue;
    }

    if (is_pattern_letter(c)) {
      size_t start = i;
      int kind;
      int count = 0;
      while (i < len && pattern[i] == c) {
        count += 1;
        i += 1;
      }
      if (count > 16) {
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_PATTERN_LETTER_RUN, start, i - start);
      }
      kind = ldml_kind(c);
      if (kind < 0) {
        /*
         * TR35 reserves every ASCII letter as a pattern character, so an
         * unrecognised one is a pattern this library does not implement
         * rather than a literal. Treating it as a literal is how `b` in a
         * pattern silently becomes the letter b instead of an error.
         */
        return gchron_fail(err, GCHRON_ERR_UNSUPPORTED,
            GCHRON_DIAG_PATTERN_LETTER_UNKNOWN, start, i - start);
      }
      {
        GCHRON_Result result = push_item(b, (GCHRON_ItemKind)kind, count);
        if (result != GCHRON_OK) {
          return result;
        }
      }
      continue;
    }

    {
      size_t start = i;
      while (i < len && pattern[i] != '\'' && !is_pattern_letter(pattern[i])) {
        i += 1;
      }
      {
        GCHRON_Result result = push_literal(b, pattern + start, i - start);
        if (result != GCHRON_OK) {
          return result;
        }
      }
    }
  }
  return GCHRON_OK;
}

/**
 * Lower one `strftime` specifier into items.
 *
 * The compound ones expand to the C locale's own definitions, because that is
 * what `%c`, `%x`, `%X` and `%r` mean when nothing has called `setlocale` -
 * and this library never does (design.md section 8.5).
 */
static GCHRON_Result lower_strftime(Builder * b, char specifier, size_t at,
    GCHRON_Error * err) {
  switch (specifier) {
    case '%': return push_literal(b, "%", 1);
    case 'n': return push_literal(b, "\n", 1);
    case 't': return push_literal(b, "\t", 1);

    case 'a': return push_item(b, GCHRON_ITEM_WEEKDAY, 3);
    case 'A': return push_item(b, GCHRON_ITEM_WEEKDAY, 4);
    case 'b': case 'h': return push_item(b, GCHRON_ITEM_MONTH, 3);
    case 'B': return push_item(b, GCHRON_ITEM_MONTH, 4);
    case 'C': return push_item(b, GCHRON_ITEM_CENTURY, 2);
    case 'd': return push_item(b, GCHRON_ITEM_DAY, 2);
    case 'e': return push_item(b, GCHRON_ITEM_DAY_SPACE_PADDED, 2);
    case 'H': return push_item(b, GCHRON_ITEM_HOUR_0_23, 2);
    case 'I': return push_item(b, GCHRON_ITEM_HOUR_1_12, 2);
    case 'j': return push_item(b, GCHRON_ITEM_DAY_OF_YEAR, 3);
    case 'k': return push_item(b, GCHRON_ITEM_HOUR_0_23_SPACE_PADDED, 2);
    case 'l': return push_item(b, GCHRON_ITEM_HOUR_1_12_SPACE_PADDED, 2);
    case 'm': return push_item(b, GCHRON_ITEM_MONTH, 2);
    case 'M': return push_item(b, GCHRON_ITEM_MINUTE, 2);
    case 'p': return push_item(b, GCHRON_ITEM_DAY_PERIOD, 1);
    case 'S': return push_item(b, GCHRON_ITEM_SECOND, 2);
    case 's': return push_item(b, GCHRON_ITEM_EPOCH_SECONDS, 1);
    case 'u': return push_item(b, GCHRON_ITEM_WEEKDAY_ISO_NUMBER, 1);
    case 'w': return push_item(b, GCHRON_ITEM_WEEKDAY_SUNDAY_ZERO, 1);
    case 'g': return push_item(b, GCHRON_ITEM_WEEK_YEAR, 2);
    case 'G': return push_item(b, GCHRON_ITEM_WEEK_YEAR, 4);
    case 'V': return push_item(b, GCHRON_ITEM_WEEK_OF_YEAR, 2);
    case 'y': return push_item(b, GCHRON_ITEM_YEAR, 2);
    case 'Y': return push_item(b, GCHRON_ITEM_EXTENDED_YEAR, 4);
    case 'z': return push_item(b, GCHRON_ITEM_OFFSET_RFC822, 1);
    case 'Z': return push_item(b, GCHRON_ITEM_ZONE_ABBREV, 3);

    /* The compound specifiers, as the C locale defines them. */
    case 'D': {
      GCHRON_Result r = push_item(b, GCHRON_ITEM_MONTH, 2);
      if (r == GCHRON_OK) r = push_literal(b, "/", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_DAY, 2);
      if (r == GCHRON_OK) r = push_literal(b, "/", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_YEAR, 2);
      return r;
    }
    case 'F': {
      GCHRON_Result r = push_item(b, GCHRON_ITEM_EXTENDED_YEAR, 4);
      if (r == GCHRON_OK) r = push_literal(b, "-", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_MONTH, 2);
      if (r == GCHRON_OK) r = push_literal(b, "-", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_DAY, 2);
      return r;
    }
    case 'R': {
      GCHRON_Result r = push_item(b, GCHRON_ITEM_HOUR_0_23, 2);
      if (r == GCHRON_OK) r = push_literal(b, ":", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_MINUTE, 2);
      return r;
    }
    case 'T': case 'X': {
      GCHRON_Result r = push_item(b, GCHRON_ITEM_HOUR_0_23, 2);
      if (r == GCHRON_OK) r = push_literal(b, ":", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_MINUTE, 2);
      if (r == GCHRON_OK) r = push_literal(b, ":", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_SECOND, 2);
      return r;
    }
    case 'r': {
      GCHRON_Result r = push_item(b, GCHRON_ITEM_HOUR_1_12, 2);
      if (r == GCHRON_OK) r = push_literal(b, ":", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_MINUTE, 2);
      if (r == GCHRON_OK) r = push_literal(b, ":", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_SECOND, 2);
      if (r == GCHRON_OK) r = push_literal(b, " ", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_DAY_PERIOD, 1);
      return r;
    }
    case 'x': {
      GCHRON_Result r = push_item(b, GCHRON_ITEM_MONTH, 2);
      if (r == GCHRON_OK) r = push_literal(b, "/", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_DAY, 2);
      if (r == GCHRON_OK) r = push_literal(b, "/", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_YEAR, 2);
      return r;
    }
    case 'c': {
      /* The C locale's `%a %b %e %H:%M:%S %Y`. */
      GCHRON_Result r = push_item(b, GCHRON_ITEM_WEEKDAY, 3);
      if (r == GCHRON_OK) r = push_literal(b, " ", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_MONTH, 3);
      if (r == GCHRON_OK) r = push_literal(b, " ", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_DAY_SPACE_PADDED, 2);
      if (r == GCHRON_OK) r = push_literal(b, " ", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_HOUR_0_23, 2);
      if (r == GCHRON_OK) r = push_literal(b, ":", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_MINUTE, 2);
      if (r == GCHRON_OK) r = push_literal(b, ":", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_SECOND, 2);
      if (r == GCHRON_OK) r = push_literal(b, " ", 1);
      if (r == GCHRON_OK) r = push_item(b, GCHRON_ITEM_EXTENDED_YEAR, 4);
      return r;
    }

    case 'U': case 'W':
      /*
       * Week numbers counted from the first Sunday or Monday of the year,
       * with days before it in "week 0". Neither is the ISO week, neither has
       * an LDML letter, and a caller who wants a week number almost always
       * wants `%V`. Refused rather than approximated.
       */
      return gchron_fail(err, GCHRON_ERR_UNSUPPORTED,
          GCHRON_DIAG_PATTERN_LETTER_UNKNOWN, at, 2);

    default:
      return gchron_fail(err, GCHRON_ERR_UNSUPPORTED,
          GCHRON_DIAG_PATTERN_LETTER_UNKNOWN, at, 2);
  }
}

/** Compile a `strftime` pattern. */
static GCHRON_Result compile_strftime(Builder * b, const char * pattern,
    size_t len, GCHRON_Error * err) {
  size_t i = 0;

  while (i < len) {
    if (pattern[i] != '%') {
      size_t start = i;
      while (i < len && pattern[i] != '%') {
        i += 1;
      }
      {
        GCHRON_Result result = push_literal(b, pattern + start, i - start);
        if (result != GCHRON_OK) {
          return result;
        }
      }
      continue;
    }
    if (i + 1 >= len) {
      return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_UNEXPECTED_END,
          i, 1);
    }
    {
      /* `%E` and `%O` are locale-alternative modifiers; in the C locale they
       * mean the unmodified specifier, so they are skipped rather than
       * refused. */
      size_t at = i;
      char specifier = pattern[i + 1];
      size_t consumed = 2;
      if ((specifier == 'E' || specifier == 'O') && i + 2 < len) {
        specifier = pattern[i + 2];
        consumed = 3;
      }
      {
        GCHRON_Result result = lower_strftime(b, specifier, at, err);
        if (result != GCHRON_OK) {
          return result;
        }
      }
      i += consumed;
    }
  }
  return GCHRON_OK;
}

/** An upper bound on what one item can produce. */
static size_t item_bound(const GCHRON_FormatItem * item) {
  switch (item->kind) {
    case GCHRON_ITEM_LITERAL: return item->literal_len;
    case GCHRON_ITEM_ERA: return 32;
    case GCHRON_ITEM_MONTH: case GCHRON_ITEM_WEEKDAY:
    case GCHRON_ITEM_WEEKDAY_LOCAL: return 32;
    case GCHRON_ITEM_ZONE_ABBREV: return 40;
    case GCHRON_ITEM_ZONE_ID: return 64;
    case GCHRON_ITEM_YEAR: case GCHRON_ITEM_WEEK_YEAR:
    case GCHRON_ITEM_EXTENDED_YEAR:
      /*
       * The widest year is nine digits, and the count can ask for more
       * padding than that - but a negative year writes its sign *outside*
       * the padding, so the sign is one more byte on top of either. The
       * first spelling of this line took the larger of the count and
       * "9 digits + a sign", which is right until the count exceeds the
       * digits and the sign stops being covered: `uuuuuuuuuuu` on a
       * negative year needed twelve bytes and was promised eleven.
       * `fuzz_format` found it by asserting the bound really bounds.
       */
      return (item->count > 9 ? (size_t)item->count : 9) + 1;
    case GCHRON_ITEM_EPOCH_SECONDS: case GCHRON_ITEM_MODIFIED_JULIAN:
      return 24;
    case GCHRON_ITEM_OFFSET_LOCALISED: return 16;
    case GCHRON_ITEM_FRACTION:
      return item->count > 9 ? (size_t)item->count : 9;
    default:
      return item->count > 12 ? (size_t)item->count : 12;
  }
}

GCHRON_Result gchron_format_compile_into(const char * pattern, size_t len,
    GCHRON_FormatSyntax syntax, const GCHRON_Limits * limits,
    const GCHRON_Allocator * allocator, GCHRON_Format * out,
    GCHRON_Error * err) {
  Builder b;
  GCHRON_Limits defaults;
  GCHRON_Result result;
  size_t i;
  size_t bound = 1; /* the terminating NUL */

  if (pattern == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  if (limits == NULL) {
    gchron_limits_default(&defaults);
    limits = &defaults;
  }
  if (limits->max_format_length != 0 && len > limits->max_format_length) {
    return gchron_fail(err, GCHRON_ERR_LIMIT, GCHRON_DIAG_INPUT_TOO_LONG,
        limits->max_format_length, 0);
  }

  memset(&b, 0, sizeof(b));
  b.allocator = allocator;
  b.max_items = limits->max_format_items;

  result = (syntax == GCHRON_FORMAT_STRFTIME)
      ? compile_strftime(&b, pattern, len, err)
      : compile_ldml(&b, pattern, len, err);
  if (result != GCHRON_OK) {
    gcu_allocator_free(allocator, b.items);
    gcu_allocator_free(allocator, b.literals);
    return result;
  }

  for (i = 0; i < b.item_count; ++i) {
    size_t part = item_bound(&b.items[i]);
    if (!gcu_safe_add_size(bound, part, &bound)) {
      gcu_allocator_free(allocator, b.items);
      gcu_allocator_free(allocator, b.literals);
      return GCHRON_ERR_LIMIT;
    }
  }

  out->allocator = allocator;
  out->items = b.items;
  out->item_count = b.item_count;
  out->literals = b.literals;
  out->literal_bytes = b.literal_bytes;
  out->max_length = bound;
  out->is_static = false;
  return GCHRON_OK;
}

GCHRON_Result gchron_format_compile(const char * pattern, size_t len,
    GCHRON_FormatSyntax syntax, const GCHRON_Limits * limits,
    const GCHRON_Allocator * allocator, GCHRON_Format ** out,
    GCHRON_Error * err) {
  GCHRON_Format * format;
  GCHRON_Result result;

  gchron_error_clear(err);
  if (out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  if (allocator == NULL) {
    allocator = gchron_allocator_default();
  }
  format = (GCHRON_Format *)gcu_allocator_calloc(allocator, 1,
      sizeof(GCHRON_Format));
  if (format == NULL) {
    return GCHRON_ERR_OOM;
  }
  result = gchron_format_compile_into(pattern, len, syntax, limits, allocator,
      format, err);
  if (result != GCHRON_OK) {
    gcu_allocator_free(allocator, format);
    return result;
  }
  *out = format;
  return GCHRON_OK;
}

void gchron_format_destroy(GCHRON_Format * format) {
  if (format == NULL || format->is_static) {
    /* A named format is static, and a caller holding "whichever format was
     * chosen" should not have to remember which kind it is. */
    return;
  }
  gcu_allocator_free(format->allocator, format->items);
  gcu_allocator_free(format->allocator, format->literals);
  gcu_allocator_free(format->allocator, format);
}

GCHRON_Result gchron_format_max_length(const GCHRON_Format * format,
    size_t * out) {
  if (format == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  *out = format->max_length;
  return GCHRON_OK;
}
