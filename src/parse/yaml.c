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
 * The YAML 1.1 `!!timestamp` type.
 *
 * YAML defines this type as a regular expression rather than as a grammar
 * built on RFC 3339, and the two differ in more ways than they agree: a one-
 * or two-digit month, day and hour; one *or more* spaces or tabs in place of
 * `T`; a fraction that may carry no digits at all; an offset that may omit
 * its minutes, write its hour in one digit, or not be there. So the
 * productions here are their own rather than RFC 3339's with five flags bolted
 * on - a scanner with a switch for each of those differences is one a caller
 * has to enable four things on to get the one they wanted.
 *
 * Reference: the YAML 1.1 type repository, `tag:yaml.org,2002:timestamp`,
 * whose regular expression is the definition of the type:
 *
 *     [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]
 *    |[0-9][0-9][0-9][0-9]
 *     -[0-9][0-9]?
 *     -[0-9][0-9]?
 *     ([Tt]|[ \t]+)[0-9][0-9]?
 *     :[0-9][0-9]
 *     :[0-9][0-9]
 *     (\.[0-9]*)?
 *     (([ \t]*)Z|[-+][0-9][0-9]?(:[0-9][0-9])?)?
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/offset.h>
#include <ghoti.io/chron/parse.h>

#include "../core/core_internal.h"
#include "parse_internal.h"

/** Whether a byte is one of the two YAML counts as inline whitespace. */
static bool is_space(char c) {
  return c == ' ' || c == '\t';
}

/**
 * Read `[0-9][0-9]?` - one digit, or two if a second is there.
 *
 * @param sc The scanner.
 * @param out Receives the value.
 * @param out_width Receives how many digits were read. May be NULL.
 * @return GCHRON_OK or GCHRON_ERR_FORMAT.
 */
static GCHRON_Result scan_digits_1_or_2(GCHRON_Scanner * sc, int * out,
    int * out_width) {
  GCHRON_Result result = gchron_scan_digits(sc, 1, out);

  if (result != GCHRON_OK) {
    return result;
  }
  if (sc->pos < sc->len && sc->text[sc->pos] >= '0'
      && sc->text[sc->pos] <= '9') {
    *out = *out * 10 + (sc->text[sc->pos] - '0');
    sc->pos += 1;
    if (out_width != NULL) {
      *out_width = 2;
    }
    return GCHRON_OK;
  }
  if (out_width != NULL) {
    *out_width = 1;
  }
  return GCHRON_OK;
}

/**
 * Read `(\.[0-9]*)?`, if one is there.
 *
 * YAML's fraction is `*DIGIT` where RFC 3339's is `1*DIGIT`, so `21:59:43.Z`
 * is a timestamp here and is not one there. The difference is small and real:
 * a parser that shared RFC 3339's production would reject a document some
 * other YAML reader accepts, and the two would disagree about what the
 * document *says* rather than about how to spell it.
 *
 * @param sc The scanner.
 * @param out Receives the parts, whose fraction fields this fills in.
 * @return GCHRON_OK or GCHRON_ERR_FORMAT.
 */
static GCHRON_Result scan_secfrac(GCHRON_Scanner * sc,
    GCHRON_TimeParts * out) {
  size_t start;
  size_t digits = 0;
  int32_t nsec = 0;
  int32_t scale = 100000000;

  out->digits = 0;
  out->truncated = false;
  out->time.nsec = 0;

  if (sc->pos >= sc->len || sc->text[sc->pos] != '.') {
    return GCHRON_OK;
  }
  sc->pos += 1;
  start = sc->pos;

  while (sc->pos < sc->len && sc->text[sc->pos] >= '0'
      && sc->text[sc->pos] <= '9') {
    if (digits < 9) {
      nsec += (int32_t)(sc->text[sc->pos] - '0') * scale;
      scale /= 10;
    }
    digits += 1;
    sc->pos += 1;
  }

  if (digits > 9) {
    if (sc->opts->fraction == GCHRON_FRACTION_REJECT) {
      return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
          GCHRON_DIAG_FRACTION_TOO_LONG, start + 9, digits - 9);
    }
    /* Truncate, never round (design.md, mistake M22). */
    out->truncated = true;
  }
  out->time.nsec = nsec;
  out->digits = (uint8_t)(digits > 255 ? 255 : digits);
  return GCHRON_OK;
}

/**
 * Read `([ \t]*)Z | [-+][0-9][0-9]?(:[0-9][0-9])?`, if one is there.
 *
 * **Whitespace is accepted before either branch**, and the type repository's
 * expression writes it before `Z` alone. Two things decided that. PyYAML -
 * the reference implementation of YAML 1.1, and the parser most existing 1.1
 * documents were written against - hoists the `[ \t]*` outside the whole
 * group and so accepts `21:59:43 -05:00`; and the published expression offers
 * no reason why a space should be readable before `Z` and not before an
 * offset, which reads as an oversight in the regex rather than a statement
 * about the language. Accepting it means this parser reads every document
 * either of them reads. It is a deviation all the same, it is recorded in
 * documentation/text-formats.md, and the writer never emits one.
 *
 * @param sc The scanner.
 * @param out Receives the parts, whose offset fields this fills in.
 * @return GCHRON_OK or GCHRON_ERR_FORMAT.
 */
static GCHRON_Result scan_zone(GCHRON_Scanner * sc, GCHRON_TimeParts * out,
    bool * out_is_z) {
  size_t rewind = sc->pos;
  int negative;
  int hour;
  int minute = 0;
  size_t field_start;
  char c;
  GCHRON_Result result;

  out->offset_sec = 0;
  out->offset_unknown = false;
  out->has_offset = false;
  *out_is_z = false;

  while (sc->pos < sc->len && is_space(sc->text[sc->pos])) {
    sc->pos += 1;
  }
  if (sc->pos >= sc->len) {
    /*
     * Trailing whitespace and no zone. Rewound rather than consumed, so that
     * gchron_scan_finish() reports it as the trailing text it is: the grammar
     * ends at the fraction, and " " after it is not part of any production.
     */
    sc->pos = rewind;
    return GCHRON_OK;
  }

  c = sc->text[sc->pos];
  if (c == 'Z') {
    sc->pos += 1;
    out->has_offset = true;
    *out_is_z = true;
    return GCHRON_OK;
  }
  if (c == 'z') {
    /*
     * Upper case only, and the asymmetry with `t` is the specification's.
     * YAML's expression writes `[Tt]` for the separator and a bare `Z` for
     * the zone, and PyYAML's copy of it agrees - so `21:59:43z` is a `!!str`
     * and `t21:59:43Z` is a timestamp. RFC 3339 is the other way round, where
     * RFC 5234 section 2.3 makes every literal case-insensitive; a parser
     * that carried that habit across grammars would accept a scalar every
     * other YAML 1.1 reader treats as a string. Its own diagnostic, because
     * "expected Z or an offset" reads as though nothing were there.
     */
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_YAML_LOWERCASE_Z, sc->pos, 1);
  }
  if (c != '+' && c != '-') {
    sc->pos = rewind;
    return GCHRON_OK;
  }
  negative = (c == '-');
  sc->pos += 1;

  field_start = sc->pos;
  result = scan_digits_1_or_2(sc, &hour, NULL);
  if (result != GCHRON_OK) {
    return result;
  }
  if (hour > 23) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_OFFSET_OUT_OF_RANGE, field_start, sc->pos - field_start);
  }
  if (sc->pos < sc->len && sc->text[sc->pos] == ':') {
    sc->pos += 1;
    field_start = sc->pos;
    result = gchron_scan_digits(sc, 2, &minute);
    if (result != GCHRON_OK) {
      return result;
    }
    if (minute > 59) {
      return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
          GCHRON_DIAG_OFFSET_OUT_OF_RANGE, field_start, 2);
    }
  }

  out->offset_sec = (int32_t)((hour * 3600 + minute * 60)
      * (negative ? -1 : 1));
  /* RFC 3339 section 4.3's *unknown*, which YAML inherits by writing the
     same spelling (design.md, mistake M14). */
  out->offset_unknown = (negative && out->offset_sec == 0);
  out->has_offset = true;
  return GCHRON_OK;
}

/**
 * Read the time half: `hour ":" minute ":" second [fraction] [zone]`.
 *
 * @param sc The scanner.
 * @param out Receives the time and what the text said about it.
 * @param out_is_z Receives whether the offset was spelled `Z`.
 * @return GCHRON_OK or GCHRON_ERR_FORMAT.
 */
static GCHRON_Result scan_time(GCHRON_Scanner * sc, GCHRON_TimeParts * out,
    bool * out_is_z) {
  int hour;
  int minute;
  int second;
  size_t field_start;
  size_t second_start;
  GCHRON_Result result;

  out->leap_second = false;

  field_start = sc->pos;
  result = scan_digits_1_or_2(sc, &hour, NULL);
  if (result != GCHRON_OK) {
    return result;
  }
  if (hour > 23) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_HOUR_OUT_OF_RANGE, field_start, sc->pos - field_start);
  }
  result = gchron_scan_literal(sc, ':', GCHRON_DIAG_EXPECTED_COLON);
  if (result != GCHRON_OK) {
    return result;
  }
  /* The minute and the second are `[0-9][0-9]`: exactly two, where the hour
     before them is one or two. The asymmetry is the specification's. */
  field_start = sc->pos;
  result = gchron_scan_digits(sc, 2, &minute);
  if (result != GCHRON_OK) {
    return result;
  }
  if (minute > 59) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_MINUTE_OUT_OF_RANGE, field_start, 2);
  }
  result = gchron_scan_literal(sc, ':', GCHRON_DIAG_EXPECTED_COLON);
  if (result != GCHRON_OK) {
    return result;
  }
  second_start = sc->pos;
  result = gchron_scan_digits(sc, 2, &second);
  if (result != GCHRON_OK) {
    return result;
  }
  if (second > 60) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_SECOND_OUT_OF_RANGE, second_start, 2);
  }

  result = scan_secfrac(sc, out);
  if (result != GCHRON_OK) {
    return result;
  }
  result = scan_zone(sc, out, out_is_z);
  if (result != GCHRON_OK) {
    return result;
  }

  if (second == 60) {
    /*
     * Applied after the zone, because GCHRON_LEAP_MINUTE's question - "is
     * this 23:59 in UTC?" - cannot be answered until the offset is known.
     * A YAML timestamp may carry no offset at all, and then there is no UTC
     * minute to check, so MINUTE and TABLE refuse it rather than assuming the
     * document meant UTC.
     */
    switch (sc->opts->leap) {
      case GCHRON_LEAP_REJECT:
        return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_LEAP_SECOND_REJECTED, second_start, 2);

      case GCHRON_LEAP_MINUTE:
      case GCHRON_LEAP_TABLE: {
        int64_t local_sec = (int64_t)hour * 3600 + (int64_t)minute * 60;
        int64_t utc_sec = gchron_floor_mod(local_sec - out->offset_sec,
            GCHRON_SECONDS_PER_DAY);
        if (!out->has_offset || utc_sec != 23 * 3600 + 59 * 60) {
          return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_LEAP_SECOND_WRONG_MINUTE, second_start, 2);
        }
        if (sc->opts->leap == GCHRON_LEAP_TABLE
            && sc->opts->leap_table == NULL) {
          /* No quiet fallback to GCHRON_LEAP_MINUTE: a caller who asked for
             the strict reading and silently got the loose one has a check
             that passes for the wrong reason. */
          return gchron_fail(sc->err, GCHRON_ERR_INVALID,
              GCHRON_DIAG_LEAP_SECOND_REJECTED, second_start, 2);
        }
        break;
      }

      case GCHRON_LEAP_CLAMP:
      default:
        break;
    }
    second = 59;
    out->leap_second = true;
  }

  out->time.hour = (uint8_t)hour;
  out->time.minute = (uint8_t)minute;
  out->time.second = (uint8_t)second;
  return GCHRON_OK;
}

GCHRON_Result gchron_parse_yaml_timestamp(const char * text, size_t len,
    const GCHRON_ParseOptions * opts, GCHRON_YamlValue * out,
    GCHRON_ParseInfo * info, GCHRON_Error * err) {
  GCHRON_ParseOptions fallback;
  GCHRON_Scanner sc;
  GCHRON_TimeParts parts;
  GCHRON_YamlValue value;
  GCHRON_Date date;
  int year;
  int month;
  int day;
  int month_width = 0;
  int day_width = 0;
  int length;
  size_t month_start;
  size_t day_start;
  size_t after_day;
  bool has_time = false;
  bool is_z = false;
  GCHRON_Result result;

  gchron_error_clear(err);
  gchron_parse_info_clear(info);
  if (text == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }

  if (opts != NULL) {
    sc.opts = opts;
  }
  else {
    /* As with TOML, a caller who named the grammar has already chosen its
       options; gchron_parse_options_yaml() says which and why. */
    gchron_parse_options_yaml(&fallback);
    sc.opts = &fallback;
  }
  sc.text = text;
  sc.len = len;
  sc.pos = 0;
  sc.err = err;

  result = gchron_scan_preflight(&sc);
  if (result != GCHRON_OK) {
    return result;
  }

  result = gchron_scan_digits(&sc, 4, &year);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_scan_literal(&sc, '-', GCHRON_DIAG_EXPECTED_HYPHEN);
  if (result != GCHRON_OK) {
    return result;
  }
  month_start = sc.pos;
  result = scan_digits_1_or_2(&sc, &month, &month_width);
  if (result != GCHRON_OK) {
    return result;
  }
  if (month < 1 || month > 12) {
    return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_MONTH_OUT_OF_RANGE,
        month_start, sc.pos - month_start);
  }
  result = gchron_scan_literal(&sc, '-', GCHRON_DIAG_EXPECTED_HYPHEN);
  if (result != GCHRON_OK) {
    return result;
  }
  day_start = sc.pos;
  result = scan_digits_1_or_2(&sc, &day, &day_width);
  if (result != GCHRON_OK) {
    return result;
  }
  if (gchron_date_days_in_month(year, month, &length) != GCHRON_OK) {
    return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_MONTH_OUT_OF_RANGE,
        month_start, sc.pos - month_start);
  }
  if (day < 1 || day > length) {
    /* The length of the month, not 31. A `format` result that disagreed with
       its own value would be worse than either answer alone. */
    return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_DAY_OUT_OF_RANGE,
        day_start, sc.pos - day_start);
  }
  result = gchron_date_create(year, month, day, &date);
  if (result != GCHRON_OK) {
    return gchron_fail(err, result, GCHRON_DIAG_YEAR_OUT_OF_RANGE, 0, 4);
  }

  /*
   * Which of the two alternatives this is. `T` or `t` commits to the one with
   * a time outright; whitespace commits only when a digit follows the run,
   * so that `2001-12-14 and more` under GCHRON_ParseOptions::allow_trailing
   * is a date with trailing text rather than a failed hour. One byte of
   * lookahead each way, and no backtracking into a production that has
   * already reported an error.
   */
  after_day = sc.pos;
  if (sc.pos < sc.len && (sc.text[sc.pos] == 'T' || sc.text[sc.pos] == 't')) {
    sc.pos += 1;
    has_time = true;
  }
  else if (sc.pos < sc.len && is_space(sc.text[sc.pos])) {
    size_t probe = sc.pos;
    while (probe < sc.len && is_space(sc.text[probe])) {
      probe += 1;
    }
    if (probe < sc.len && sc.text[probe] >= '0' && sc.text[probe] <= '9') {
      sc.pos = probe;
      has_time = true;
    }
  }

  if (!has_time) {
    sc.pos = after_day;
    /*
     * YAML's *first* alternative is `YYYY-MM-DD` with both fields exactly two
     * digits; only the second - the one carrying a time - relaxes them. So
     * `2001-12-4` is not a timestamp while `2001-12-4T21:59:43Z` is. That
     * reads as an inconsistency and is what the type repository says, and a
     * parser that smoothed it over would resolve a scalar as `!!timestamp`
     * that every other YAML 1.1 reader resolves as `!!str`.
     */
    if (month_width != 2 || day_width != 2) {
      return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_YAML_DATE_WIDTH,
          month_start, sc.pos - month_start);
    }
    result = gchron_scan_finish(&sc, info);
    if (result != GCHRON_OK) {
      return result;
    }
    value.kind = GCHRON_YAML_DATE;
    value.civil.date = date;
    value.civil.time.hour = 0;
    value.civil.time.minute = 0;
    value.civil.time.second = 0;
    value.civil.time.nsec = 0;
    value.offset_sec = 0;
    value.offset_unknown = false;
    value.offset_is_z = false;
    *out = value;
    return GCHRON_OK;
  }

  result = scan_time(&sc, &parts, &is_z);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_scan_check_leap_table(sc.opts, &date, &parts, err);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_scan_finish(&sc, info);
  if (result != GCHRON_OK) {
    return result;
  }

  value.kind = parts.has_offset ? GCHRON_YAML_OFFSET_DATE_TIME
                                : GCHRON_YAML_DATE_TIME;
  value.civil.date = date;
  value.civil.time = parts.time;
  value.offset_sec = parts.has_offset ? parts.offset_sec : 0;
  value.offset_unknown = parts.has_offset ? parts.offset_unknown : false;
  value.offset_is_z = is_z;
  *out = value;
  if (info != NULL) {
    info->leap_second = parts.leap_second;
    info->fraction_truncated = parts.truncated;
    info->fraction_digits = parts.digits;
    info->offset_unknown = value.offset_unknown;
  }
  return GCHRON_OK;
}

bool gchron_yaml_timestamp_identical(const GCHRON_YamlValue * a,
    const GCHRON_YamlValue * b) {
  if (a == NULL || b == NULL) {
    return a == b;
  }
  /*
   * Field by field, not memcmp: this struct has padding, and padding is not
   * part of the value (design.md section 7). Two structures holding the same
   * timestamp can differ in bytes a compiler never promised to set.
   */
  return a->kind == b->kind
      && gchron_datetime_compare(&a->civil, &b->civil) == 0
      && a->offset_sec == b->offset_sec
      && a->offset_unknown == b->offset_unknown
      && a->offset_is_z == b->offset_is_z;
}
