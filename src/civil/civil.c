/**
 * @file
 *
 * Civil dates and times: construction, validity, the epoch day, comparison.
 *
 * Reference: ISO 8601-1:2019.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <stdio.h>

#include "../core/core_internal.h"
#include "civil_internal.h"

/** Whether a year is one this library will represent. */
static bool year_in_range(int64_t year) {
  return year >= GCHRON_YEAR_MIN && year <= GCHRON_YEAR_MAX;
}

/*--------------------------------------------------------------------------*
 * Construction and validity
 *--------------------------------------------------------------------------*/

GCHRON_Result gchron_date_create(int32_t year, int month, int day,
    GCHRON_Date * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!year_in_range(year)) {
    return GCHRON_ERR_RANGE;
  }
  if (month < 1 || month > 12) {
    return GCHRON_ERR_INVALID;
  }
  if (day < 1 || day > gchron_gregory_days_in_month(year, month)) {
    return GCHRON_ERR_INVALID;
  }
  out->year = year;
  out->month = (uint8_t)month;
  out->day = (uint8_t)day;
  return GCHRON_OK;
}

bool gchron_date_is_valid(const GCHRON_Date * date) {
  if (date == NULL) {
    return false;
  }
  if (!year_in_range(date->year)) {
    return false;
  }
  if (date->month < 1 || date->month > 12) {
    return false;
  }
  return date->day >= 1
      && date->day <= gchron_gregory_days_in_month(date->year, date->month);
}

GCHRON_Result gchron_time_create(int hour, int minute, int second,
    int32_t nsec, GCHRON_Time * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0
      || second > 59 || nsec < 0 || nsec >= GCHRON_NANOS_PER_SECOND) {
    return GCHRON_ERR_INVALID;
  }
  out->hour = (uint8_t)hour;
  out->minute = (uint8_t)minute;
  out->second = (uint8_t)second;
  out->nsec = nsec;
  return GCHRON_OK;
}

bool gchron_time_is_valid(const GCHRON_Time * time) {
  if (time == NULL) {
    return false;
  }
  return time->hour <= 23 && time->minute <= 59 && time->second <= 59
      && time->nsec >= 0 && time->nsec < GCHRON_NANOS_PER_SECOND;
}

bool gchron_datetime_is_valid(const GCHRON_DateTime * dt) {
  if (dt == NULL) {
    return false;
  }
  return gchron_date_is_valid(&dt->date) && gchron_time_is_valid(&dt->time);
}

GCHRON_Result gchron_year_month_create(int32_t year, int month,
    GCHRON_YearMonth * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!year_in_range(year)) {
    return GCHRON_ERR_RANGE;
  }
  if (month < 1 || month > 12) {
    return GCHRON_ERR_INVALID;
  }
  out->year = year;
  out->month = (uint8_t)month;
  return GCHRON_OK;
}

GCHRON_Result gchron_month_day_create(int month, int day,
    GCHRON_MonthDay * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (month < 1 || month > 12) {
    return GCHRON_ERR_INVALID;
  }
  /*
   * A leap year, so that February 29 is a month-day even though it is not a
   * date in most years. That is the whole reason this type exists: a
   * recurrence keeps the label and asks each year whether it has one.
   */
  if (day < 1 || day > gchron_gregory_days_in_month(2000, month)) {
    return GCHRON_ERR_INVALID;
  }
  out->month = (uint8_t)month;
  out->day = (uint8_t)day;
  return GCHRON_OK;
}

GCHRON_Result gchron_month_day_in_year(const GCHRON_MonthDay * md,
    int32_t year, GCHRON_Date * out) {
  if (md == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (md->month < 1 || md->month > 12 || md->day < 1
      || md->day > gchron_gregory_days_in_month(2000, md->month)) {
    return GCHRON_ERR_INVALID;
  }
  if (!year_in_range(year)) {
    return GCHRON_ERR_RANGE;
  }
  if (md->day > gchron_gregory_days_in_month(year, md->month)) {
    /*
     * February 29 of a common year. GCHRON_ERR_RANGE rather than a clamp to
     * the 28th: the caller asked for a day that does not exist, and which day
     * they meant instead is their decision, not this function's.
     */
    return GCHRON_ERR_RANGE;
  }
  out->year = year;
  out->month = md->month;
  out->day = md->day;
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * Calendar facts
 *--------------------------------------------------------------------------*/

GCHRON_Result gchron_year_is_leap(int32_t year, bool * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!year_in_range(year)) {
    return GCHRON_ERR_RANGE;
  }
  *out = gchron_gregory_is_leap(year);
  return GCHRON_OK;
}

GCHRON_Result gchron_date_days_in_month(int32_t year, int month, int * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!year_in_range(year)) {
    return GCHRON_ERR_RANGE;
  }
  if (month < 1 || month > 12) {
    return GCHRON_ERR_INVALID;
  }
  *out = gchron_gregory_days_in_month(year, month);
  return GCHRON_OK;
}

GCHRON_Result gchron_date_days_in_year(int32_t year, int * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!year_in_range(year)) {
    return GCHRON_ERR_RANGE;
  }
  *out = gchron_gregory_is_leap(year) ? 366 : 365;
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * The epoch day
 *--------------------------------------------------------------------------*/

GCHRON_Result gchron_date_to_epoch_day(const GCHRON_Date * date,
    int64_t * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_date_is_valid(date)) {
    return GCHRON_ERR_INVALID;
  }
  *out = gchron_gregory_to_epoch_day(date->year, date->month, date->day);
  return GCHRON_OK;
}

GCHRON_Result gchron_date_from_epoch_day(int64_t epoch_day,
    GCHRON_Date * out) {
  int64_t year;
  int month;
  int day;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  /*
   * Bounded before the arithmetic, not after: `epoch_day + 719468` on a value
   * near INT64_MAX is the signed overflow this library promises not to have
   * (design.md, mistake M24).
   */
  if (epoch_day < GCHRON_EPOCH_DAY_MIN || epoch_day > GCHRON_EPOCH_DAY_MAX) {
    return GCHRON_ERR_RANGE;
  }
  gchron_gregory_from_epoch_day(epoch_day, &year, &month, &day);
  out->year = (int32_t)year;
  out->month = (uint8_t)month;
  out->day = (uint8_t)day;
  return GCHRON_OK;
}

GCHRON_Result gchron_epoch_day_to_jdn(int64_t epoch_day, int64_t * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_add_i64(epoch_day, GCHRON_JDN_OFFSET, out)) {
    return GCHRON_ERR_RANGE;
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_jdn_to_epoch_day(int64_t jdn, int64_t * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_sub_i64(jdn, GCHRON_JDN_OFFSET, out)) {
    return GCHRON_ERR_RANGE;
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_epoch_day_to_rd(int64_t epoch_day, int64_t * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_add_i64(epoch_day, GCHRON_RD_OFFSET, out)) {
    return GCHRON_ERR_RANGE;
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_rd_to_epoch_day(int64_t rd, int64_t * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_sub_i64(rd, GCHRON_RD_OFFSET, out)) {
    return GCHRON_ERR_RANGE;
  }
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * Arithmetic on days and on the time of day
 *--------------------------------------------------------------------------*/

GCHRON_Result gchron_date_add_days(const GCHRON_Date * date, int64_t days,
    GCHRON_Date * out) {
  int64_t epoch_day;
  int64_t sum;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_date_is_valid(date)) {
    return GCHRON_ERR_INVALID;
  }
  epoch_day = gchron_gregory_to_epoch_day(date->year, date->month, date->day);
  if (!gchron_add_i64(epoch_day, days, &sum)) {
    return GCHRON_ERR_RANGE;
  }
  return gchron_date_from_epoch_day(sum, out);
}

GCHRON_Result gchron_time_to_nanos_of_day(const GCHRON_Time * time,
    int64_t * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_time_is_valid(time)) {
    return GCHRON_ERR_INVALID;
  }
  *out = ((int64_t)time->hour * 3600 + (int64_t)time->minute * 60
             + (int64_t)time->second)
          * GCHRON_NANOS_PER_SECOND
      + time->nsec;
  return GCHRON_OK;
}

GCHRON_Result gchron_time_from_nanos_of_day(int64_t nanos, GCHRON_Time * out) {
  int64_t seconds;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (nanos < 0
      || nanos >= GCHRON_SECONDS_PER_DAY * GCHRON_NANOS_PER_SECOND) {
    /*
     * Not a wrap into the next day. The caller owns the date, and a function
     * that silently rolled one over is how "start of day" arithmetic loses a
     * day at a boundary.
     */
    return GCHRON_ERR_INVALID;
  }
  seconds = nanos / GCHRON_NANOS_PER_SECOND;
  out->nsec = (int32_t)(nanos % GCHRON_NANOS_PER_SECOND);
  out->hour = (uint8_t)(seconds / 3600);
  out->minute = (uint8_t)((seconds / 60) % 60);
  out->second = (uint8_t)(seconds % 60);
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * Comparison
 *--------------------------------------------------------------------------*/

/** Order two values the way a comparison function is expected to. */
static int cmp_i64(int64_t a, int64_t b) {
  if (a < b) {
    return -1;
  }
  return a > b ? 1 : 0;
}

/** NULL sorts before non-NULL, so that a comparator is a total order. */
static int cmp_null(const void * a, const void * b, int * out) {
  if (a == NULL || b == NULL) {
    *out = (a == b) ? 0 : (a == NULL ? -1 : 1);
    return 1;
  }
  return 0;
}

int gchron_date_compare(const GCHRON_Date * a, const GCHRON_Date * b) {
  int early;
  int c;

  if (cmp_null(a, b, &early)) {
    return early;
  }
  c = cmp_i64(a->year, b->year);
  if (c != 0) {
    return c;
  }
  c = cmp_i64(a->month, b->month);
  if (c != 0) {
    return c;
  }
  return cmp_i64(a->day, b->day);
}

int gchron_time_compare(const GCHRON_Time * a, const GCHRON_Time * b) {
  int early;
  int c;

  if (cmp_null(a, b, &early)) {
    return early;
  }
  c = cmp_i64(a->hour, b->hour);
  if (c != 0) {
    return c;
  }
  c = cmp_i64(a->minute, b->minute);
  if (c != 0) {
    return c;
  }
  c = cmp_i64(a->second, b->second);
  if (c != 0) {
    return c;
  }
  return cmp_i64(a->nsec, b->nsec);
}

int gchron_datetime_compare(const GCHRON_DateTime * a,
    const GCHRON_DateTime * b) {
  int early;
  int c;

  if (cmp_null(a, b, &early)) {
    return early;
  }
  c = gchron_date_compare(&a->date, &b->date);
  if (c != 0) {
    return c;
  }
  return gchron_time_compare(&a->time, &b->time);
}

int gchron_year_month_compare(const GCHRON_YearMonth * a,
    const GCHRON_YearMonth * b) {
  int early;
  int c;

  if (cmp_null(a, b, &early)) {
    return early;
  }
  c = cmp_i64(a->year, b->year);
  if (c != 0) {
    return c;
  }
  return cmp_i64(a->month, b->month);
}

int gchron_month_day_compare(const GCHRON_MonthDay * a,
    const GCHRON_MonthDay * b) {
  int early;
  int c;

  if (cmp_null(a, b, &early)) {
    return early;
  }
  c = cmp_i64(a->month, b->month);
  if (c != 0) {
    return c;
  }
  return cmp_i64(a->day, b->day);
}

/*--------------------------------------------------------------------------*
 * Debugging
 *--------------------------------------------------------------------------*/

void gchron_datetime_dump(const GCHRON_DateTime * dt, FILE * stream) {
  if (stream == NULL) {
    return;
  }
  if (dt == NULL) {
    fprintf(stream, "GCHRON_DateTime(NULL)\n");
    return;
  }
  fprintf(stream,
      "GCHRON_DateTime(%+011d-%02u-%02u %02u:%02u:%02u.%09d)%s\n",
      dt->date.year, (unsigned)dt->date.month, (unsigned)dt->date.day,
      (unsigned)dt->time.hour, (unsigned)dt->time.minute,
      (unsigned)dt->time.second, dt->time.nsec,
      gchron_datetime_is_valid(dt) ? "" : " [invalid]");
}
