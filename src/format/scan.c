/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Chron.
 *
 * Ghoti.io Chron is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Chron is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Reading text back through a compiled pattern: design.md section 8.7.
 *
 * The inverse of emit.c, walking the same GCHRON_FormatItem list and
 * consuming where the emitter would have produced. It exists because
 * `strptime` is POSIX and is absent from the Windows C runtime altogether, so
 * every cross-platform program that reads a timestamp out of a log line
 * otherwise writes the scanner again.
 *
 * What comes out is a field set and not a value. A pattern need not name a
 * whole date-time, and a function that returned one would have to invent the
 * rest - which is precisely how `strptime` earns its reputation, since
 * glibc's leaves untouched whatever the caller failed to initialise.
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/format.h>
#include <ghoti.io/chron/macros.h>
#include <string.h>

#include "../core/core_internal.h"
#include "format_internal.h"

/*--------------------------------------------------------------------------*
 * A cursor over the input
 *--------------------------------------------------------------------------*/

typedef struct Cursor {
  const char * text;
  size_t len;
  size_t pos;
} Cursor;

static bool at_end(const Cursor * c) {
  return c->pos >= c->len;
}

static char peek(const Cursor * c) {
  return at_end(c) ? '\0' : c->text[c->pos];
}

static bool is_digit(char ch) {
  return ch >= '0' && ch <= '9';
}

/**
 * Read a run of digits.
 *
 * TR35's **adjacent numeric value parsing**, which is the rule that makes the
 * letter count mean two different things and has to, because the emitter's
 * count is a *minimum* width rather than an exact one.
 *
 * - A numeric field immediately followed by another numeric field reads
 *   **exactly** @p count digits. Nothing else can find the boundary:
 *   `uuuuMMdd` writes `20260921`, and only the declared widths say where the
 *   year stops.
 * - Any other numeric field reads **at least** @p count and then as many more
 *   as are there, up to @p natural. This is the half an earlier version of
 *   this file got wrong. `uuu` is a perfectly ordinary way to write a year,
 *   the emitter pads it to a minimum of three and so writes `2026`, and a
 *   reader demanding exactly three took `202` and then failed on the `6`.
 *   `fuzz_scan` found it through `uuAg`, and `uuu-MM-dd` - a pattern somebody
 *   might really type - was broken the same way.
 *
 * @param c The cursor.
 * @param count Pattern letters, the minimum width.
 * @param natural The most digits this field can naturally carry.
 * @param exact Whether another number follows with nothing in between.
 * @param out Receives the value.
 * @return `false` when the digits are not there.
 */
static bool take_digits_adj(Cursor * c, int count, int natural, bool exact,
    int64_t * out) {
  size_t start = c->pos;
  int64_t value = 0;
  int taken = 0;
  int ceiling = exact ? count : (natural > count ? natural : count);

  while (taken < ceiling && !at_end(c) && is_digit(c->text[c->pos])) {
    /*
     * Checked, because `ceiling` reaches nineteen for `%s` and a pattern may
     * ask for more letters than that. Nineteen digits is the edge of an
     * int64, and `fuzz_scan` walked straight off it - UBSan reported
     * `999999999999999999 * 10` as signed overflow, which is undefined
     * behaviour reached from untrusted text.
     */
    if (!gchron_mul_i64(value, 10, &value)
        || !gchron_add_i64(value, c->text[c->pos] - '0', &value)) {
      c->pos = start;
      return false;
    }
    c->pos += 1;
    taken += 1;
  }
  if (taken == 0 || taken < count) {
    c->pos = start;
    return false;
  }
  *out = value;
  return true;
}

/**
 * Read a run of digits, and refuse a value the field cannot hold.
 *
 * The range belongs here and not only in the resolvers. `fuzz_scan` found the
 * reason: the pattern `uuuu-MM-d.'T'hH:mm` writes `2026-09-21.T315:30`, and
 * two adjacent variable-width numbers have no boundary between them - so the
 * `h` read `31`, which is not an hour on any clock, and the `H` after it read
 * the leftover `5`. The second hour overwrote the first and the impossible
 * value simply vanished, leaving a wrong time that looked perfectly ordinary.
 *
 * Refusing it where it is read means such a pattern fails rather than
 * answering. That is the right outcome for text that genuinely is ambiguous:
 * `315` cannot be split into an hour and an hour by anything but a guess.
 */
static bool take_ranged(Cursor * c, int count, int natural, bool exact,
    int64_t low, int64_t high, int64_t * out) {
  size_t start = c->pos;

  if (!take_digits_adj(c, count, natural, exact, out)) {
    return false;
  }
  if (*out < low || *out > high) {
    c->pos = start;
    return false;
  }
  return true;
}

/** Read an optional leading sign, then digits. */
static bool take_signed(Cursor * c, int count, int natural, bool exact,
    int64_t * out) {
  size_t start = c->pos;
  bool negative = false;

  if (!at_end(c) && (peek(c) == '-' || peek(c) == '+')) {
    negative = (peek(c) == '-');
    c->pos += 1;
  }
  if (!take_digits_adj(c, count, natural, exact, out)) {
    c->pos = start;
    return false;
  }
  if (negative) {
    *out = -*out;
  }
  return true;
}

/** Read exactly this text, or consume nothing. */
static bool take_exact(Cursor * c, const char * want, size_t want_len) {
  if (c->len - c->pos < want_len) {
    return false;
  }
  if (memcmp(c->text + c->pos, want, want_len) != 0) {
    return false;
  }
  c->pos += want_len;
  return true;
}

/*--------------------------------------------------------------------------*
 * Names
 *--------------------------------------------------------------------------*/

/** What kind of name a lookup is asking the provider for. */
typedef enum {
  NAME_MONTH,
  NAME_WEEKDAY,
  NAME_ERA,
  NAME_DAY_PERIOD
} NameKind;

static const char * name_at(const GCHRON_Names * names, NameKind kind,
    int index, GCHRON_NameWidth width) {
  switch (kind) {
    case NAME_MONTH:      return names->month(names, index, width);
    case NAME_WEEKDAY:    return names->weekday(names, index, width);
    case NAME_ERA:        return names->era(names, index, width);
    case NAME_DAY_PERIOD: return names->day_period(names, index, width);
  }
  return NULL;
}

/**
 * Match the longest name the provider gives for this field.
 *
 * Longest, not first: a provider whose abbreviated and wide names share a
 * prefix - which is most of them, `Jun` and `June` - would otherwise stop at
 * the short one and leave `e` in the input for the next item to choke on.
 *
 * Every width is tried and not only the one the letter count asks for,
 * because a provider that has no name at a width is documented to fall back
 * to the abbreviated one, so the width a caller asked for is not reliably the
 * width the text carries.
 *
 * @param out_index Receives the index that matched.
 * @return `false` when nothing matched; the cursor does not move.
 */
static bool take_name(Cursor * c, const GCHRON_Names * names, NameKind kind,
    int low, int high, int count, int * out_index) {
  size_t best_len = 0;
  int best_index = -1;
  int index;
  int w;

  (void)count;
  for (index = low; index <= high; ++index) {
    for (w = 0; w <= (int)GCHRON_NAME_SHORT; ++w) {
      const char * name = name_at(names, kind, index, (GCHRON_NameWidth)w);
      size_t name_len;
      if (name == NULL) {
        continue;
      }
      name_len = strlen(name);
      if (name_len <= best_len || name_len > c->len - c->pos) {
        continue;
      }
      if (memcmp(c->text + c->pos, name, name_len) == 0) {
        best_len = name_len;
        best_index = index;
      }
    }
  }
  if (best_index < 0) {
    return false;
  }
  c->pos += best_len;
  *out_index = best_index;
  return true;
}

/*--------------------------------------------------------------------------*
 * Offsets
 *--------------------------------------------------------------------------*/

/** Two digits, exactly. */
static bool take_two(Cursor * c, int * out) {
  int64_t value;
  if (!take_digits_adj(c, 2, 2, true, &value)) {
    return false;
  }
  *out = (int)value;
  return true;
}

/**
 * Read an offset, in any of the shapes emit.c writes.
 *
 * Deliberately shape-directed rather than count-directed. `XX` writes
 * `+0530` and `XXX` writes `+05:30`, but `Z`..`ZZZ` append seconds only when
 * they are not zero and `X` omits the minutes when *they* are zero - so the
 * text a given count produces varies with the value, and a reader that
 * insisted on one shape per count could not read back what the emitter
 * wrote. Accepting every shape and requiring the colons to be consistent
 * keeps the round trip exact without accepting `+05:3000`.
 *
 * @param style 0 for `X`, 1 for `x`, 2 for `Z`, 3 for `O`.
 */
static bool take_offset(Cursor * c, int style, int32_t * out_sec,
    bool * out_unknown) {
  size_t start = c->pos;
  bool negative;
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  bool colons;

  *out_unknown = false;

  if (style == 3) {
    /*
     * `O`: localised GMT. `GMT` alone is a *known* zero offset, which is what
     * CLDR's `gmtZeroFormat` means and what every ICU up to 76.1 wrote for one
     * - so reading it as anything else would misread text this library does not
     * produce but does have to accept. An unknown offset arrives as `GMT-0` or
     * `GMT-00:00` and is recognised below by the rule every other letter uses:
     * a negative sign with a zero magnitude.
     */
    if (!take_exact(c, "GMT", 3)) {
      return false;
    }
    if (at_end(c) || (peek(c) != '+' && peek(c) != '-')) {
      *out_sec = 0;
      return true;
    }
  }
  else if (style != 1 && peek(c) == 'Z') {
    /* `X` and `ZZZZZ` write `Z` for a zero offset; `x` never does. */
    c->pos += 1;
    *out_sec = 0;
    return true;
  }

  if (at_end(c) || (peek(c) != '+' && peek(c) != '-')) {
    c->pos = start;
    return false;
  }
  negative = (peek(c) == '-');
  c->pos += 1;

  if (style == 3) {
    /* `GMT+8` and `GMT+08:00` are both written; the hour may be one digit. */
    int64_t value;
    if (!take_digits_adj(c, 1, 2, false, &value)) {
      c->pos = start;
      return false;
    }
    hours = (int)value;
  }
  else if (!take_two(c, &hours)) {
    c->pos = start;
    return false;
  }

  colons = (peek(c) == ':');
  if (colons) {
    c->pos += 1;
    if (!take_two(c, &minutes)) {
      c->pos = start;
      return false;
    }
  }
  else if (is_digit(peek(c))) {
    if (!take_two(c, &minutes)) {
      c->pos = start;
      return false;
    }
  }

  /*
   * Seconds, which the tzdb really does need: Europe/Amsterdam kept
   * +00:19:32 until 1937, and an offset reader that stops at minutes is
   * wrong by a third of an hour there.
   */
  if (colons && peek(c) == ':') {
    c->pos += 1;
    if (!take_two(c, &seconds)) {
      c->pos = start;
      return false;
    }
  }
  else if (!colons && is_digit(peek(c))) {
    if (!take_two(c, &seconds)) {
      c->pos = start;
      return false;
    }
  }

  if (hours > 23 || minutes > 59 || seconds > 59) {
    c->pos = start;
    return false;
  }
  *out_sec = (int32_t)(hours * 3600 + minutes * 60 + seconds);
  if (negative) {
    /* RFC 3339 section 4.3: `-00:00` is *unknown offset*, which is a fact
     * about the text that the number alone cannot carry. */
    if (*out_sec == 0) {
      *out_unknown = true;
    }
    *out_sec = -*out_sec;
  }
  return true;
}

/**
 * Whether an item starts by consuming digits.
 *
 * The question adjacency turns on. A name, a literal, an offset and a zone
 * identifier all give the reader a boundary of their own; two numbers in a
 * row do not.
 */
static bool item_is_numeric(const GCHRON_FormatItem * item) {
  switch (item->kind) {
    case GCHRON_ITEM_YEAR:
    case GCHRON_ITEM_WEEK_YEAR:
    case GCHRON_ITEM_EXTENDED_YEAR:
    case GCHRON_ITEM_CENTURY:
    case GCHRON_ITEM_WEEK_OF_YEAR:
    case GCHRON_ITEM_WEEK_OF_MONTH:
    case GCHRON_ITEM_DAY:
    case GCHRON_ITEM_DAY_OF_YEAR:
    case GCHRON_ITEM_WEEKDAY_IN_MONTH:
    case GCHRON_ITEM_MODIFIED_JULIAN:
    case GCHRON_ITEM_WEEKDAY_ISO_NUMBER:
    case GCHRON_ITEM_WEEKDAY_SUNDAY_ZERO:
    case GCHRON_ITEM_HOUR_0_23:
    case GCHRON_ITEM_HOUR_1_24:
    case GCHRON_ITEM_HOUR_1_12:
    case GCHRON_ITEM_HOUR_0_11:
    case GCHRON_ITEM_MINUTE:
    case GCHRON_ITEM_SECOND:
    case GCHRON_ITEM_FRACTION:
    case GCHRON_ITEM_MILLIS_OF_DAY:
    case GCHRON_ITEM_EPOCH_SECONDS:
      return true;
    case GCHRON_ITEM_MONTH:
    case GCHRON_ITEM_WEEKDAY_LOCAL:
      /* Three letters or more is a name, which reads as one. */
      return item->count < 3;
    case GCHRON_ITEM_QUARTER:
      /* Three or more writes `Q1`, whose `Q` is a boundary. */
      return item->count < 3;
    default:
      /* A literal, a name, an offset, a zone identifier, and the
       * space-padded forms, which find their own edge. */
      return false;
  }
}

/*--------------------------------------------------------------------------*
 * Which letters invert
 *--------------------------------------------------------------------------*/

/**
 * Whether one item can be read back.
 *
 * `z` and `v` name a zone loosely - `EST` is US Eastern, Australian Eastern and
 * a handful of others, and `v` is looser still - so choosing among the
 * candidates needs CLDR's data and a preference order, which section 14
 * declines to ship. Those do not invert and cannot.
 *
 * **`O`, `OOOO` and `ZZZZ` do.** This function used to refuse them on the
 * grounds that TR35 calls the localised GMT format a localised *name*, and that
 * "a locale that localises the word would not be readable at all, so refusing
 * it uniformly beats a reader that works only in the root locale". That reason
 * described a library this is not. `GCHRON_Names` has hooks for months,
 * weekdays, eras, day periods and the week rules, and none for `gmtFormat`,
 * `gmtZeroFormat` or `hourFormat` - so emit.c writes the literal `GMT` and
 * ASCII digits whatever provider it is handed. The *writer* works only in the
 * root locale, which makes a reader that does the same symmetric rather than a
 * compromise, and there is no locale that localises the word because nothing
 * can supply one.
 *
 * What the old refusal cost was the defect it was hiding: this library emitted
 * `GMT+0` and could not read it. If `GCHRON_Names` ever grows a localised GMT
 * hook, this is the decision that has to be revisited, and the reader in
 * take_offset() is where the root-locale assumption lives.
 *
 * So nothing here turns on the letter count, and the parameter it briefly took
 * is gone again. It mattered for one commit, while `ZZZZ` was refused by the
 * scan and accepted by this function; making the scan read the localised GMT
 * format settled it from the other end, which is the better end. If a future
 * letter needs the count, Format.EveryPatternThatSaysItInvertsIsOneTheScanner-
 * WillTry is the test that will say so.
 */
static bool item_inverts(GCHRON_ItemKind kind) {
  switch (kind) {
    case GCHRON_ITEM_ZONE_ABBREV:
      return false;
    default:
      return true;
  }
}

GCHRON_Result gchron_format_is_invertible(const GCHRON_Format * format,
    GCHRON_Error * err) {
  size_t i;

  if (format == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  for (i = 0; i < format->item_count; ++i) {
    if (!item_inverts(format->items[i].kind)) {
      /*
       * The offset is into the *pattern*, which is what a caller debugging a
       * template needs and what this function documents. It used to be the
       * item index, which is a different number in a field the suite defines
       * as a byte offset - `fuzz_scan` found it by noticing that an offset
       * had run past the end of the input it was supposed to point into.
       */
      return gchron_fail(err, GCHRON_ERR_UNSUPPORTED,
          GCHRON_DIAG_PATTERN_NOT_INVERTIBLE,
          format->items[i].pattern_at, format->items[i].count);
    }
  }
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * The scan
 *--------------------------------------------------------------------------*/

/** A two-digit year, placed in the century the clock says we are near. */
static GCHRON_Result expand_two_digit_year(int two_digits,
    const GCHRON_Clock * clock, int64_t * out) {
  GCHRON_Instant now;
  GCHRON_DateTime civil;
  int64_t century;
  int64_t candidate;
  GCHRON_Result result;

  result = gchron_clock_now(clock, &now);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_instant_to_utc(&now, &civil);
  if (result != GCHRON_OK) {
    return result;
  }
  /* The same sliding window gchron_parse_http_date() uses for RFC 850, and
   * for the same reason: the nearest year with those last two digits. */
  century = gchron_floor_div(civil.date.year, 100) * 100;
  candidate = century + two_digits;
  if (candidate - civil.date.year > 50) {
    candidate -= 100;
  }
  else if (civil.date.year - candidate > 50) {
    candidate += 100;
  }
  *out = candidate;
  return GCHRON_OK;
}

/** `false` when a byte cannot appear in a zone identifier. */
static bool zone_id_byte(char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
      || (ch >= '0' && ch <= '9') || ch == '/' || ch == '_' || ch == '-'
      || ch == '+' || ch == '.';
}

/**
 * Record a field, refusing text that has already said something else.
 *
 * Section 8.7 says redundant fields are checked rather than ignored, and this
 * is that rule applied to the case the resolvers cannot see: a pattern naming
 * the *same* field twice. `DHDu` carries two day-of-year fields, and the
 * second silently overwrote the first - so a text whose two copies disagreed
 * parsed happily and produced whichever one came last. `fuzz_scan` found it
 * by noticing that re-formatting the result no longer reproduced the text.
 *
 * The hour is the one field compared within its cycle rather than across it:
 * `h` reads 3 where `H` reads 15 for the same instant, so the raw numbers
 * differ without the text contradicting itself.
 */
#define SET_FIELD(bit, member, value)                                        \
  do {                                                                       \
    if ((out->present & (uint32_t)(bit)) != 0                                \
        && out->member != (value)) {                                         \
      return gchron_fail(err, GCHRON_ERR_FORMAT,                             \
          GCHRON_DIAG_PATTERN_FIELD_CONFLICT, began, 1);                     \
    }                                                                        \
    out->member = (value);                                                   \
    out->present |= (uint32_t)(bit);                                         \
  } while (0)

GCHRON_Result gchron_format_parse(const GCHRON_Format * format,
    const char * text, size_t len, const GCHRON_PatternContext * context,
    GCHRON_ParsedFields * out, GCHRON_Error * err) {
  GCHRON_PatternContext defaults;
  const GCHRON_Names * names;
  Cursor c;
  size_t i;
  GCHRON_Result result;

  if (format == NULL || out == NULL || (text == NULL && len != 0)) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  /* Cleared before anything else, so that a caller cannot read a field from
   * a previous parse out of a struct this one never wrote (mistake M15, and
   * the reason gchron_parse_options_default() memsets). */
  memset(out, 0, sizeof(*out));

  if (context == NULL) {
    memset(&defaults, 0, sizeof(defaults));
    context = &defaults;
  }
  if (context->lenience != GCHRON_PATTERN_STRICT) {
    return gchron_fail(err, GCHRON_ERR_UNSUPPORTED, GCHRON_DIAG_NONE, 0, 0);
  }
  names = context->names != NULL ? context->names : gchron_names_english();

  result = gchron_format_is_invertible(format, err);
  if (result != GCHRON_OK) {
    return result;
  }

  if (format->in_utc) {
    /*
     * The format states its own zone, and the reader has to hear it. An
     * HTTP-date's `GMT` is a literal, so the loop below matches it as text
     * and sets no offset - which left `parse(write(x))` unable to rebuild an
     * offset date-time from a format that had just asserted one. RFC 9110
     * section 5.6.7 makes GMT the only zone an HTTP-date has, which is the
     * same thing gchron_parse_http_date() already tells its callers.
     *
     * Set before the loop rather than after, so that a pattern which also
     * carries a real offset letter would meet the conflict check at the
     * point every other repeated field meets it.
     */
    out->offset_sec = 0;
    out->present |= (uint32_t)GCHRON_FIELD_OFFSET;
  }

  c.text = text;
  c.len = len;
  c.pos = 0;

  for (i = 0; i < format->item_count; ++i) {
    const GCHRON_FormatItem * item = &format->items[i];
    int count = (int)item->count;
    size_t began = c.pos;
    int64_t value = 0;
    int index = 0;
    /* Exactly `count` digits only when another number follows immediately;
     * otherwise `count` is the minimum the emitter padded to. */
    const bool exact = (i + 1 < format->item_count)
        && item_is_numeric(&format->items[i + 1]);

    switch (item->kind) {
      case GCHRON_ITEM_LITERAL:
        if (!take_exact(&c, format->literals + item->literal_at,
                item->literal_len)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_LITERAL, began, 1);
        }
        break;

      case GCHRON_ITEM_ERA:
        if (!take_name(&c, names, NAME_ERA, 0, 1, count, &index)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_PATTERN_NAME,
              began, 1);
        }
        SET_FIELD(GCHRON_FIELD_ERA, era, index);
        break;

      case GCHRON_ITEM_YEAR:
        if (count == 2) {
          /* TR35: `yy` is specifically the low two digits, and nothing else
           * about the pattern says which century they belong to. */
          if (!take_digits_adj(&c, 2, 2, exact, &value)) {
            return gchron_fail(err, GCHRON_ERR_FORMAT,
                GCHRON_DIAG_PATTERN_DIGITS, began, 1);
          }
          if (context->clock == NULL) {
            return gchron_fail(err, GCHRON_ERR_INVALID,
                GCHRON_DIAG_PATTERN_NEEDS_CLOCK, began, 2);
          }
          result = expand_two_digit_year((int)value, context->clock, &value);
          if (result != GCHRON_OK) {
            return gchron_fail(err, result, GCHRON_DIAG_PATTERN_NEEDS_CLOCK,
                began, 2);
          }
        }
        else if (!take_signed(&c, count, 9, exact, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_YEAR, year, value);
        break;

      case GCHRON_ITEM_EXTENDED_YEAR:
        if (!take_signed(&c, count, 9, exact, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_YEAR, year, value);
        break;

      case GCHRON_ITEM_WEEK_YEAR:
        if (count == 2) {
          /* `YY` is the low two digits of the week-based year, exactly as
           * `yy` is of the ordinary one, and needs the same clock. */
          if (!take_digits_adj(&c, 2, 2, exact, &value)) {
            return gchron_fail(err, GCHRON_ERR_FORMAT,
                GCHRON_DIAG_PATTERN_DIGITS, began, 1);
          }
          if (context->clock == NULL) {
            return gchron_fail(err, GCHRON_ERR_INVALID,
                GCHRON_DIAG_PATTERN_NEEDS_CLOCK, began, 2);
          }
          result = expand_two_digit_year((int)value, context->clock, &value);
          if (result != GCHRON_OK) {
            return gchron_fail(err, result, GCHRON_DIAG_PATTERN_NEEDS_CLOCK,
                began, 2);
          }
        }
        else if (!take_signed(&c, count, 9, exact, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_WEEK_YEAR, week_year, value);
        break;

      case GCHRON_ITEM_CENTURY:
        if (!take_ranged(&c, count, 2, exact, 0, 99, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_CENTURY, century, (int32_t)value);
        break;

      case GCHRON_ITEM_QUARTER:
        /* Quarter *names* are CLDR's and the root provider has none, so the
         * emitter writes `Q1` for three letters or more. That is exactly as
         * readable as the bare number. */
        if (count >= 3 && !take_exact(&c, "Q", 1)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_LITERAL, began, 1);
        }
        if (!take_digits_adj(&c, count >= 3 ? 1 : count, 1, exact, &value)
            || value < 1 || value > 4) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_QUARTER, quarter, (int32_t)value);
        break;

      case GCHRON_ITEM_MONTH:
        if (count >= 3) {
          if (!take_name(&c, names, NAME_MONTH, 1, 12, count, &index)) {
            return gchron_fail(err, GCHRON_ERR_FORMAT,
                GCHRON_DIAG_PATTERN_NAME, began, 1);
          }
          value = index;
        }
        else if (!take_ranged(&c, count, 2, exact, 1, 13, &value)) {
          /* Thirteen, not twelve: a lunisolar calendar has a leap month, and
           * which months exist is the calendar's question rather than the
           * scanner's (section 5.2). */
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_MONTH, month, (int32_t)value);
        break;

      case GCHRON_ITEM_WEEK_OF_YEAR:
        if (!take_ranged(&c, count, 2, exact, 1, 53, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_WEEK_OF_YEAR, week_of_year, (int32_t)value);
        break;

      case GCHRON_ITEM_WEEK_OF_MONTH:
        if (!take_ranged(&c, count, 1, exact, 0, 6, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_WEEK_OF_MONTH, week_of_month,
            (int32_t)value);
        break;

      case GCHRON_ITEM_DAY:
        if (!take_ranged(&c, count, 2, exact, 1, 31, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_DAY, day, (int32_t)value);
        break;

      case GCHRON_ITEM_DAY_SPACE_PADDED:
        while (peek(&c) == ' ') {
          c.pos += 1;
        }
        if (!take_ranged(&c, 1, 2, exact, 1, 31, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_DAY, day, (int32_t)value);
        break;

      case GCHRON_ITEM_DAY_OF_YEAR:
        if (!take_ranged(&c, count, 3, exact, 1, 366, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_DAY_OF_YEAR, day_of_year, (int32_t)value);
        break;

      case GCHRON_ITEM_WEEKDAY_IN_MONTH:
        if (!take_ranged(&c, count, 1, exact, 1, 5, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_WEEKDAY_IN_MONTH, weekday_in_month,
            (int32_t)value);
        break;

      case GCHRON_ITEM_MODIFIED_JULIAN:
        if (!take_signed(&c, count, 9, exact, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_MODIFIED_JULIAN, modified_julian, value);
        break;

      case GCHRON_ITEM_WEEKDAY:
        if (!take_name(&c, names, NAME_WEEKDAY, 1, 7, count, &index)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_PATTERN_NAME,
              began, 1);
        }
        SET_FIELD(GCHRON_FIELD_WEEKDAY, weekday, index);
        break;

      case GCHRON_ITEM_WEEKDAY_LOCAL:
        if (count >= 3) {
          if (!take_name(&c, names, NAME_WEEKDAY, 1, 7, count, &index)) {
            return gchron_fail(err, GCHRON_ERR_FORMAT,
                GCHRON_DIAG_PATTERN_NAME, began, 1);
          }
          SET_FIELD(GCHRON_FIELD_WEEKDAY, weekday, index);
        }
        else {
          /* A local weekday number counts from the locale's first day, so it
           * is only a weekday once that is known. */
          int first = names->first_day_of_week >= 1
              ? names->first_day_of_week : 1;
          if (!take_digits_adj(&c, count, 1, exact, &value) || value < 1 || value > 7) {
            return gchron_fail(err, GCHRON_ERR_FORMAT,
                GCHRON_DIAG_PATTERN_DIGITS, began, 1);
          }
          SET_FIELD(GCHRON_FIELD_WEEKDAY, weekday,
              (int32_t)(((value - 1 + first - 1) % 7) + 1));
        }
        break;

      case GCHRON_ITEM_WEEKDAY_ISO_NUMBER:
        if (!take_digits_adj(&c, count, 1, exact, &value) || value < 1 || value > 7) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_WEEKDAY, weekday, (int32_t)value);
        break;

      case GCHRON_ITEM_WEEKDAY_SUNDAY_ZERO:
        if (!take_digits_adj(&c, count, 1, exact, &value) || value > 6) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_WEEKDAY, weekday,
            (int32_t)(value == 0 ? 7 : value));
        break;

      case GCHRON_ITEM_DAY_PERIOD:
        if (!take_name(&c, names, NAME_DAY_PERIOD, 0, 1, count, &index)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_PATTERN_NAME,
              began, 1);
        }
        SET_FIELD(GCHRON_FIELD_DAY_PERIOD, day_period, index);
        break;

      case GCHRON_ITEM_HOUR_0_23:
      case GCHRON_ITEM_HOUR_1_24:
      case GCHRON_ITEM_HOUR_1_12:
      case GCHRON_ITEM_HOUR_0_11:
      case GCHRON_ITEM_HOUR_0_23_SPACE_PADDED:
      case GCHRON_ITEM_HOUR_1_12_SPACE_PADDED: {
        bool spaced = item->kind == GCHRON_ITEM_HOUR_0_23_SPACE_PADDED
            || item->kind == GCHRON_ITEM_HOUR_1_12_SPACE_PADDED;
        if (spaced) {
          while (peek(&c) == ' ') {
            c.pos += 1;
          }
        }
        int64_t low = 0;
        int64_t high = 23;
        GCHRON_HourCycle cycle = GCHRON_HOUR_0_23;
        switch (item->kind) {
          case GCHRON_ITEM_HOUR_1_24:
            cycle = GCHRON_HOUR_1_24;
            low = 1;
            high = 24;
            break;
          case GCHRON_ITEM_HOUR_1_12:
          case GCHRON_ITEM_HOUR_1_12_SPACE_PADDED:
            cycle = GCHRON_HOUR_1_12;
            low = 1;
            high = 12;
            break;
          case GCHRON_ITEM_HOUR_0_11:
            cycle = GCHRON_HOUR_0_11;
            high = 11;
            break;
          default:
            break;
        }
        if (!take_ranged(&c, spaced ? 1 : count, 2, exact, low, high, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        if ((out->present & (uint32_t)GCHRON_FIELD_HOUR) != 0
            && out->hour_cycle == cycle && out->hour != (int32_t)value) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_FIELD_CONFLICT, began, 1);
        }
        out->hour_cycle = cycle;
        out->hour = (int32_t)value;
        out->present |= (uint32_t)GCHRON_FIELD_HOUR;
        break;
      }

      case GCHRON_ITEM_MINUTE:
        if (!take_ranged(&c, count, 2, exact, 0, 59, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_MINUTE, minute, (int32_t)value);
        break;

      case GCHRON_ITEM_SECOND:
        if (!take_ranged(&c, count, 2, exact, 0, 60, &value)) {
          /* Sixty, because the text may claim a leap second; whether one was
           * issued that day is the leap table's question (section 5.4). */
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_SECOND, second, (int32_t)value);
        break;

      case GCHRON_ITEM_FRACTION: {
        /*
         * `S` writes exactly `count` digits of the nanosecond field, so this
         * reads exactly that many and scales back up.
         *
         * Past the ninth digit the value is consumed and dropped rather than
         * accumulated. A nanosecond field holds nine digits; the emitter
         * writes zeros beyond them, and TR35 allows a count of up to
         * fourteen. Multiplying all of them into one integer is how
         * `SSSSSSSSSS` produced a nanosecond count of ten digits that
         * overflowed the field it was about to be stored in - which is where
         * `fuzz_scan` found it. Cutting rather than rounding is what this
         * library does with an over-long fraction everywhere else (section
         * 8.2): carrying into the next second would change the time.
         */
        int taken = 0;
        int64_t scaled = 0;
        while (taken < count && is_digit(peek(&c))) {
          if (taken < 9) {
            scaled = scaled * 10 + (c.text[c.pos] - '0');
          }
          c.pos += 1;
          taken += 1;
        }
        if (taken < count) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        while (taken < 9) {
          scaled *= 10;
          taken += 1;
        }
        SET_FIELD(GCHRON_FIELD_FRACTION, nsec, (int32_t)scaled);
        break;
      }

      case GCHRON_ITEM_MILLIS_OF_DAY:
        if (!take_ranged(&c, count, 8, exact, 0, 86399999, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_MILLIS_OF_DAY, millis_of_day, value);
        break;

      case GCHRON_ITEM_ZONE_ID: {
        size_t begin = c.pos;
        size_t length;
        while (!at_end(&c) && zone_id_byte(c.text[c.pos])) {
          c.pos += 1;
        }
        length = c.pos - begin;
        if (length == 0 || length > GCHRON_ZONE_ID_MAX) {
          c.pos = begin;
          return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_PATTERN_NAME,
              began, 1);
        }
        if ((out->present & (uint32_t)GCHRON_FIELD_ZONE_ID) != 0
            && (strlen(out->zone_id) != length
                || memcmp(out->zone_id, c.text + begin, length) != 0)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_FIELD_CONFLICT, began, 1);
        }
        memcpy(out->zone_id, c.text + begin, length);
        out->zone_id[length] = '\0';
        out->present |= (uint32_t)GCHRON_FIELD_ZONE_ID;
        break;
      }

      case GCHRON_ITEM_OFFSET_ISO_Z:
      case GCHRON_ITEM_OFFSET_ISO:
      case GCHRON_ITEM_OFFSET_RFC822:
      case GCHRON_ITEM_OFFSET_LOCALISED: {
        /*
         * The same four styles emit.c writes, chosen the same way - `ZZZZ` is
         * the long localised GMT format and not the RFC 822 offset its three
         * shorter spellings are, so it reads as style 3 too.
         *
         * take_offset()'s style 3 branch was written with the rest of this and
         * then reached by nothing: this arm listed only three kinds and
         * computed only styles 0, 1 and 2. Twenty lines of documented reader
         * for `GMT`, `GMT+8` and `GMT+08:00`, unreachable, while
         * gchron_format_parse() refused every pattern that needed it.
         */
        int style = item->kind == GCHRON_ITEM_OFFSET_ISO ? 1
            : item->kind == GCHRON_ITEM_OFFSET_LOCALISED ? 3
            : item->kind != GCHRON_ITEM_OFFSET_RFC822 ? 0
            : (item->count == 4 ? 3 : 2);
        int32_t offset = 0;
        bool unknown = false;
        if (!take_offset(&c, style, &offset, &unknown)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        if ((out->present & (uint32_t)GCHRON_FIELD_OFFSET) != 0
            && (out->offset_sec != offset
                || out->offset_unknown != unknown)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_FIELD_CONFLICT, began, 1);
        }
        out->offset_sec = offset;
        out->offset_unknown = unknown;
        out->present |= (uint32_t)GCHRON_FIELD_OFFSET;
        break;
      }

      case GCHRON_ITEM_EPOCH_SECONDS:
        if (!take_signed(&c, count, 19, exact, &value)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_PATTERN_DIGITS, began, 1);
        }
        SET_FIELD(GCHRON_FIELD_EPOCH_SECONDS, epoch_seconds, value);
        break;

      case GCHRON_ITEM_ZONE_ABBREV:
      case GCHRON_ITEM_COUNT:
      default:
        /* gchron_format_is_invertible() ran first and refused these. */
        return gchron_fail(err, GCHRON_ERR_UNSUPPORTED,
            GCHRON_DIAG_PATTERN_NOT_INVERTIBLE, began, 1);
    }
  }

  out->consumed = c.pos;
  if (!at_end(&c)) {
    /*
     * Text left over is a failure, not a stopping point. A parser that
     * stopped early would read `2026-09-20xyz` as a valid date, and the
     * caller who wanted to scan a longer string wants
     * GCHRON_ParsedFields::consumed from a pattern that ends where the value
     * does.
     */
    return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_PATTERN_TRAILING,
        c.pos, len - c.pos);
  }
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * Turning fields into values
 *--------------------------------------------------------------------------*/

static bool has(const GCHRON_ParsedFields * f, uint32_t bit) {
  return (f->present & bit) != 0;
}

/** The day this library counts epoch days from, as a Modified Julian Day. */
#define MJD_AT_EPOCH INT64_C(40587)

/**
 * Check that one derived fact agrees with what the text also said.
 *
 * A pattern carrying both `E` and `dd` gives two answers about the same day.
 * Ignoring the weekday is what the HTTP-date parser does, because RFC 9110
 * tells it to; here nobody has said so, and a timestamp that contradicts
 * itself is more likely a bug in whatever wrote it than a field to discard.
 */
static GCHRON_Result agree(bool present, int64_t claimed, int64_t derived,
    GCHRON_Error * err) {
  if (present && claimed != derived) {
    return gchron_fail(err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_PATTERN_FIELD_CONFLICT, 0, 0);
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_parsed_to_date(const GCHRON_ParsedFields * fields,
    const GCHRON_PatternContext * context, GCHRON_Date * out,
    GCHRON_Error * err) {
  const GCHRON_Calendar * calendar;
  GCHRON_Date date;
  GCHRON_Result result;
  int64_t year;
  int64_t epoch_day;
  int weekday;

  if (fields == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  calendar = (context != NULL) ? context->calendar : NULL;

  if (has(fields, GCHRON_FIELD_MODIFIED_JULIAN)) {
    /* A Modified Julian Day names a day outright; nothing else is needed and
     * nothing else is consulted. */
    if (!gchron_sub_i64(fields->modified_julian, MJD_AT_EPOCH, &epoch_day)) {
      return gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_NONE, 0, 0);
    }
    result = gchron_date_from_epoch_day(epoch_day, &date);
    if (result != GCHRON_OK) {
      return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, 0);
    }
  }
  else if (has(fields, GCHRON_FIELD_WEEK_YEAR)
      && has(fields, GCHRON_FIELD_WEEK_OF_YEAR)
      && has(fields, GCHRON_FIELD_WEEKDAY)) {
    GCHRON_IsoWeekDate week;
    if (fields->week_year < INT32_MIN || fields->week_year > INT32_MAX) {
      return gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_NONE, 0, 0);
    }
    memset(&week, 0, sizeof(week));
    week.week_year = (int32_t)fields->week_year;
    week.week = (uint8_t)fields->week_of_year;
    week.day = (uint8_t)fields->weekday;
    result = gchron_date_from_iso_week(&week, &date);
    if (result != GCHRON_OK) {
      return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, 0);
    }
  }
  else if (has(fields, GCHRON_FIELD_YEAR)
      && has(fields, GCHRON_FIELD_DAY_OF_YEAR)
      && !has(fields, GCHRON_FIELD_MONTH)) {
    year = fields->year;
    if (has(fields, GCHRON_FIELD_ERA) && fields->era == 0) {
      /* BCE: year 1 BCE is proleptic year 0, 2 BCE is -1. */
      year = 1 - year;
    }
    if (year < INT32_MIN || year > INT32_MAX) {
      return gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_NONE, 0, 0);
    }
    result = gchron_date_from_ordinal((int32_t)year,
        fields->day_of_year, &date);
    if (result != GCHRON_OK) {
      return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, 0);
    }
  }
  else if (has(fields, GCHRON_FIELD_YEAR) && has(fields, GCHRON_FIELD_MONTH)
      && has(fields, GCHRON_FIELD_DAY)) {
    year = fields->year;
    if (has(fields, GCHRON_FIELD_ERA) && fields->era == 0) {
      year = 1 - year;
    }
    if (year < INT32_MIN || year > INT32_MAX) {
      return gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_NONE, 0, 0);
    }
    memset(&date, 0, sizeof(date));
    date.year = (int32_t)year;
    date.month = (uint8_t)fields->month;
    date.day = (uint8_t)fields->day;
    if (!gchron_calendar_date_is_valid(calendar, &date)) {
      return gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_NONE, 0, 0);
    }
    if (calendar != NULL) {
      /* A non-Gregorian date is read in its own calendar and handed back in
       * this library's proleptic Gregorian one, which is what every other
       * value here is written in (section 5.2). */
      result = gchron_calendar_to_epoch_day(calendar, &date, &epoch_day);
      if (result != GCHRON_OK) {
        return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, 0);
      }
      result = gchron_date_from_epoch_day(epoch_day, &date);
      if (result != GCHRON_OK) {
        return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, 0);
      }
    }
  }
  else {
    /* None of the four ways to write a date is complete. */
    return gchron_fail(err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_PATTERN_FIELD_MISSING, 0, 0);
  }

  /* Whatever the text said twice, it must have said consistently. */
  result = gchron_date_day_of_week(&date, &weekday);
  if (result != GCHRON_OK) {
    return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, 0);
  }
  result = agree(has(fields, GCHRON_FIELD_WEEKDAY), fields->weekday, weekday,
      err);
  if (result != GCHRON_OK) {
    return result;
  }
  result = agree(has(fields, GCHRON_FIELD_MONTH), fields->month, date.month,
      err);
  if (result != GCHRON_OK) {
    return result;
  }
  result = agree(has(fields, GCHRON_FIELD_DAY), fields->day, date.day, err);
  if (result != GCHRON_OK) {
    return result;
  }
  if (has(fields, GCHRON_FIELD_QUARTER)) {
    result = agree(true, fields->quarter, (date.month - 1) / 3 + 1, err);
    if (result != GCHRON_OK) {
      return result;
    }
  }
  if (has(fields, GCHRON_FIELD_CENTURY)) {
    result = agree(true, fields->century, gchron_floor_div(date.year, 100),
        err);
    if (result != GCHRON_OK) {
      return result;
    }
  }

  *out = date;
  return GCHRON_OK;
}

GCHRON_Result gchron_parsed_to_time(const GCHRON_ParsedFields * fields,
    const GCHRON_PatternContext * context, GCHRON_Time * out,
    GCHRON_Error * err) {
  int hour;
  GCHRON_Result result;

  (void)context;
  if (fields == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }

  if (has(fields, GCHRON_FIELD_MILLIS_OF_DAY)
      && !has(fields, GCHRON_FIELD_HOUR)) {
    int64_t millis = fields->millis_of_day;
    if (millis < 0 || millis >= INT64_C(86400000)) {
      return gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_NONE, 0, 0);
    }
    return gchron_time_create((int)(millis / 3600000),
        (int)((millis / 60000) % 60), (int)((millis / 1000) % 60),
        (int32_t)((millis % 1000) * 1000000), out) == GCHRON_OK
        ? GCHRON_OK
        : gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_NONE, 0, 0);
  }

  if (!has(fields, GCHRON_FIELD_HOUR)) {
    return gchron_fail(err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_PATTERN_FIELD_MISSING, 0, 0);
  }
  hour = fields->hour;

  switch (fields->hour_cycle) {
    case GCHRON_HOUR_0_23:
      break;
    case GCHRON_HOUR_1_24:
      /* `k` counts 1..24, and 24 is the midnight that starts the day. */
      if (hour < 1 || hour > 24) {
        return gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_NONE, 0, 0);
      }
      hour = (hour == 24) ? 0 : hour;
      break;
    case GCHRON_HOUR_1_12:
    case GCHRON_HOUR_0_11:
      /*
       * Twelve-hour time is half an answer, and the missing half is the one
       * that decides whether a log line is from breakfast or dinner.
       */
      if (!has(fields, GCHRON_FIELD_DAY_PERIOD)) {
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_PATTERN_FIELD_MISSING, 0, 0);
      }
      if (fields->hour_cycle == GCHRON_HOUR_1_12) {
        if (hour < 1 || hour > 12) {
          return gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_NONE, 0, 0);
        }
        hour = (hour == 12) ? 0 : hour;
      }
      else if (hour < 0 || hour > 11) {
        return gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_NONE, 0, 0);
      }
      hour += (fields->day_period == 1) ? 12 : 0;
      break;
    default:
      return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }

  /*
   * A minute or second the text did not carry is zero, because a time of day
   * is a complete value and `15:30` means `15:30:00` in every notation this
   * library reads. An *hour* it did not carry is refused above rather than
   * defaulted, since that would turn an empty parse into midnight.
   */
  result = gchron_time_create(hour,
      has(fields, GCHRON_FIELD_MINUTE) ? fields->minute : 0,
      has(fields, GCHRON_FIELD_SECOND) ? fields->second : 0,
      has(fields, GCHRON_FIELD_FRACTION) ? fields->nsec : 0, out);
  if (result != GCHRON_OK) {
    return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, 0);
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_parsed_to_datetime(const GCHRON_ParsedFields * fields,
    const GCHRON_PatternContext * context, GCHRON_DateTime * out,
    GCHRON_Error * err) {
  GCHRON_DateTime value;
  GCHRON_Result result;

  if (fields == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  memset(&value, 0, sizeof(value));
  result = gchron_parsed_to_date(fields, context, &value.date, err);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_parsed_to_time(fields, context, &value.time, err);
  if (result != GCHRON_OK) {
    return result;
  }
  *out = value;
  return GCHRON_OK;
}

GCHRON_Result gchron_parsed_to_offset(const GCHRON_ParsedFields * fields,
    const GCHRON_PatternContext * context, GCHRON_OffsetDateTime * out,
    GCHRON_Error * err) {
  GCHRON_OffsetDateTime value;
  GCHRON_Result result;

  if (fields == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  if (!has(fields, GCHRON_FIELD_OFFSET)) {
    /*
     * Not defaulted to UTC. A timestamp with no zone information is a civil
     * date-time, and calling it UTC is how a log line written in Adelaide
     * becomes wrong by nine and a half hours - silently, and in a direction
     * nobody checks.
     */
    return gchron_fail(err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_PATTERN_FIELD_MISSING, 0, 0);
  }
  memset(&value, 0, sizeof(value));
  result = gchron_parsed_to_datetime(fields, context, &value.civil, err);
  if (result != GCHRON_OK) {
    return result;
  }
  value.offset_sec = fields->offset_sec;
  /* `-00:00` is RFC 3339 4.3's *unknown offset*, and carrying it through is
   * mistake M14: the text was telling the reader something. */
  value.offset_unknown = fields->offset_unknown;
  *out = value;
  return GCHRON_OK;
}
