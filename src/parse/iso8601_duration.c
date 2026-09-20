/**
 * @file
 *
 * The ISO 8601 duration grammar, in both directions.
 *
 * The permissive one, and deliberately not RFC 3339 appendix A's. The two
 * look alike and are not the same grammar, and `src/parse/duration_text.c`
 * carries appendix A's nesting rules; this one has none of them. Where they
 * part company:
 *
 *     P1Y2D       ISO 8601 yes, appendix A no  (no nesting here)
 *     PT1H2S      ISO 8601 yes, appendix A no
 *     PT0.5S      ISO 8601 yes, appendix A no  (a fraction)
 *     -P1D, P-1D  ISO 8601 yes, appendix A no  (a sign; P-1D is 8601-2)
 *     P1W2D       ISO 8601 yes, appendix A no  (weeks alongside)
 *
 * Reference: ISO 8601-1:2019 section 5.5.2; ISO 8601-2:2019 for the signed
 * components.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/parse.h>
#include <stdio.h>
#include <string.h>

#include "../core/core_internal.h"
#include "parse_internal.h"

/** How far the parse has got through the unit order. */
typedef enum {
  STAGE_DATE = 0,
  STAGE_TIME
} Stage;

/** One component: a number, maybe a fraction, and a unit letter. */
typedef struct Component {
  int64_t value;
  int64_t fraction_numerator; /**< Scaled by @ref fraction_scale. */
  int64_t fraction_scale;     /**< 1 when there was no fraction. */
  bool negative;
  bool overflow;
  char unit;
} Component;

/** Whether a byte is a unit letter in this grammar. */
static bool is_unit(char c, Stage stage) {
  if (stage == STAGE_DATE) {
    return c == 'Y' || c == 'M' || c == 'W' || c == 'D';
  }
  return c == 'H' || c == 'M' || c == 'S';
}

/** Read one `[sign] 1*DIGIT [("." / ",") 1*DIGIT] UNIT`. */
static GCHRON_Result scan_component(GCHRON_Scanner * sc, Stage stage,
    Component * out) {
  size_t digits = 0;
  char c;

  memset(out, 0, sizeof(*out));
  out->fraction_scale = 1;

  if (sc->pos < sc->len
      && (sc->text[sc->pos] == '-' || sc->text[sc->pos] == '+')) {
    /* ISO 8601-2 permits a sign inside a component. */
    out->negative = (sc->text[sc->pos] == '-');
    sc->pos += 1;
  }

  while (sc->pos < sc->len && sc->text[sc->pos] >= '0'
      && sc->text[sc->pos] <= '9') {
    if (!out->overflow) {
      if (!gchron_mul_i64(out->value, 10, &out->value)
          || !gchron_add_i64(out->value, sc->text[sc->pos] - '0',
                 &out->value)) {
        out->overflow = true;
      }
    }
    digits += 1;
    sc->pos += 1;
  }
  if (digits == 0) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT, GCHRON_DIAG_EXPECTED_DIGIT,
        sc->pos, 0);
  }

  if (sc->pos < sc->len
      && (sc->text[sc->pos] == '.' || sc->text[sc->pos] == ',')) {
    /* ISO 8601 permits both separators; the comma is the standard's own
     * preference and the full stop is what everybody writes. */
    size_t fraction_digits = 0;
    sc->pos += 1;
    while (sc->pos < sc->len && sc->text[sc->pos] >= '0'
        && sc->text[sc->pos] <= '9') {
      if (fraction_digits < 9) {
        out->fraction_numerator =
            out->fraction_numerator * 10 + (sc->text[sc->pos] - '0');
        out->fraction_scale *= 10;
      }
      fraction_digits += 1;
      sc->pos += 1;
    }
    if (fraction_digits == 0) {
      return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
          GCHRON_DIAG_FRACTION_EMPTY, sc->pos, 0);
    }
  }

  if (sc->pos >= sc->len) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_DURATION_MISSING_UNIT, sc->pos, 0);
  }
  c = sc->text[sc->pos];
  if (!is_unit(c, stage)) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        stage == STAGE_DATE && (c == 'H' || c == 'S')
            ? GCHRON_DIAG_DURATION_UNIT_ORDER
            : GCHRON_DIAG_DURATION_MISSING_UNIT,
        sc->pos, 1);
  }
  sc->pos += 1;
  out->unit = c;
  return GCHRON_OK;
}

/** Store a component into the duration, honouring the overall sign. */
static GCHRON_Result apply(const Component * c, Stage stage, bool negate_all,
    GCHRON_Duration * out, GCHRON_Scanner * sc, size_t at) {
  int64_t value = c->value;
  bool negative = c->negative != negate_all;

  if (c->overflow) {
    /* The text is a duration; no integer holds it. The same split the RFC
     * 3339 grammar makes, and for the same reason. */
    return GCHRON_ERR_RANGE;
  }
  if (negative) {
    if (!gchron_neg_i64(value, &value)) {
      return GCHRON_ERR_RANGE;
    }
  }

  if (c->fraction_scale != 1) {
    /*
     * ISO 8601 permits a fraction on the smallest unit present. On a calendar
     * unit that would mean choosing a length for a year, which is the whole
     * of mistake M9; GCHRON_ERR_UNSUPPORTED says so rather than guessing at
     * 365.2425 days.
     */
    if (c->unit != 'S') {
      return gchron_fail(sc->err, GCHRON_ERR_UNSUPPORTED,
          GCHRON_DIAG_DURATION_FRACTION, at, 1);
    }
  }

  switch (c->unit) {
    case 'Y': out->years = value; break;
    case 'W': out->weeks = value; break;
    case 'D': out->days = value; break;
    case 'H': out->hours = value; break;
    case 'M':
      if (stage == STAGE_DATE) {
        out->months = value;
      }
      else {
        out->minutes = value;
      }
      break;
    case 'S': {
      int64_t nanos = 0;
      out->seconds = value;
      if (c->fraction_scale != 1) {
        /* Scale the fraction to nanoseconds without floating point: the
         * numerator is at most nine digits and the scale is a power of ten,
         * so this is exact. */
        int64_t scaled = c->fraction_numerator;
        int64_t scale = c->fraction_scale;
        while (scale < GCHRON_NANOS_PER_SECOND) {
          scaled *= 10;
          scale *= 10;
        }
        while (scale > GCHRON_NANOS_PER_SECOND) {
          scaled /= 10;
          scale /= 10;
        }
        nanos = scaled;
        if (negative && nanos != 0) {
          /* nsec carries no sign of its own: -0.5s is {seconds: -1, nsec:
           * 5e8}, the same normalisation GCHRON_Instant uses. */
          if (!gchron_sub_i64(out->seconds, 1, &out->seconds)) {
            return GCHRON_ERR_RANGE;
          }
          nanos = GCHRON_NANOS_PER_SECOND - nanos;
        }
        out->nsec = (int32_t)nanos;
      }
      break;
    }
    default:
      return GCHRON_ERR_INTERNAL;
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_parse_iso8601_duration(const char * text, size_t len,
    const GCHRON_ParseOptions * opts, GCHRON_Duration * out,
    GCHRON_ParseInfo * info, GCHRON_Error * err) {
  GCHRON_ParseOptions fallback;
  GCHRON_Scanner sc;
  GCHRON_Duration value;
  Stage stage = STAGE_DATE;
  bool negate_all = false;
  bool any_component = false;
  bool seen[7];
  GCHRON_Result result;
  bool overflowed = false;

  gchron_error_clear(err);
  gchron_parse_info_clear(info);
  if (text == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  memset(&value, 0, sizeof(value));
  memset(seen, 0, sizeof(seen));

  sc.text = text;
  sc.len = len;
  sc.pos = 0;
  sc.opts = gchron_parse_options_effective(opts, &fallback);
  sc.err = err;

  result = gchron_scan_preflight(&sc);
  if (result != GCHRON_OK) {
    return result;
  }

  if (sc.text[sc.pos] == '-' || sc.text[sc.pos] == '+') {
    negate_all = (sc.text[sc.pos] == '-');
    sc.pos += 1;
  }
  result = gchron_scan_literal(&sc, 'P', GCHRON_DIAG_EXPECTED_SEPARATOR);
  if (result != GCHRON_OK) {
    return result;
  }

  while (sc.pos < sc.len) {
    Component c;
    size_t at;
    int slot;

    if (sc.text[sc.pos] == 'T') {
      if (stage == STAGE_TIME) {
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_DURATION_UNIT_ORDER, sc.pos, 1);
      }
      stage = STAGE_TIME;
      sc.pos += 1;
      if (sc.pos >= sc.len) {
        /* `P1YT` has a time separator and no time. */
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_DURATION_EMPTY, sc.pos, 0);
      }
      continue;
    }

    at = sc.pos;
    result = scan_component(&sc, stage, &c);
    if (result != GCHRON_OK) {
      return result;
    }

    /*
     * Each unit at most once, and in order. ISO 8601 does not nest its
     * productions the way appendix A does, but it does fix the order, and a
     * repeated unit is two answers to one question.
     */
    switch (c.unit) {
      case 'Y': slot = 0; break;
      case 'W': slot = 2; break;
      case 'D': slot = 3; break;
      case 'H': slot = 4; break;
      case 'S': slot = 6; break;
      case 'M': slot = (stage == STAGE_DATE) ? 1 : 5; break;
      default: slot = -1; break;
    }
    if (slot < 0 || seen[slot]) {
      return gchron_fail(err, GCHRON_ERR_FORMAT,
          GCHRON_DIAG_DURATION_UNIT_ORDER, at, 1);
    }
    {
      int earlier;
      for (earlier = slot + 1; earlier < 7; ++earlier) {
        if (seen[earlier]) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_DURATION_UNIT_ORDER, at, 1);
        }
      }
    }
    seen[slot] = true;

    if (c.overflow) {
      overflowed = true;
      c.overflow = false;
    }
    result = apply(&c, stage, negate_all, &value, &sc, at);
    if (result != GCHRON_OK) {
      return result == GCHRON_ERR_RANGE
          ? gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_NONE, at, 1)
          : result;
    }
    any_component = true;
  }

  if (!any_component) {
    return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_DURATION_EMPTY,
        sc.pos, 0);
  }
  result = gchron_scan_finish(&sc, info);
  if (result != GCHRON_OK) {
    return result;
  }
  if (overflowed) {
    return gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_NONE, 0, len);
  }
  if (!gchron_duration_is_valid(&value)) {
    /* Components of disagreeing sign. ISO 8601-2 lets a caller write them;
     * this library's duration type is the one that says a duration has one
     * sign (design.md section 4.1), and "one month minus one day" is two
     * operations rather than one value. */
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_DURATION_SIGN, 0,
        len);
  }
  *out = value;
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * Writing
 *--------------------------------------------------------------------------*/

/** Append a component and its unit, if the value is non-zero. */
static void put_component(char * buffer, size_t size, size_t * at,
    int64_t value, char unit) {
  int written;

  if (value == 0) {
    return;
  }
  written = snprintf(buffer + *at, size - *at, "%lld%c", (long long)value,
      unit);
  if (written > 0) {
    *at += (size_t)written;
  }
}

GCHRON_Result gchron_write_iso8601_duration(const GCHRON_Duration * d,
    char * buf, size_t buf_len, size_t * out_len) {
  char scratch[GCHRON_RFC3339_DURATION_MAX];
  GCHRON_Duration positive;
  size_t at = 0;
  int sign;
  GCHRON_Result result;

  if (buf == NULL && buf_len != 0) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_duration_is_valid(d)) {
    return GCHRON_ERR_INVALID;
  }
  if (d->weeks != 0
      && (d->years != 0 || d->months != 0 || d->days != 0 || d->hours != 0
          || d->minutes != 0 || d->seconds != 0 || d->nsec != 0)) {
    /* ISO 8601 writes a week duration alone. Turning a week into seven days
     * would be rewriting the caller's units, which section 4.2 says this
     * library never does unasked. */
    return GCHRON_ERR_UNSUPPORTED;
  }

  sign = gchron_duration_sign(d);
  if (sign < 0) {
    /* Only `-P1D` is produced; `P-1D` is accepted on input and is ISO
     * 8601-2's spelling rather than 8601-1's. */
    result = gchron_duration_negate(d, &positive);
    if (result != GCHRON_OK) {
      return result;
    }
    scratch[at++] = '-';
  }
  else {
    positive = *d;
  }

  scratch[at++] = 'P';
  put_component(scratch, sizeof(scratch), &at, positive.years, 'Y');
  put_component(scratch, sizeof(scratch), &at, positive.months, 'M');
  put_component(scratch, sizeof(scratch), &at, positive.weeks, 'W');
  put_component(scratch, sizeof(scratch), &at, positive.days, 'D');

  if (positive.hours != 0 || positive.minutes != 0 || positive.seconds != 0
      || positive.nsec != 0) {
    scratch[at++] = 'T';
    put_component(scratch, sizeof(scratch), &at, positive.hours, 'H');
    put_component(scratch, sizeof(scratch), &at, positive.minutes, 'M');
    if (positive.nsec != 0) {
      /*
       * The shortest fraction that loses nothing, with trailing zeros
       * stripped: `PT0.5S` rather than `PT0.500S`. ISO 8601 puts no floor on
       * the digit count, which is the difference from the RFC 3339 writer's
       * 0/3/6/9 - that one keeps a millisecond-shaped output for consumers
       * that expect it.
       */
      int digits = 9;
      int32_t scale = 100000000;
      int32_t trimmed = positive.nsec;
      while (digits > 1 && trimmed % 10 == 0) {
        trimmed /= 10;
        digits -= 1;
      }
      int i;
      int written = snprintf(scratch + at, sizeof(scratch) - at, "%lld",
          (long long)positive.seconds);
      if (written > 0) {
        at += (size_t)written;
      }
      scratch[at++] = '.';
      for (i = 0; i < digits; ++i) {
        scratch[at++] = (char)('0' + (positive.nsec / scale) % 10);
        scale /= 10;
      }
      scratch[at++] = 'S';
    }
    else {
      put_component(scratch, sizeof(scratch), &at, positive.seconds, 'S');
    }
  }
  else if (at == (size_t)(sign < 0 ? 2 : 1)) {
    /* A duration of zero. `P` alone is not one; `PT0S` is the shortest
     * spelling ISO 8601 permits, as it is for appendix A. */
    scratch[at++] = 'T';
    scratch[at++] = '0';
    scratch[at++] = 'S';
  }
  scratch[at] = '\0';

  if (out_len != NULL) {
    *out_len = at;
  }
  if (buf_len < at + 1) {
    return GCHRON_ERR_LIMIT;
  }
  memcpy(buf, scratch, at + 1);
  return GCHRON_OK;
}
