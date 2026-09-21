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
 * The proleptic Gregorian calendar: the conversion between a `(year, month,
 * day)` label and the epoch day it labels.
 *
 * Reference: Howard Hinnant, *chrono-Compatible Low-Level Date Algorithms*,
 * `days_from_civil` and `civil_from_days`, with their verified ranges. The
 * algorithms are used rather than a table because they are closed-form, are
 * exact over a range far wider than this library's, and are what the oracles
 * in documentation/design.md section 12 were checked against.
 */

#include <ghoti.io/chron/macros.h>

#include "civil_internal.h"

bool gchron_gregory_is_leap(int64_t year) {
  /*
   * Written on int64_t rather than int32_t so that `year + 1`-style
   * expressions elsewhere cannot overflow at GCHRON_YEAR_MAX. The guard band
   * design.md section 3.2 describes is what makes that free.
   */
  return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

int gchron_gregory_days_in_month(int64_t year, int month) {
  static const int LENGTHS[13] = {
    0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };
  if (month == 2 && gchron_gregory_is_leap(year)) {
    return 29;
  }
  return LENGTHS[month];
}

int64_t gchron_gregory_to_epoch_day(int64_t year, int month, int day) {
  int64_t y = year - (month <= 2 ? 1 : 0);
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;                       /* [0, 399]      */
  int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; /* [0, 146096] */
  return era * 146097 + doe - 719468;
}

void gchron_gregory_from_epoch_day(int64_t epoch_day, int64_t * out_year,
    int * out_month, int * out_day) {
  int64_t z = epoch_day + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097;                    /* [0, 146096]   */
  int64_t yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0, 399] */
  int64_t y = yoe + era * 400;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);     /* [0, 365] */
  int64_t mp = (5 * doy + 2) / 153;                          /* [0, 11]  */
  int64_t d = doy - (153 * mp + 2) / 5 + 1;                  /* [1, 31]  */
  int64_t m = mp + (mp < 10 ? 3 : -9);                       /* [1, 12]  */

  *out_year = y + (m <= 2 ? 1 : 0);
  *out_month = (int)m;
  *out_day = (int)d;
}
