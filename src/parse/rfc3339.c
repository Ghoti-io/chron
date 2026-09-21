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
 * RFC 3339 section 5.6: `full-date`, `partial-time`, `full-time`, `date-time`.
 *
 * The productions here are shared with the TOML grammar, which defines its
 * four types in terms of them.
 *
 * Reference: RFC 3339 (2002) sections 4.3, 5.6 and 5.7.
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/offset.h>
#include <ghoti.io/chron/leap.h>
#include <ghoti.io/chron/parse.h>

#include "../core/core_internal.h"
#include "parse_internal.h"

GCHRON_Result gchron_scan_full_date(GCHRON_Scanner * sc, GCHRON_Date * out) {
  int year;
  int month;
  int day;
  int length;
  GCHRON_Result result;
  size_t month_start;
  size_t day_start;

  /*
   * `date-fullyear = 4DIGIT`: exactly four, with no sign and no expanded
   * form. `+2020-01-01`, `12020-01-01` and `998-01-01` are ISO 8601 questions
   * and are not this grammar; the ISO 8601 profile in phase 2 is where an
   * expanded year is read.
   */
  result = gchron_scan_digits(sc, 4, &year);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_scan_literal(sc, '-', GCHRON_DIAG_EXPECTED_HYPHEN);
  if (result != GCHRON_OK) {
    return result;
  }
  month_start = sc->pos;
  result = gchron_scan_digits(sc, 2, &month);
  if (result != GCHRON_OK) {
    return result;
  }
  if (month < 1 || month > 12) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_MONTH_OUT_OF_RANGE, month_start, 2);
  }
  result = gchron_scan_literal(sc, '-', GCHRON_DIAG_EXPECTED_HYPHEN);
  if (result != GCHRON_OK) {
    return result;
  }
  day_start = sc->pos;
  result = gchron_scan_digits(sc, 2, &day);
  if (result != GCHRON_OK) {
    return result;
  }
  if (gchron_date_days_in_month(year, month, &length) != GCHRON_OK) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_MONTH_OUT_OF_RANGE, month_start, 2);
  }
  if (day < 1 || day > length) {
    /*
     * The length of the month, not 31: `2020-02-30` and `2021-02-29` are as
     * wrong as `2020-01-32`, and a parser that defers the question to a
     * later validity check is one whose `format` result disagrees with its
     * own value.
     */
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_DAY_OUT_OF_RANGE, day_start, 2);
  }
  return gchron_date_create(year, month, day, out);
}

/**
 * Read `time-offset = "Z" / time-numoffset`.
 *
 * @param sc The scanner.
 * @param out_sec Receives the offset in seconds.
 * @param out_unknown Receives whether it was written `-00:00`.
 * @return GCHRON_OK or GCHRON_ERR_FORMAT.
 */
static GCHRON_Result scan_offset(GCHRON_Scanner * sc, int32_t * out_sec,
    bool * out_unknown) {
  char c;
  int negative;
  int hour;
  int minute;
  size_t field_start;
  GCHRON_Result result;

  if (sc->pos >= sc->len) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT, GCHRON_DIAG_UNEXPECTED_END,
        sc->pos, 0);
  }
  c = sc->text[sc->pos];
  if (c == 'Z' || c == 'z') {
    sc->pos += 1;
    *out_sec = 0;
    *out_unknown = false;
    return GCHRON_OK;
  }
  if (c != '+' && c != '-') {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT, GCHRON_DIAG_EXPECTED_OFFSET,
        sc->pos, 1);
  }
  negative = (c == '-');
  sc->pos += 1;

  field_start = sc->pos;
  result = gchron_scan_digits(sc, 2, &hour);
  if (result != GCHRON_OK) {
    return result;
  }
  /* `time-numoffset` is built from `time-hour`, which is 00-23. */
  if (hour > 23) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_OFFSET_OUT_OF_RANGE, field_start, 2);
  }
  result = gchron_scan_literal(sc, ':', GCHRON_DIAG_EXPECTED_COLON);
  if (result != GCHRON_OK) {
    return result;
  }
  field_start = sc->pos;
  result = gchron_scan_digits(sc, 2, &minute);
  if (result != GCHRON_OK) {
    return result;
  }
  if (minute > 59) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_OFFSET_OUT_OF_RANGE, field_start, 2);
  }

  *out_sec = (int32_t)((hour * 3600 + minute * 60) * (negative ? -1 : 1));
  /*
   * RFC 3339 section 4.3: `-00:00` says the local offset is *unknown*, which
   * `Z` and `+00:00` do not say. Preserved rather than folded into zero,
   * because a log line carrying it is telling you something and a library
   * that rewrites it has destroyed evidence (design.md, mistake M14).
   */
  *out_unknown = (negative && *out_sec == 0);
  return GCHRON_OK;
}

/**
 * Read `time-secfrac = "." 1*DIGIT`, if one is there.
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
  int scale = 100000000;

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

  if (digits == 0) {
    /* `1*DIGIT`: `08:30:06.Z` is not a time. */
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT, GCHRON_DIAG_FRACTION_EMPTY,
        start, 0);
  }
  if (digits > 9) {
    if (sc->opts->fraction == GCHRON_FRACTION_REJECT) {
      return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
          GCHRON_DIAG_FRACTION_TOO_LONG, start + 9, digits - 9);
    }
    /*
     * Truncate, never round. A parser that rounds `23:59:59.9999999999` into
     * the next day has changed the date (design.md, mistake M22).
     */
    out->truncated = true;
  }
  out->time.nsec = nsec;
  out->digits = (uint8_t)(digits > 255 ? 255 : digits);
  return GCHRON_OK;
}

GCHRON_Result gchron_scan_full_time(GCHRON_Scanner * sc,
    GCHRON_OffsetMode mode, GCHRON_TimeParts * out) {
  int hour;
  int minute;
  int second;
  size_t field_start;
  size_t second_start;
  GCHRON_Result result;

  out->leap_second = false;
  out->offset_sec = 0;
  out->offset_unknown = false;
  out->has_offset = false;

  field_start = sc->pos;
  result = gchron_scan_digits(sc, 2, &hour);
  if (result != GCHRON_OK) {
    return result;
  }
  if (hour > 23) {
    /*
     * `time-hour = 2DIGIT ; 00-23`. Hour 24 is invalid even beside a leap
     * second, which is what the suite's `2016-12-31T24:59:60+01:00` checks.
     * ISO 8601's `24:00:00` end-of-day is a different grammar.
     */
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_HOUR_OUT_OF_RANGE, field_start, 2);
  }
  result = gchron_scan_literal(sc, ':', GCHRON_DIAG_EXPECTED_COLON);
  if (result != GCHRON_OK) {
    return result;
  }
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

  if (mode == GCHRON_OFFSET_REQUIRED
      || (mode == GCHRON_OFFSET_OPTIONAL && sc->pos < sc->len
          && (sc->text[sc->pos] == 'Z' || sc->text[sc->pos] == 'z'
              || sc->text[sc->pos] == '+' || sc->text[sc->pos] == '-'))) {
    result = scan_offset(sc, &out->offset_sec, &out->offset_unknown);
    if (result != GCHRON_OK) {
      return result;
    }
    out->has_offset = true;
  }

  if (second == 60) {
    /*
     * The leap-second policy is applied here, after the offset, because
     * GCHRON_LEAP_MINUTE's question - "is this 23:59 in UTC?" - cannot be
     * answered until the offset is known. `01:29:60+01:30` is a leap second
     * and `23:59:60+01:00` is not, and the digits before the offset are the
     * same shape in both.
     */
    switch (sc->opts->leap) {
      case GCHRON_LEAP_REJECT:
        return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_LEAP_SECOND_REJECTED, second_start, 2);

      case GCHRON_LEAP_MINUTE: {
        int64_t local_sec = (int64_t)hour * 3600 + (int64_t)minute * 60;
        int64_t utc_sec = gchron_floor_mod(local_sec - out->offset_sec,
            GCHRON_SECONDS_PER_DAY);
        if (!out->has_offset || utc_sec != 23 * 3600 + 59 * 60) {
          return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_LEAP_SECOND_WRONG_MINUTE, second_start, 2);
        }
        break;
      }

      case GCHRON_LEAP_TABLE: {
        /*
         * `TABLE` is `MINUTE` plus one more question, and only the first half
         * can be asked here: a `full-time` production has no date, and
         * whether *this day* gained a second is a question about a date. The
         * second half runs in gchron_scan_check_leap_table(), which the
         * date-bearing parsers call once they have one.
         */
        int64_t local_sec = (int64_t)hour * 3600 + (int64_t)minute * 60;
        int64_t utc_sec = gchron_floor_mod(local_sec - out->offset_sec,
            GCHRON_SECONDS_PER_DAY);
        if (!out->has_offset || utc_sec != 23 * 3600 + 59 * 60) {
          return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_LEAP_SECOND_WRONG_MINUTE, second_start, 2);
        }
        if (sc->opts->leap_table == NULL) {
          /*
           * No quiet fallback to GCHRON_LEAP_MINUTE. A caller who asked for
           * the strict reading and silently got the loose one has a check
           * that passes for the wrong reason.
           */
          return gchron_fail(sc->err, GCHRON_ERR_INVALID,
              GCHRON_DIAG_LEAP_SECOND_REJECTED, second_start, 2);
        }
        break;
      }

      case GCHRON_LEAP_CLAMP:
      default:
        break;
    }
    /*
     * Whatever the policy allowed, the value is `:59` of the same minute with
     * the same fraction - where the Linux kernel puts the repeated second -
     * and `leap_second` is the evidence that the text said otherwise.
     */
    second = 59;
    out->leap_second = true;
  }

  out->time.hour = (uint8_t)hour;
  out->time.minute = (uint8_t)minute;
  out->time.second = (uint8_t)second;
  return GCHRON_OK;
}

/** Copy what the text said into the caller's info, if they wanted it. */
static void fill_info(GCHRON_ParseInfo * info, const GCHRON_TimeParts * parts) {
  if (info == NULL) {
    return;
  }
  info->leap_second = parts->leap_second;
  info->fraction_truncated = parts->truncated;
  info->fraction_digits = parts->digits;
  info->offset_unknown = parts->offset_unknown;
}

GCHRON_Result gchron_scan_check_leap_table(const GCHRON_ParseOptions * opts,
    const GCHRON_Date * date, const GCHRON_TimeParts * parts,
    GCHRON_Error * err) {
  GCHRON_Date utc_date;
  int64_t epoch_day = 0;
  int64_t local_sec;
  bool is_leap = false;
  GCHRON_Result result;

  if (opts->leap != GCHRON_LEAP_TABLE || !parts->leap_second) {
    return GCHRON_OK;
  }
  if (opts->leap_table == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID,
        GCHRON_DIAG_LEAP_SECOND_REJECTED, 0, 0);
  }

  /*
   * The date in UTC, which is not always the date in the text: the suite's
   * own `1998-12-31T15:59:60.123-08:00` is a leap second belonging to the
   * next UTC day, and asking the table about 1998-12-31 local would be
   * asking about the wrong day.
   */
  if (gchron_date_to_epoch_day(date, &epoch_day) != GCHRON_OK) {
    return gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_YEAR_OUT_OF_RANGE,
        0, 0);
  }
  local_sec = (int64_t)parts->time.hour * 3600
      + (int64_t)parts->time.minute * 60;
  epoch_day += gchron_floor_div(local_sec - parts->offset_sec,
      GCHRON_SECONDS_PER_DAY);
  if (gchron_date_from_epoch_day(epoch_day, &utc_date) != GCHRON_OK) {
    return gchron_fail(err, GCHRON_ERR_RANGE, GCHRON_DIAG_YEAR_OUT_OF_RANGE,
        0, 0);
  }

  result = gchron_leap_is_leap_day(opts->leap_table, &utc_date, &is_leap);
  if (result != GCHRON_OK) {
    /*
     * Before the table begins, or past its expiry. Both reach the caller
     * unchanged, because "I cannot know" is a different answer from "that is
     * not a leap second" and a caller may reasonably treat them differently.
     */
    return gchron_fail(err, result, GCHRON_DIAG_LEAP_SECOND_REJECTED, 0, 0);
  }
  if (!is_leap) {
    return gchron_fail(err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_LEAP_SECOND_NOT_IN_TABLE, 0, 0);
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_parse_rfc3339_date_time(const char * text, size_t len,
    const GCHRON_ParseOptions * opts, GCHRON_OffsetDateTime * out,
    GCHRON_ParseInfo * info, GCHRON_Error * err) {
  GCHRON_ParseOptions fallback;
  GCHRON_Scanner sc;
  GCHRON_Date date;
  GCHRON_TimeParts parts;
  GCHRON_DateTime civil;
  GCHRON_Result result;

  gchron_error_clear(err);
  gchron_parse_info_clear(info);
  if (text == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }

  sc.text = text;
  sc.len = len;
  sc.pos = 0;
  sc.opts = gchron_parse_options_effective(opts, &fallback);
  sc.err = err;

  result = gchron_scan_preflight(&sc);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_scan_full_date(&sc, &date);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_scan_date_time_separator(&sc);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_scan_full_time(&sc, GCHRON_OFFSET_REQUIRED, &parts);
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

  civil.date = date;
  civil.time = parts.time;
  result = gchron_offset_create(&civil, parts.offset_sec, parts.offset_unknown,
      out);
  if (result != GCHRON_OK) {
    return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, sc.pos);
  }
  fill_info(info, &parts);
  return GCHRON_OK;
}

GCHRON_Result gchron_parse_rfc3339_full_date(const char * text, size_t len,
    const GCHRON_ParseOptions * opts, GCHRON_Date * out,
    GCHRON_ParseInfo * info, GCHRON_Error * err) {
  GCHRON_ParseOptions fallback;
  GCHRON_Scanner sc;
  GCHRON_Date date;
  GCHRON_Result result;

  gchron_error_clear(err);
  gchron_parse_info_clear(info);
  if (text == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }

  sc.text = text;
  sc.len = len;
  sc.pos = 0;
  sc.opts = gchron_parse_options_effective(opts, &fallback);
  sc.err = err;

  result = gchron_scan_preflight(&sc);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_scan_full_date(&sc, &date);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_scan_finish(&sc, info);
  if (result != GCHRON_OK) {
    return result;
  }
  *out = date;
  return GCHRON_OK;
}

GCHRON_Result gchron_parse_rfc3339_full_time(const char * text, size_t len,
    const GCHRON_ParseOptions * opts, GCHRON_OffsetTime * out,
    GCHRON_ParseInfo * info, GCHRON_Error * err) {
  GCHRON_ParseOptions fallback;
  GCHRON_Scanner sc;
  GCHRON_TimeParts parts;
  GCHRON_Result result;

  gchron_error_clear(err);
  gchron_parse_info_clear(info);
  if (text == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }

  sc.text = text;
  sc.len = len;
  sc.pos = 0;
  sc.opts = gchron_parse_options_effective(opts, &fallback);
  sc.err = err;

  result = gchron_scan_preflight(&sc);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_scan_full_time(&sc, GCHRON_OFFSET_REQUIRED, &parts);
  if (result != GCHRON_OK) {
    return result;
  }
  if (parts.leap_second && sc.opts->leap == GCHRON_LEAP_TABLE) {
    /*
     * GCHRON_LEAP_TABLE asks whether *this day* gained a second, and a
     * `full-time` has no day. Refused rather than quietly settled at
     * GCHRON_LEAP_MINUTE's strictness: a caller who asked for the strict
     * reading and got the loose one has a check that passes for the wrong
     * reason, and that is the whole reason the two are separate levels.
     */
    return gchron_fail(err, GCHRON_ERR_UNSUPPORTED,
        GCHRON_DIAG_LEAP_SECOND_NOT_IN_TABLE, 0, 0);
  }
  result = gchron_scan_finish(&sc, info);
  if (result != GCHRON_OK) {
    return result;
  }

  out->time = parts.time;
  out->offset_sec = parts.offset_sec;
  out->offset_unknown = parts.offset_unknown;
  fill_info(info, &parts);
  return GCHRON_OK;
}
