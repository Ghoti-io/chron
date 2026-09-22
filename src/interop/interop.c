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
 * The encodings other systems use for a timestamp.
 *
 * Mistake M17 is that each of these has a defect and each program that meets
 * one rediscovers it. The defects are documented in `interop.h` beside the
 * functions; the constants that encode them are here, each derived once and
 * checked against an outside calendar in `tests/unit/test_interop.cpp`.
 */

/*
 * `tm_gmtoff` is a BSD extension glibc carries, and neither C17 nor POSIX
 * before 2024 declares it. _DEFAULT_SOURCE makes the name visible; the field
 * is in the struct either way on glibc, so a caller compiled without the
 * macro still passes a `struct tm` of the same layout - only the spelling of
 * that one member changes.
 */
#define _DEFAULT_SOURCE

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/interop.h>
#include <ghoti.io/chron/macros.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/core_internal.h"

/** Seconds from 1601-01-01 to the Unix epoch: Windows' FILETIME origin. */
#define FILETIME_EPOCH_SECONDS INT64_C(-11644473600)

/** Seconds from 0001-01-01 to the Unix epoch: .NET's origin. */
#define DOTNET_EPOCH_SECONDS INT64_C(-62135596800)

/** Seconds from 1900-01-01 to the Unix epoch: NTP's origin. */
#define NTP_EPOCH_SECONDS INT64_C(-2208988800)

/** Seconds in one NTP era: 2^32. */
#define NTP_ERA_SECONDS INT64_C(4294967296)

/** Seconds from 1904-01-01 to the Unix epoch: HFS+ and Excel's 1904 system. */
#define EPOCH_1904_SECONDS INT64_C(-2082844800)

/** Seconds from 2001-01-01 to the Unix epoch: Cocoa's reference date. */
#define COCOA_EPOCH_SECONDS INT64_C(978307200)

/** The epoch day of 1980-01-01, where DOS timestamps begin. */
#define DOS_EPOCH_YEAR 1980

/**
 * The epoch day of 1899-12-30, which is Excel serial 0 in the 1900 system.
 *
 * Not 1899-12-31, which is what "serial 1 is 1900-01-01" would suggest: the
 * extra day is Excel's phantom 29 February 1900, and this constant already
 * carries it. Serials at or below 60 are refused, so the offset is only ever
 * applied where it is right.
 */
#define EXCEL_1900_EPOCH_DAY INT64_C(-25569)

/** The epoch day of 1904-01-01, Excel serial 0 in the 1904 system. */
#define EXCEL_1904_EPOCH_DAY INT64_C(-24107)

/** The epoch day of 1858-11-17, where the Modified Julian Date begins. */
#define MJD_EPOCH_DAY INT64_C(-40587)

/*--------------------------------------------------------------------------*
 * The C library
 *--------------------------------------------------------------------------*/

GCHRON_Result gchron_interop_from_time_t(time_t value, GCHRON_Instant * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  out->sec = (int64_t)value;
  out->nsec = 0;
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_to_time_t(const GCHRON_Instant * i,
    time_t * out) {
  time_t narrowed;

  if (out == NULL || !gchron_instant_is_valid(i)) {
    return GCHRON_ERR_INVALID;
  }
  narrowed = (time_t)i->sec;
  if ((int64_t)narrowed != i->sec) {
    /*
     * `time_t` is 32 bits on some ABIs, and this is the one place a 64-bit
     * library still meets 2038 (mistake M6). Refused rather than wrapped: a
     * wrapped value is a date in 1901 that looks perfectly reasonable.
     */
    return GCHRON_ERR_RANGE;
  }
  *out = narrowed;
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_from_timespec(const struct timespec * value,
    GCHRON_Instant * out) {
  if (value == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (value->tv_nsec < 0 || value->tv_nsec >= GCHRON_NANOS_PER_SECOND) {
    return GCHRON_ERR_INVALID;
  }
  out->sec = (int64_t)value->tv_sec;
  out->nsec = (int32_t)value->tv_nsec;
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_to_timespec(const GCHRON_Instant * i,
    struct timespec * out) {
  time_t narrowed;
  GCHRON_Result result;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_interop_to_time_t(i, &narrowed);
  if (result != GCHRON_OK) {
    return result;
  }
  out->tv_sec = narrowed;
  out->tv_nsec = i->nsec;
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_from_tm(const struct tm * value,
    GCHRON_DateTime * out) {
  int64_t year;
  GCHRON_Result result;

  if (value == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  /* tm_year is years since 1900 and tm_mon is 0-based: mistake M7 in its
   * original form, undone here so that no caller has to remember which way. */
  year = (int64_t)value->tm_year + 1900;
  if (year < GCHRON_YEAR_MIN || year > GCHRON_YEAR_MAX) {
    return GCHRON_ERR_RANGE;
  }
  result = gchron_date_create((int32_t)year, value->tm_mon + 1,
      value->tm_mday, &out->date);
  if (result != GCHRON_OK) {
    return result;
  }
  /*
   * tm_sec can be 60 or 61 in a `struct tm` a leap-second-aware platform
   * filled in. GCHRON_Time holds 0..59, so the value is clamped to :59 the
   * same way a parsed `:60` is - and unlike a parser there is nowhere here to
   * record that it happened, which is one more reason `struct tm` is not a
   * type to keep a timestamp in.
   */
  return gchron_time_create(value->tm_hour, value->tm_min,
      value->tm_sec > 59 ? 59 : value->tm_sec, 0, &out->time);
}

GCHRON_Result gchron_interop_to_tm(const GCHRON_DateTime * dt,
    int32_t offset_sec, struct tm * out) {
  int weekday = 0;
  int day_of_year = 0;

  if (out == NULL || !gchron_datetime_is_valid(dt)) {
    return GCHRON_ERR_INVALID;
  }
  if (gchron_date_day_of_week(&dt->date, &weekday) != GCHRON_OK
      || gchron_date_day_of_year(&dt->date, &day_of_year) != GCHRON_OK) {
    return GCHRON_ERR_RANGE;
  }

  memset(out, 0, sizeof(*out));
  out->tm_year = (int)((int64_t)dt->date.year - 1900);
  out->tm_mon = dt->date.month - 1;
  out->tm_mday = dt->date.day;
  out->tm_hour = dt->time.hour;
  out->tm_min = dt->time.minute;
  out->tm_sec = dt->time.second;
  /* tm_wday is Sunday-0 where this library is Monday-1; the conversion lives
   * here and nowhere else. */
  out->tm_wday = weekday % 7;
  out->tm_yday = day_of_year - 1;
  /* -1 means "not known". This is a civil reading with no zone, and claiming
   * to know would be mistake M4 exactly. */
  out->tm_isdst = -1;
#if defined(__GLIBC__) || defined(__APPLE__) || defined(BSD)
  out->tm_gmtoff = offset_sec;
#else
  /* Not portable, and there is nowhere else to put it. A caller who needs the
   * offset to survive should not be using `struct tm`. */
  (void)offset_sec;
#endif
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * Scaled integer epochs
 *--------------------------------------------------------------------------*/

/** An instant from a count of sub-second units since some epoch. */
static GCHRON_Result from_scaled_epoch(int64_t value, int64_t per_second,
    int64_t epoch_seconds, GCHRON_Instant * out) {
  int64_t seconds;
  int64_t nanos_per_unit = GCHRON_NANOS_PER_SECOND / per_second;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_add_i64(gchron_floor_div(value, per_second), epoch_seconds,
          &seconds)) {
    return GCHRON_ERR_RANGE;
  }
  out->sec = seconds;
  out->nsec =
      (int32_t)(gchron_floor_mod(value, per_second) * nanos_per_unit);
  return GCHRON_OK;
}

/** A count of sub-second units since some epoch, from an instant. */
static GCHRON_Result to_scaled_epoch(const GCHRON_Instant * i,
    int64_t per_second, int64_t epoch_seconds, int64_t * out) {
  int64_t seconds;
  int64_t scaled;
  int64_t nanos_per_unit = GCHRON_NANOS_PER_SECOND / per_second;

  if (out == NULL || !gchron_instant_is_valid(i)) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_sub_i64(i->sec, epoch_seconds, &seconds)
      || !gchron_mul_i64(seconds, per_second, &scaled)
      || !gchron_add_i64(scaled, i->nsec / nanos_per_unit, &scaled)) {
    return GCHRON_ERR_RANGE;
  }
  *out = scaled;
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_from_filetime(uint64_t value,
    GCHRON_Instant * out) {
  if (value > (uint64_t)INT64_MAX) {
    return GCHRON_ERR_RANGE;
  }
  return from_scaled_epoch((int64_t)value, INT64_C(10000000),
      FILETIME_EPOCH_SECONDS, out);
}

GCHRON_Result gchron_interop_to_filetime(const GCHRON_Instant * i,
    uint64_t * out) {
  int64_t value;
  GCHRON_Result result;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = to_scaled_epoch(i, INT64_C(10000000), FILETIME_EPOCH_SECONDS,
      &value);
  if (result != GCHRON_OK) {
    return result;
  }
  if (value < 0) {
    /* FILETIME is unsigned: there is no 1600 in it. */
    return GCHRON_ERR_RANGE;
  }
  *out = (uint64_t)value;
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_from_dotnet_ticks(int64_t value,
    GCHRON_Instant * out) {
  return from_scaled_epoch(value, INT64_C(10000000), DOTNET_EPOCH_SECONDS,
      out);
}

GCHRON_Result gchron_interop_to_dotnet_ticks(const GCHRON_Instant * i,
    int64_t * out) {
  return to_scaled_epoch(i, INT64_C(10000000), DOTNET_EPOCH_SECONDS, out);
}

GCHRON_Result gchron_interop_from_ntp(uint64_t value, int32_t era,
    GCHRON_Instant * out) {
  int64_t seconds = (int64_t)(value >> 32);
  uint64_t fraction = value & UINT64_C(0xFFFFFFFF);
  int64_t era_offset;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  /*
   * NTP's seconds field is 32 bits and era 0 ends on 2036-02-07. Which era a
   * timestamp belongs to is information the timestamp does not carry, so the
   * caller supplies it - and a function that guessed would be wrong for 136
   * years at a time (mistake M17).
   */
  if (!gchron_mul_i64((int64_t)era, NTP_ERA_SECONDS, &era_offset)
      || !gchron_add_i64(seconds, era_offset, &seconds)
      || !gchron_add_i64(seconds, NTP_EPOCH_SECONDS, &seconds)) {
    return GCHRON_ERR_RANGE;
  }
  out->sec = seconds;
  /* The fraction is in units of 2^-32 seconds; scaling through a 64-bit
   * multiply keeps it exact to the nanosecond. */
  out->nsec = (int32_t)((fraction * (uint64_t)GCHRON_NANOS_PER_SECOND)
      >> 32);
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_to_ntp(const GCHRON_Instant * i,
    uint64_t * out_value, int32_t * out_era) {
  int64_t since_1900;
  int64_t era;
  int64_t within;
  uint64_t fraction;

  if (!gchron_instant_is_valid(i)) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_sub_i64(i->sec, NTP_EPOCH_SECONDS, &since_1900)) {
    return GCHRON_ERR_RANGE;
  }
  if (since_1900 < 0) {
    /* There is no NTP timestamp before 1900. */
    return GCHRON_ERR_RANGE;
  }
  era = gchron_floor_div(since_1900, NTP_ERA_SECONDS);
  within = gchron_floor_mod(since_1900, NTP_ERA_SECONDS);
  if (era > INT32_MAX || era < INT32_MIN) {
    return GCHRON_ERR_RANGE;
  }

  fraction = ((uint64_t)i->nsec << 32) / (uint64_t)GCHRON_NANOS_PER_SECOND;
  if (out_value != NULL) {
    *out_value = ((uint64_t)within << 32) | fraction;
  }
  if (out_era != NULL) {
    *out_era = (int32_t)era;
  }
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * Excel
 *--------------------------------------------------------------------------*/

/** Split a serial into its whole day and its fraction of a day. */
static GCHRON_Result serial_to_civil(double serial, int64_t epoch_day,
    GCHRON_DateTime * out) {
  double whole;
  double fraction;
  int64_t day;
  int64_t nanos;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!(serial > -1e12 && serial < 1e12)) {
    /*
     * Written as the negation of "inside" rather than as "outside", because
     * NaN fails every comparison: `!(in range)` refuses it and the tidier
     * `serial <= -1e12 || serial >= 1e12` accepts it. What follows is
     * `(int64_t)` of this value, which for NaN or anything too large is
     * undefined behaviour rather than a wrong answer - so this line is the
     * whole of the defence, and its shape is the load-bearing part.
     */
    return GCHRON_ERR_RANGE;
  }
  whole = floor(serial);
  fraction = serial - whole;
  if (!gchron_add_i64((int64_t)whole, epoch_day, &day)) {
    return GCHRON_ERR_RANGE;
  }
  /* Rounded to the nearest nanosecond, because a serial is a `double` and
   * 0.5 of a day is not exactly 43200 seconds in binary. */
  nanos = (int64_t)(fraction * 86400.0 * 1e9 + 0.5);
  if (nanos >= GCHRON_SECONDS_PER_DAY * GCHRON_NANOS_PER_SECOND) {
    nanos = GCHRON_SECONDS_PER_DAY * GCHRON_NANOS_PER_SECOND - 1;
  }
  if (gchron_date_from_epoch_day(day, &out->date) != GCHRON_OK) {
    return GCHRON_ERR_RANGE;
  }
  return gchron_time_from_nanos_of_day(nanos, &out->time);
}

GCHRON_Result gchron_interop_from_excel_1900(double serial,
    GCHRON_DateTime * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!(serial >= 1.0)) {
    /* Negated for NaN, as serial_to_civil explains. INVALID rather than
     * RANGE: a serial below the epoch is a wrong argument, not an
     * unrepresentable one - and either way it is refused before the cast. */
    return GCHRON_ERR_INVALID;
  }
  if (serial >= 60.0 && serial < 61.0) {
    /*
     * Serial 60 is Excel's "1900-02-29", a day that did not occur: Excel
     * believes 1900 was a leap year, which is a Lotus 1-2-3 compatibility
     * bug it has carried since 1985. Refused rather than mapped onto some
     * real day, because there is no real day it means.
     */
    return GCHRON_ERR_INVALID;
  }
  /* Serials at or below 59 predate the phantom day and need no offset;
   * 61 and above are one too high because of it. EXCEL_1900_EPOCH_DAY is
   * already the 61-and-above anchor. */
  return serial_to_civil(serial < 60.0 ? serial + 1.0 : serial,
      EXCEL_1900_EPOCH_DAY, out);
}

GCHRON_Result gchron_interop_to_excel_1900(const GCHRON_DateTime * dt,
    double * out) {
  int64_t day;
  int64_t nanos;

  if (out == NULL || !gchron_datetime_is_valid(dt)) {
    return GCHRON_ERR_INVALID;
  }
  if (gchron_date_to_epoch_day(&dt->date, &day) != GCHRON_OK
      || gchron_time_to_nanos_of_day(&dt->time, &nanos) != GCHRON_OK) {
    return GCHRON_ERR_INVALID;
  }
  if (day < EXCEL_1900_EPOCH_DAY + 61) {
    /*
     * Before 1900-03-01. Those dates fall on the wrong side of Excel's
     * phantom leap day, where the two numberings differ by one and no single
     * answer is right. GCHRON_ERR_UNSUPPORTED rather than an off-by-one
     * nobody would notice.
     */
    return GCHRON_ERR_UNSUPPORTED;
  }
  *out = (double)(day - EXCEL_1900_EPOCH_DAY)
      + (double)nanos / (86400.0 * 1e9);
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_from_excel_1904(double serial,
    GCHRON_DateTime * out) {
  if (!(serial >= 0.0)) {
    /* Negated for NaN, as serial_to_civil explains. INVALID rather than
     * RANGE: a serial below the epoch is a wrong argument, not an
     * unrepresentable one - and either way it is refused before the cast. */
    return GCHRON_ERR_INVALID;
  }
  return serial_to_civil(serial, EXCEL_1904_EPOCH_DAY, out);
}

GCHRON_Result gchron_interop_to_excel_1904(const GCHRON_DateTime * dt,
    double * out) {
  int64_t day;
  int64_t nanos;

  if (out == NULL || !gchron_datetime_is_valid(dt)) {
    return GCHRON_ERR_INVALID;
  }
  if (gchron_date_to_epoch_day(&dt->date, &day) != GCHRON_OK
      || gchron_time_to_nanos_of_day(&dt->time, &nanos) != GCHRON_OK) {
    return GCHRON_ERR_INVALID;
  }
  *out = (double)(day - EXCEL_1904_EPOCH_DAY)
      + (double)nanos / (86400.0 * 1e9);
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * File formats
 *--------------------------------------------------------------------------*/

GCHRON_Result gchron_interop_from_dos(uint16_t date, uint16_t time,
    GCHRON_DateTime * out) {
  int year = DOS_EPOCH_YEAR + ((date >> 9) & 0x7F);
  int month = (date >> 5) & 0x0F;
  int day = date & 0x1F;
  int hour = (time >> 11) & 0x1F;
  int minute = (time >> 5) & 0x3F;
  int second = (time & 0x1F) * 2;
  GCHRON_Result result;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_date_create((int32_t)year, month, day, &out->date);
  if (result != GCHRON_OK) {
    return result;
  }
  return gchron_time_create(hour, minute, second, 0, &out->time);
}

GCHRON_Result gchron_interop_to_dos(const GCHRON_DateTime * dt,
    uint16_t * out_date, uint16_t * out_time) {
  int year_offset;

  if (!gchron_datetime_is_valid(dt)) {
    return GCHRON_ERR_INVALID;
  }
  year_offset = (int)((int64_t)dt->date.year - DOS_EPOCH_YEAR);
  if (year_offset < 0 || year_offset > 127) {
    return GCHRON_ERR_RANGE;
  }
  if (out_date != NULL) {
    *out_date = (uint16_t)((year_offset << 9) | (dt->date.month << 5)
        | dt->date.day);
  }
  if (out_time != NULL) {
    /* Truncated, not rounded: rounding 23:59:59 up would carry into the next
     * day, and the date word has already been written. */
    *out_time = (uint16_t)((dt->time.hour << 11) | (dt->time.minute << 5)
        | (dt->time.second / 2));
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_from_gzip_mtime(uint32_t value,
    GCHRON_Instant * out_instant, bool * out_present) {
  if (value == 0) {
    /*
     * RFC 1952: zero means "no time available", not the epoch. A decompressor
     * that stamped 1970-01-01 on a file because the archive carried no time
     * is the defect this refuses to allow by accident - a caller who does not
     * ask for `out_present` gets an error rather than a wrong date.
     */
    if (out_present != NULL) {
      *out_present = false;
      return GCHRON_OK;
    }
    return GCHRON_ERR_UNSUPPORTED;
  }
  if (out_instant == NULL) {
    return GCHRON_ERR_INVALID;
  }
  out_instant->sec = (int64_t)value;
  out_instant->nsec = 0;
  if (out_present != NULL) {
    *out_present = true;
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_to_gzip_mtime(const GCHRON_Instant * i,
    uint32_t * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (i == NULL) {
    *out = 0; /* "not available" */
    return GCHRON_OK;
  }
  if (!gchron_instant_is_valid(i)) {
    return GCHRON_ERR_INVALID;
  }
  if (i->sec <= 0 || i->sec > (int64_t)UINT32_MAX) {
    /* Zero is reserved for "not available", so an instant *at* the epoch
     * cannot be written - which is a real limit of the format rather than of
     * this function. */
    return GCHRON_ERR_RANGE;
  }
  *out = (uint32_t)i->sec;
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_from_png_time(const uint8_t bytes[7],
    GCHRON_DateTime * out) {
  int year;
  GCHRON_Result result;

  if (bytes == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  /* Byte by byte with an explicit shift: the year is two bytes big-endian,
   * and CONVENTIONS.md section 8 forbids reading a format through a struct
   * overlay. */
  year = ((int)bytes[0] << 8) | (int)bytes[1];
  result = gchron_date_create((int32_t)year, bytes[2], bytes[3], &out->date);
  if (result != GCHRON_OK) {
    return result;
  }
  return gchron_time_create(bytes[4], bytes[5], bytes[6], 0, &out->time);
}

GCHRON_Result gchron_interop_to_png_time(const GCHRON_DateTime * dt,
    uint8_t out[7]) {
  if (out == NULL || !gchron_datetime_is_valid(dt)) {
    return GCHRON_ERR_INVALID;
  }
  if (dt->date.year < 0 || dt->date.year > 65535) {
    return GCHRON_ERR_RANGE;
  }
  out[0] = (uint8_t)((dt->date.year >> 8) & 0xFF);
  out[1] = (uint8_t)(dt->date.year & 0xFF);
  out[2] = dt->date.month;
  out[3] = dt->date.day;
  out[4] = dt->time.hour;
  out[5] = dt->time.minute;
  out[6] = dt->time.second;
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_from_exif(const char * text, size_t len,
    GCHRON_DateTime * out) {
  int fields[6];
  size_t i;
  bool blank = true;
  static const size_t POSITIONS[6] = { 0, 5, 8, 11, 14, 17 };
  static const char SEPARATORS[5] = { ':', ':', ' ', ':', ':' };
  static const size_t SEPARATOR_AT[5] = { 4, 7, 10, 13, 16 };

  if (text == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (len != 19 && !(len == 20 && text[19] == '\0')) {
    return GCHRON_ERR_FORMAT;
  }

  for (i = 0; i < 19; ++i) {
    if (text[i] != ' ' && text[i] != '\0' && text[i] != ':') {
      blank = false;
    }
  }
  if (blank) {
    /* Many cameras write all spaces or all NULs for "unknown". That is not a
     * date, and reporting it as one is how a photo ends up filed under the
     * year 0. */
    return GCHRON_ERR_UNSUPPORTED;
  }

  for (i = 0; i < 5; ++i) {
    if (text[SEPARATOR_AT[i]] != SEPARATORS[i]) {
      /* The colons in the *date* are the thing everybody's first EXIF parser
       * gets wrong: `YYYY:MM:DD`, not `YYYY-MM-DD`. */
      return GCHRON_ERR_FORMAT;
    }
  }
  for (i = 0; i < 6; ++i) {
    size_t at = POSITIONS[i];
    size_t width = (i == 0) ? 4 : 2;
    size_t digit;
    int value = 0;
    for (digit = 0; digit < width; ++digit) {
      char c = text[at + digit];
      if (c < '0' || c > '9') {
        return GCHRON_ERR_FORMAT;
      }
      value = value * 10 + (c - '0');
    }
    fields[i] = value;
  }

  if (gchron_date_create((int32_t)fields[0], fields[1], fields[2], &out->date)
      != GCHRON_OK) {
    return GCHRON_ERR_FORMAT;
  }
  if (gchron_time_create(fields[3], fields[4], fields[5], 0, &out->time)
      != GCHRON_OK) {
    return GCHRON_ERR_FORMAT;
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_to_exif(const GCHRON_DateTime * dt,
    char out[GCHRON_EXIF_DATETIME_BYTES]) {
  if (out == NULL || !gchron_datetime_is_valid(dt)) {
    return GCHRON_ERR_INVALID;
  }
  if (dt->date.year < 0 || dt->date.year > 9999) {
    return GCHRON_ERR_RANGE;
  }
  /*
   * Written field by field rather than through one format string. The year is
   * an `int32_t` the compiler cannot narrow from the range check above, so
   * `%04d` might in principle write ten characters and truncate the rest -
   * and a truncated EXIF field is a wrong date rather than a short one.
   */
  {
    unsigned year = (unsigned)dt->date.year;
    out[0] = (char)('0' + (year / 1000) % 10);
    out[1] = (char)('0' + (year / 100) % 10);
    out[2] = (char)('0' + (year / 10) % 10);
    out[3] = (char)('0' + year % 10);
    out[4] = ':';
    out[5] = (char)('0' + dt->date.month / 10);
    out[6] = (char)('0' + dt->date.month % 10);
    out[7] = ':';
    out[8] = (char)('0' + dt->date.day / 10);
    out[9] = (char)('0' + dt->date.day % 10);
    out[10] = ' ';
    out[11] = (char)('0' + dt->time.hour / 10);
    out[12] = (char)('0' + dt->time.hour % 10);
    out[13] = ':';
    out[14] = (char)('0' + dt->time.minute / 10);
    out[15] = (char)('0' + dt->time.minute % 10);
    out[16] = ':';
    out[17] = (char)('0' + dt->time.second / 10);
    out[18] = (char)('0' + dt->time.second % 10);
    out[19] = '\0';
  }
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * Other epochs
 *--------------------------------------------------------------------------*/

GCHRON_Result gchron_interop_from_cocoa(double value, GCHRON_Instant * out) {
  double whole;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!(value > -1e17 && value < 1e17)) {
    /* The negation is deliberate and refuses NaN; see serial_to_civil. */
    return GCHRON_ERR_RANGE;
  }
  whole = floor(value);
  return gchron_instant_normalize((int64_t)whole + COCOA_EPOCH_SECONDS,
      (int64_t)((value - whole) * 1e9 + 0.5), out);
}

GCHRON_Result gchron_interop_to_cocoa(const GCHRON_Instant * i, double * out) {
  if (out == NULL || !gchron_instant_is_valid(i)) {
    return GCHRON_ERR_INVALID;
  }
  /* Lossy in the same way every `double` of seconds is; see
   * gchron_instant_as_double(), which says by how much. */
  *out = (double)(i->sec - COCOA_EPOCH_SECONDS) + (double)i->nsec / 1e9;
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_from_hfs_plus(uint32_t value,
    GCHRON_Instant * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  out->sec = (int64_t)value + EPOCH_1904_SECONDS;
  out->nsec = 0;
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_to_hfs_plus(const GCHRON_Instant * i,
    uint32_t * out) {
  int64_t since_1904;

  if (out == NULL || !gchron_instant_is_valid(i)) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_sub_i64(i->sec, EPOCH_1904_SECONDS, &since_1904)) {
    return GCHRON_ERR_RANGE;
  }
  if (since_1904 < 0 || since_1904 > (int64_t)UINT32_MAX) {
    return GCHRON_ERR_RANGE;
  }
  *out = (uint32_t)since_1904;
  return GCHRON_OK;
}

GCHRON_Result gchron_interop_from_mjd(double value, GCHRON_Instant * out) {
  double whole;
  int64_t day;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!(value > -1e12 && value < 1e12)) {
    /* The negation is deliberate and refuses NaN; see serial_to_civil. */
    return GCHRON_ERR_RANGE;
  }
  whole = floor(value);
  if (!gchron_add_i64((int64_t)whole, MJD_EPOCH_DAY, &day)) {
    return GCHRON_ERR_RANGE;
  }
  return gchron_instant_normalize(day * GCHRON_SECONDS_PER_DAY,
      (int64_t)((value - whole) * 86400.0 * 1e9 + 0.5), out);
}

GCHRON_Result gchron_interop_to_mjd(const GCHRON_Instant * i, double * out) {
  int64_t day = 0;
  int64_t nanos = 0;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (gchron_instant_to_epoch_day(i, &day, &nanos) != GCHRON_OK) {
    return GCHRON_ERR_INVALID;
  }
  *out = (double)(day - MJD_EPOCH_DAY) + (double)nanos / (86400.0 * 1e9);
  return GCHRON_OK;
}
