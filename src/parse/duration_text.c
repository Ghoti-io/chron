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
 * RFC 3339 appendix A's `duration` grammar, in both directions.
 *
 * This is **not** ISO 8601's duration grammar, and the difference is not
 * cosmetic. Appendix A permits no sign and no fraction, and its productions
 * nest:
 *
 *     dur-second = 1*DIGIT "S"
 *     dur-minute = 1*DIGIT "M" [dur-second]
 *     dur-hour   = 1*DIGIT "H" [dur-minute]
 *     dur-time   = "T" (dur-hour / dur-minute / dur-second)
 *     dur-day    = 1*DIGIT "D"
 *     dur-week   = 1*DIGIT "W"
 *     dur-month  = 1*DIGIT "M" [dur-day]
 *     dur-year   = 1*DIGIT "Y" [dur-month]
 *     dur-date   = (dur-day / dur-month / dur-year) [dur-time]
 *     duration   = "P" (dur-date / dur-time / dur-week)
 *
 * so `P1Y2M` and `P1M2D` are durations and `P1Y2D` is not, and `PT1M2S` is
 * and `PT1H2S` is not. A parser written as "read numbers and unit letters
 * until they run out" accepts all four, which is how a JSON Schema `format`
 * check comes to pass text no other implementation accepts. The permissive
 * ISO 8601 grammar - which does spell `PT0.5S` and `-P1D` - arrives with
 * phase 2 as a separate function.
 *
 * Reference: RFC 3339 (2002) appendix A.
 */

#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/parse.h>
#include <stdio.h>
#include <string.h>

#include "../core/core_internal.h"
#include "parse_internal.h"

/** One `1*DIGIT` followed by its unit letter. */
typedef struct DurationComponent {
  int64_t value;  /**< The number, saturated at INT64_MAX. */
  char unit;      /**< The unit letter that followed it. */
  bool overflow;  /**< The number was too large for int64_t. */
} DurationComponent;

/** Whether a byte is one of appendix A's unit letters. */
static bool is_unit_letter(char c) {
  return c == 'Y' || c == 'M' || c == 'W' || c == 'D' || c == 'H'
      || c == 'S';
}

/**
 * Read `1*DIGIT` and the unit letter after it.
 *
 * A number too large for `int64_t` is not a grammar error: the text is still
 * a duration, and saying so is what lets a `format` check agree with every
 * other implementation on `P999999999999999999999999D`. The overflow travels
 * out in @ref DurationComponent::overflow and becomes GCHRON_ERR_RANGE only
 * once the whole text has been shown to parse.
 */
static GCHRON_Result scan_component(GCHRON_Scanner * sc,
    DurationComponent * out) {
  size_t start = sc->pos;
  size_t digits = 0;
  char c;

  out->value = 0;
  out->overflow = false;

  while (sc->pos < sc->len && sc->text[sc->pos] >= '0'
      && sc->text[sc->pos] <= '9') {
    int digit = sc->text[sc->pos] - '0';
    if (!out->overflow) {
      if (!gchron_mul_i64(out->value, 10, &out->value)
          || !gchron_add_i64(out->value, digit, &out->value)) {
        out->overflow = true;
      }
    }
    digits += 1;
    sc->pos += 1;
  }

  if (digits == 0) {
    if (sc->pos < sc->len
        && (sc->text[sc->pos] == '-' || sc->text[sc->pos] == '+')) {
      return gchron_fail(sc->err, GCHRON_ERR_FORMAT, GCHRON_DIAG_DURATION_SIGN,
          sc->pos, 1);
    }
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_EXPECTED_DIGIT, sc->pos, 0);
  }

  if (sc->pos >= sc->len) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_DURATION_MISSING_UNIT, start, sc->pos - start);
  }
  c = sc->text[sc->pos];
  if (c == '.' || c == ',') {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_DURATION_FRACTION, sc->pos, 1);
  }
  if (!is_unit_letter(c)) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_DURATION_MISSING_UNIT, sc->pos, 1);
  }
  sc->pos += 1;
  out->unit = c;
  return GCHRON_OK;
}

/** Whether a component is waiting at the cursor. */
static bool component_ahead(const GCHRON_Scanner * sc) {
  return sc->pos < sc->len && sc->text[sc->pos] >= '0'
      && sc->text[sc->pos] <= '9';
}

/** Report a unit that appendix A does not allow in this position. */
static GCHRON_Result wrong_unit(GCHRON_Scanner * sc,
    const DurationComponent * c, size_t at) {
  if (c->unit == 'W') {
    /*
     * `dur-week` is the whole of the duration or nothing: `P1Y2W`, `P0Y1W`
     * and `P1WT1H` are all refused, and each of them is a case the
     * JSON-Schema-Test-Suite checks.
     */
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_DURATION_WEEK_COMBINED, at, 1);
  }
  return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
      GCHRON_DIAG_DURATION_UNIT_ORDER, at, 1);
}

/**
 * Read `dur-time = "T" (dur-hour / dur-minute / dur-second)`.
 *
 * The nesting is what makes `PT1H2S` invalid while `PT1M2S` is valid: a
 * `dur-hour` may be followed only by a `dur-minute`, and a `dur-minute` only
 * by a `dur-second`.
 */
static GCHRON_Result scan_dur_time(GCHRON_Scanner * sc, GCHRON_Duration * out,
    bool * overflow) {
  DurationComponent c;
  size_t at;
  GCHRON_Result result;

  result = gchron_scan_literal(sc, 'T', GCHRON_DIAG_EXPECTED_SEPARATOR);
  if (result != GCHRON_OK) {
    return result;
  }
  if (!component_ahead(sc)) {
    /* `PT` and `P1YT` have a time separator and no time. */
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT, GCHRON_DIAG_DURATION_EMPTY,
        sc->pos, 0);
  }

  at = sc->pos;
  result = scan_component(sc, &c);
  if (result != GCHRON_OK) {
    return result;
  }
  *overflow = *overflow || c.overflow;

  if (c.unit == 'H') {
    out->hours = c.value;
    if (!component_ahead(sc)) {
      return GCHRON_OK;
    }
    at = sc->pos;
    result = scan_component(sc, &c);
    if (result != GCHRON_OK) {
      return result;
    }
    *overflow = *overflow || c.overflow;
    if (c.unit != 'M') {
      return wrong_unit(sc, &c, at);
    }
  }

  if (c.unit == 'M') {
    out->minutes = c.value;
    if (!component_ahead(sc)) {
      return GCHRON_OK;
    }
    at = sc->pos;
    result = scan_component(sc, &c);
    if (result != GCHRON_OK) {
      return result;
    }
    *overflow = *overflow || c.overflow;
    if (c.unit != 'S') {
      return wrong_unit(sc, &c, at);
    }
  }

  if (c.unit != 'S') {
    return wrong_unit(sc, &c, at);
  }
  out->seconds = c.value;
  return GCHRON_OK;
}

GCHRON_Result gchron_parse_rfc3339_duration(const char * text, size_t len,
    const GCHRON_ParseOptions * opts, GCHRON_Duration * out,
    GCHRON_ParseInfo * info, GCHRON_Error * err) {
  GCHRON_ParseOptions fallback;
  GCHRON_Scanner sc;
  GCHRON_Duration value;
  DurationComponent c;
  bool overflow = false;
  bool saw_date = false;
  size_t at;
  GCHRON_Result result;

  gchron_error_clear(err);
  gchron_parse_info_clear(info);
  if (text == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }

  memset(&value, 0, sizeof(value));

  sc.text = text;
  sc.len = len;
  sc.pos = 0;
  sc.opts = gchron_parse_options_effective(opts, &fallback);
  sc.err = err;

  result = gchron_scan_preflight(&sc);
  if (result != GCHRON_OK) {
    return result;
  }
  if (text[0] == '-' || text[0] == '+') {
    /*
     * Appendix A has no sign. Named as its own diagnostic because `-P1D` is a
     * caller writing ISO 8601-2, not a caller writing nonsense, and phase 2's
     * ISO 8601 parser is where it belongs.
     */
    return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_DURATION_SIGN, 0, 1);
  }
  result = gchron_scan_literal(&sc, 'P', GCHRON_DIAG_EXPECTED_SEPARATOR);
  if (result != GCHRON_OK) {
    return result;
  }

  if (sc.pos < sc.len && sc.text[sc.pos] == 'T') {
    result = scan_dur_time(&sc, &value, &overflow);
    if (result != GCHRON_OK) {
      return result;
    }
  }
  else {
    if (!component_ahead(&sc)) {
      if (sc.pos < sc.len
          && (sc.text[sc.pos] == '-' || sc.text[sc.pos] == '+')) {
        /* `P-1D`: ISO 8601-2 spells a sign inside a component; this does not. */
        return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_DURATION_SIGN,
            sc.pos, 1);
      }
      /* `P` alone is not a duration. */
      return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_DURATION_EMPTY,
          sc.pos, 0);
    }
    at = sc.pos;
    result = scan_component(&sc, &c);
    if (result != GCHRON_OK) {
      return result;
    }
    overflow = overflow || c.overflow;

    if (c.unit == 'W') {
      /* `duration = "P" (dur-date / dur-time / dur-week)`: weeks stand alone. */
      value.weeks = c.value;
      if (sc.pos != sc.len) {
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_DURATION_WEEK_COMBINED, sc.pos, sc.len - sc.pos);
      }
    }
    else {
      saw_date = true;
      if (c.unit == 'Y') {
        value.years = c.value;
        if (component_ahead(&sc)) {
          at = sc.pos;
          result = scan_component(&sc, &c);
          if (result != GCHRON_OK) {
            return result;
          }
          overflow = overflow || c.overflow;
          if (c.unit != 'M') {
            /* `P1Y2D`: years and days cannot appear without months. */
            return wrong_unit(&sc, &c, at);
          }
        }
      }
      if (c.unit == 'M') {
        value.months = c.value;
        if (component_ahead(&sc)) {
          at = sc.pos;
          result = scan_component(&sc, &c);
          if (result != GCHRON_OK) {
            return result;
          }
          overflow = overflow || c.overflow;
          if (c.unit != 'D') {
            return wrong_unit(&sc, &c, at);
          }
        }
      }
      if (c.unit == 'D') {
        value.days = c.value;
      }
      else if (c.unit != 'Y' && c.unit != 'M') {
        /* `P2S`: a time unit in the date position. */
        return wrong_unit(&sc, &c, at);
      }
    }
  }

  if (saw_date && sc.pos < sc.len && sc.text[sc.pos] == 'T') {
    result = scan_dur_time(&sc, &value, &overflow);
    if (result != GCHRON_OK) {
      return result;
    }
  }

  result = gchron_scan_finish(&sc, info);
  if (result != GCHRON_OK) {
    return result;
  }
  if (overflow) {
    /*
     * The text *is* a duration; no integer can hold it. GCHRON_ERR_RANGE and
     * not GCHRON_ERR_FORMAT, so that a caller performing a `format` check
     * treats it as a pass and a caller wanting the value does not.
     */
    return gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_NONE, 0, len);
  }
  *out = value;
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * Writing
 *--------------------------------------------------------------------------*/

/** Append a decimal number and its unit letter, if the number is non-zero. */
static void append_component(char * scratch, size_t size, size_t * at,
    int64_t value, char unit, bool force) {
  int written;

  if (value == 0 && !force) {
    return;
  }
  written = snprintf(scratch + *at, size - *at, "%lld%c", (long long)value,
      unit);
  if (written > 0) {
    *at += (size_t)written;
  }
}

GCHRON_Result gchron_write_rfc3339_duration(const GCHRON_Duration * d,
    char * buf, size_t buf_len, size_t * out_len) {
  char scratch[GCHRON_RFC3339_DURATION_MAX];
  size_t at = 0;
  size_t length;

  if (buf == NULL && buf_len != 0) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_duration_is_valid(d)) {
    return GCHRON_ERR_INVALID;
  }
  if (gchron_duration_sign(d) < 0 || d->nsec != 0) {
    /*
     * Appendix A has no sign and no fraction. Refusing rather than writing
     * something it does not define: phase 2's ISO 8601 writer spells both,
     * and a caller who needs them should reach for that rather than get text
     * a conforming reader will refuse.
     */
    return GCHRON_ERR_UNSUPPORTED;
  }
  if (d->weeks != 0
      && (d->years != 0 || d->months != 0 || d->days != 0 || d->hours != 0
          || d->minutes != 0 || d->seconds != 0)) {
    /* `dur-week` stands alone; this duration has no appendix A spelling. */
    return GCHRON_ERR_UNSUPPORTED;
  }

  scratch[at++] = 'P';
  if (d->weeks != 0) {
    append_component(scratch, sizeof(scratch), &at, d->weeks, 'W', true);
  }
  else {
    /*
     * The intermediate zeros are not decoration. `dur-year = 1*DIGIT "Y"
     * [dur-month]` means a duration of one year and two days spells
     * `P1Y0M2D`, because appendix A has no way to skip the months.
     */
    bool need_month = d->years != 0 && d->days != 0 && d->months == 0;
    bool need_minute = d->hours != 0 && d->seconds != 0 && d->minutes == 0;

    append_component(scratch, sizeof(scratch), &at, d->years, 'Y', false);
    append_component(scratch, sizeof(scratch), &at, d->months, 'M', need_month);
    append_component(scratch, sizeof(scratch), &at, d->days, 'D', false);

    if (d->hours != 0 || d->minutes != 0 || d->seconds != 0) {
      scratch[at++] = 'T';
      append_component(scratch, sizeof(scratch), &at, d->hours, 'H', false);
      append_component(scratch, sizeof(scratch), &at, d->minutes, 'M', need_minute);
      append_component(scratch, sizeof(scratch), &at, d->seconds, 'S', false);
    }
    else if (at == 1) {
      /* A duration of zero. `P` alone is not one; `PT0S` is the shortest. */
      scratch[at++] = 'T';
      scratch[at++] = '0';
      scratch[at++] = 'S';
    }
  }
  scratch[at] = '\0';
  length = at;

  if (out_len != NULL) {
    *out_len = length;
  }
  if (buf_len < length + 1) {
    return GCHRON_ERR_LIMIT;
  }
  memcpy(buf, scratch, length + 1);
  return GCHRON_OK;
}
