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
 * TOML v1.0.0's four date-time types.
 *
 * TOML distinguishes them by which fields are present rather than by a tag,
 * so this is one parser with one entry point that reports which of the four
 * it read. The Local Time is the shape a YAML timestamp cannot hold, and the
 * reason a library with only "date-time with an offset" cannot read TOML.
 *
 * Reference: TOML v1.0.0, *Offset Date-Time*, *Local Date-Time*, *Local
 * Date*, *Local Time*, which define themselves in terms of RFC 3339 section
 * 5.6 and differ from it in permitting a space where RFC 3339 wants `T`.
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/parse.h>

#include "../core/core_internal.h"
#include "parse_internal.h"

GCHRON_Result gchron_parse_toml(const char * text, size_t len,
    const GCHRON_ParseOptions * opts, GCHRON_TomlValue * out,
    GCHRON_ParseInfo * info, GCHRON_Error * err) {
  GCHRON_ParseOptions fallback;
  GCHRON_Scanner sc;
  GCHRON_Date date;
  GCHRON_TimeParts parts;
  GCHRON_TomlValue value;
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
    /*
     * The one grammar here whose NULL is not the strict default. TOML states
     * what its own options are - truncate a long fraction, accept a space
     * separator - so a caller who names the grammar has already chosen them.
     */
    gchron_parse_options_toml(&fallback);
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

  /*
   * Which of the four this is turns on two bytes: a `partial-time` has `:` at
   * offset 2, a `full-date` has `-` at offset 4. Deciding here rather than by
   * trying each production in turn keeps the error position meaningful - a
   * caller whose `07:3x:00` failed hears about the minute, not about a date
   * that was never there.
   */
  if (sc.len >= 3 && text[2] == ':') {
    result = gchron_scan_full_time(&sc, GCHRON_OFFSET_NONE, &parts);
    if (result != GCHRON_OK) {
      return result;
    }
    result = gchron_scan_finish(&sc, info);
    if (result != GCHRON_OK) {
      return result;
    }
    value.kind = GCHRON_TOML_LOCAL_TIME;
    /*
     * The date half means nothing for a Local Time, and is the epoch rather
     * than zeroes so that the struct always holds a date that exists: year 0
     * month 0 day 0 is not a date, and a caller who read the wrong field
     * should get a wrong answer rather than an invalid one.
     */
    value.civil.date.year = 1970;
    value.civil.date.month = 1;
    value.civil.date.day = 1;
    value.civil.time = parts.time;
    value.offset_sec = 0;
    value.offset_unknown = false;
    *out = value;
    if (info != NULL) {
      info->leap_second = parts.leap_second;
      info->fraction_truncated = parts.truncated;
      info->fraction_digits = parts.digits;
    }
    return GCHRON_OK;
  }

  result = gchron_scan_full_date(&sc, &date);
  if (result != GCHRON_OK) {
    return result;
  }

  if (sc.pos == sc.len) {
    result = gchron_scan_finish(&sc, info);
    if (result != GCHRON_OK) {
      return result;
    }
    value.kind = GCHRON_TOML_LOCAL_DATE;
    value.civil.date = date;
    value.civil.time.hour = 0;
    value.civil.time.minute = 0;
    value.civil.time.second = 0;
    value.civil.time.nsec = 0;
    value.offset_sec = 0;
    value.offset_unknown = false;
    *out = value;
    return GCHRON_OK;
  }

  result = gchron_scan_date_time_separator(&sc);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_scan_full_time(&sc, GCHRON_OFFSET_OPTIONAL, &parts);
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

  value.kind = parts.has_offset ? GCHRON_TOML_OFFSET_DATE_TIME
                                : GCHRON_TOML_LOCAL_DATE_TIME;
  value.civil.date = date;
  value.civil.time = parts.time;
  value.offset_sec = parts.has_offset ? parts.offset_sec : 0;
  value.offset_unknown = parts.has_offset ? parts.offset_unknown : false;
  *out = value;
  if (info != NULL) {
    info->leap_second = parts.leap_second;
    info->fraction_truncated = parts.truncated;
    info->fraction_digits = parts.digits;
    info->offset_unknown = value.offset_unknown;
  }
  return GCHRON_OK;
}
