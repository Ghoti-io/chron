/**
 * @file
 *
 * The calendar vtable, and the two calendars with closed-form algorithms:
 * proleptic Gregorian and proleptic Julian.
 *
 * Reference: Edward M. Reingold and Nachum Dershowitz, *Calendrical
 * Calculations: The Ultimate Edition* (2018), for the Julian algorithm.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/calendar.h>
#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <stdio.h>
#include <string.h>

#include "../core/core_internal.h"
#include "civil_internal.h"

/*--------------------------------------------------------------------------*
 * Gregorian
 *--------------------------------------------------------------------------*/

static GCHRON_Result gregorian_from_epoch_day(const GCHRON_Calendar * self,
    int64_t epoch_day, GCHRON_Date * out) {
  (void)self;
  return gchron_date_from_epoch_day(epoch_day, out);
}

static GCHRON_Result gregorian_to_epoch_day(const GCHRON_Calendar * self,
    const GCHRON_Date * date, int64_t * out) {
  (void)self;
  return gchron_date_to_epoch_day(date, out);
}

static int gregorian_months_in_year(const GCHRON_Calendar * self,
    int32_t year) {
  (void)self;
  (void)year;
  return 12;
}

static int gregorian_days_in_month(const GCHRON_Calendar * self, int32_t year,
    int month) {
  (void)self;
  if (month < 1 || month > 12) {
    return 0;
  }
  return gchron_gregory_days_in_month(year, month);
}

static int gregorian_days_in_year(const GCHRON_Calendar * self, int32_t year) {
  (void)self;
  return gchron_gregory_is_leap(year) ? 366 : 365;
}

static bool gregorian_is_leap_year(const GCHRON_Calendar * self,
    int32_t year) {
  (void)self;
  return gchron_gregory_is_leap(year);
}

/*
 * 1970-01-01 is a Thursday, and the ISO week runs Monday to Sunday, so the
 * Monday three days earlier is day 1 of a week. Every calendar here that has
 * a seven-day week anchored the way the real ones are shares this, which is
 * what makes gchron_calendar_day_of_week() agree with ISO 8601's numbering.
 */
#define GREGORIAN_WEEK_EPOCH INT64_C(-3)

static const GCHRON_Calendar GREGORIAN = {
  "gregory",
  gregorian_from_epoch_day,
  gregorian_to_epoch_day,
  gregorian_months_in_year,
  gregorian_days_in_month,
  gregorian_days_in_year,
  gregorian_is_leap_year,
  7,
  GREGORIAN_WEEK_EPOCH,
  NULL
};

const GCHRON_Calendar * gchron_calendar_gregorian(void) {
  return &GREGORIAN;
}

/*--------------------------------------------------------------------------*
 * Julian
 *--------------------------------------------------------------------------*/

/**
 * Every fourth year, with no century rule.
 *
 * A floor modulo rather than `%`, so that year -1 is not a leap year and year
 * -4 is - the Julian rule applied to astronomical years, which is what a
 * proleptic calendar means (design.md section 3.2).
 */
static bool julian_is_leap(int64_t year) {
  return gchron_floor_mod(year, 4) == 0;
}

/** The length of a Julian month. */
static int julian_month_days(int64_t year, int month) {
  static const int LENGTHS[13] = {
    0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };
  if (month < 1 || month > 12) {
    return 0;
  }
  if (month == 2 && julian_is_leap(year)) {
    return 29;
  }
  return LENGTHS[month];
}

/**
 * The epoch day of a Julian date.
 *
 * Reingold and Dershowitz's `fixed-from-julian`, rewritten to land on this
 * library's epoch rather than on Rata Die, and with floor division throughout
 * so that it holds for astronomical years at or below zero.
 *
 * Julian 0001-01-01 is Rata Die -1, which is epoch day -719164 - two days
 * before Gregorian 0001-01-01. Everything below is that anchor plus the
 * 365.25-day year.
 */
static int64_t julian_to_epoch_day(int64_t year, int month, int day) {
  /*
   * The year count is shifted so that March begins it: that puts the leap day
   * at the end, where a single floor division handles it, and is the same
   * trick Hinnant's Gregorian algorithm uses.
   */
  int64_t shifted_year = year - (month <= 2 ? 1 : 0);
  int64_t shifted_month = month + (month <= 2 ? 9 : -3); /* March is 0 */
  int64_t days = gchron_floor_div(shifted_year, 4) * 1461
      + gchron_floor_mod(shifted_year, 4) * 365;
  days += (153 * shifted_month + 2) / 5;
  days += day - 1;
  /*
   * The constant lands year 0, March 1 on its epoch day. Derived once, and
   * asserted against an outside oracle in tests/unit/test_calendar.cpp rather
   * than trusted: a wrong epoch here is a silent two-day error that every
   * self-consistent round trip would still pass.
   */
  return days - INT64_C(719470);
}

/** The Julian label of an epoch day: the exact inverse of the above. */
static void julian_from_epoch_day(int64_t epoch_day, int64_t * out_year,
    int * out_month, int * out_day) {
  int64_t days = epoch_day + INT64_C(719470);
  int64_t quad = gchron_floor_div(days, 1461);
  int64_t within = gchron_floor_mod(days, 1461);
  int64_t year_in_quad = within / 365;
  int64_t day_of_year;
  int64_t month_index;
  int64_t year;

  if (year_in_quad == 4) {
    /* The last day of a leap year lands one past the end of the fourth
     * 365-day block; it belongs to that year, not to the next quad. */
    year_in_quad = 3;
  }
  day_of_year = within - year_in_quad * 365;
  year = quad * 4 + year_in_quad;

  month_index = (5 * day_of_year + 2) / 153;       /* 0 = March */
  *out_day = (int)(day_of_year - (153 * month_index + 2) / 5 + 1);
  *out_month = (int)(month_index + (month_index < 10 ? 3 : -9));
  *out_year = year + (*out_month <= 2 ? 1 : 0);
}

static GCHRON_Result julian_from_epoch_day_fn(const GCHRON_Calendar * self,
    int64_t epoch_day, GCHRON_Date * out) {
  int64_t year;
  int month;
  int day;

  (void)self;
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  /*
   * Bounded before the arithmetic, like the Gregorian conversion. The Julian
   * year is a little longer than the Gregorian one, so its epoch-day range
   * for the same years is slightly wider; the Gregorian bounds are used as a
   * conservative outer limit and the year is checked after.
   */
  if (epoch_day < GCHRON_EPOCH_DAY_MIN - INT64_C(8000000)
      || epoch_day > GCHRON_EPOCH_DAY_MAX + INT64_C(8000000)) {
    return GCHRON_ERR_RANGE;
  }
  julian_from_epoch_day(epoch_day, &year, &month, &day);
  if (year < GCHRON_YEAR_MIN || year > GCHRON_YEAR_MAX) {
    return GCHRON_ERR_RANGE;
  }
  out->year = (int32_t)year;
  out->month = (uint8_t)month;
  out->day = (uint8_t)day;
  return GCHRON_OK;
}

static GCHRON_Result julian_to_epoch_day_fn(const GCHRON_Calendar * self,
    const GCHRON_Date * date, int64_t * out) {
  (void)self;
  if (date == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (date->year < GCHRON_YEAR_MIN || date->year > GCHRON_YEAR_MAX) {
    return GCHRON_ERR_RANGE;
  }
  if (date->month < 1 || date->month > 12 || date->day < 1
      || date->day > julian_month_days(date->year, date->month)) {
    return GCHRON_ERR_INVALID;
  }
  *out = julian_to_epoch_day(date->year, date->month, date->day);
  return GCHRON_OK;
}

static int julian_months_in_year(const GCHRON_Calendar * self, int32_t year) {
  (void)self;
  (void)year;
  return 12;
}

static int julian_days_in_month_fn(const GCHRON_Calendar * self, int32_t year,
    int month) {
  (void)self;
  return julian_month_days(year, month);
}

static int julian_days_in_year_fn(const GCHRON_Calendar * self, int32_t year) {
  (void)self;
  return julian_is_leap(year) ? 366 : 365;
}

static bool julian_is_leap_year_fn(const GCHRON_Calendar * self,
    int32_t year) {
  (void)self;
  return julian_is_leap(year);
}

static const GCHRON_Calendar JULIAN = {
  "julian",
  julian_from_epoch_day_fn,
  julian_to_epoch_day_fn,
  julian_months_in_year,
  julian_days_in_month_fn,
  julian_days_in_year_fn,
  julian_is_leap_year_fn,
  7,
  /* The week does not care which calendar labels the day, so the anchor is
   * the same one: the days of the week ran on through the reform, which is
   * the one thing about October 1582 that did not change. */
  GREGORIAN_WEEK_EPOCH,
  NULL
};

const GCHRON_Calendar * gchron_calendar_julian(void) {
  return &JULIAN;
}

/*--------------------------------------------------------------------------*
 * Using a calendar
 *--------------------------------------------------------------------------*/

/** Resolve a NULL calendar to the Gregorian one. */
static const GCHRON_Calendar * effective(const GCHRON_Calendar * calendar) {
  return calendar != NULL ? calendar : &GREGORIAN;
}

const char * gchron_calendar_id(const GCHRON_Calendar * calendar) {
  return effective(calendar)->id;
}

GCHRON_Result gchron_calendar_from_epoch_day(const GCHRON_Calendar * calendar,
    int64_t epoch_day, GCHRON_Date * out) {
  const GCHRON_Calendar * cal = effective(calendar);
  if (out == NULL || cal->from_epoch_day == NULL) {
    return GCHRON_ERR_INVALID;
  }
  return cal->from_epoch_day(cal, epoch_day, out);
}

GCHRON_Result gchron_calendar_to_epoch_day(const GCHRON_Calendar * calendar,
    const GCHRON_Date * date, int64_t * out) {
  const GCHRON_Calendar * cal = effective(calendar);
  if (date == NULL || out == NULL || cal->to_epoch_day == NULL) {
    return GCHRON_ERR_INVALID;
  }
  return cal->to_epoch_day(cal, date, out);
}

bool gchron_calendar_date_is_valid(const GCHRON_Calendar * calendar,
    const GCHRON_Date * date) {
  int64_t ignored;
  if (date == NULL) {
    return false;
  }
  return gchron_calendar_to_epoch_day(calendar, date, &ignored) == GCHRON_OK;
}

/** Whether a year is one this library will represent. */
static bool year_ok(int32_t year) {
  return year >= GCHRON_YEAR_MIN && year <= GCHRON_YEAR_MAX;
}

GCHRON_Result gchron_calendar_months_in_year(const GCHRON_Calendar * calendar,
    int32_t year, int * out) {
  const GCHRON_Calendar * cal = effective(calendar);
  if (out == NULL || cal->months_in_year == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!year_ok(year)) {
    return GCHRON_ERR_RANGE;
  }
  *out = cal->months_in_year(cal, year);
  return GCHRON_OK;
}

GCHRON_Result gchron_calendar_days_in_month(const GCHRON_Calendar * calendar,
    int32_t year, int month, int * out) {
  const GCHRON_Calendar * cal = effective(calendar);
  int length;

  if (out == NULL || cal->days_in_month == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!year_ok(year)) {
    return GCHRON_ERR_RANGE;
  }
  length = cal->days_in_month(cal, year, month);
  if (length <= 0) {
    return GCHRON_ERR_INVALID;
  }
  *out = length;
  return GCHRON_OK;
}

GCHRON_Result gchron_calendar_days_in_year(const GCHRON_Calendar * calendar,
    int32_t year, int * out) {
  const GCHRON_Calendar * cal = effective(calendar);
  if (out == NULL || cal->days_in_year == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!year_ok(year)) {
    return GCHRON_ERR_RANGE;
  }
  *out = cal->days_in_year(cal, year);
  return GCHRON_OK;
}

GCHRON_Result gchron_calendar_is_leap_year(const GCHRON_Calendar * calendar,
    int32_t year, bool * out) {
  const GCHRON_Calendar * cal = effective(calendar);
  if (out == NULL || cal->is_leap_year == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!year_ok(year)) {
    return GCHRON_ERR_RANGE;
  }
  *out = cal->is_leap_year(cal, year);
  return GCHRON_OK;
}

GCHRON_Result gchron_calendar_day_of_week(const GCHRON_Calendar * calendar,
    const GCHRON_Date * date, int * out) {
  const GCHRON_Calendar * cal = effective(calendar);
  int64_t epoch_day;
  GCHRON_Result result;

  if (out == NULL || cal->days_in_week < 1) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_calendar_to_epoch_day(cal, date, &epoch_day);
  if (result != GCHRON_OK) {
    return result;
  }
  *out = (int)gchron_floor_mod(epoch_day - cal->week_epoch_day,
      cal->days_in_week) + 1;
  return GCHRON_OK;
}

GCHRON_Result gchron_calendar_day_of_year(const GCHRON_Calendar * calendar,
    const GCHRON_Date * date, int * out) {
  const GCHRON_Calendar * cal = effective(calendar);
  GCHRON_Date first;
  int64_t here;
  int64_t start;
  GCHRON_Result result;

  if (date == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_calendar_to_epoch_day(cal, date, &here);
  if (result != GCHRON_OK) {
    return result;
  }
  first.year = date->year;
  first.month = 1;
  first.day = 1;
  result = gchron_calendar_to_epoch_day(cal, &first, &start);
  if (result != GCHRON_OK) {
    return result;
  }
  *out = (int)(here - start + 1);
  return GCHRON_OK;
}

GCHRON_Result gchron_calendar_convert(const GCHRON_Calendar * from,
    const GCHRON_Date * date, const GCHRON_Calendar * to, GCHRON_Date * out) {
  int64_t epoch_day;
  GCHRON_Result result;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  /* By way of the epoch day, which is why there is one conversion to write
   * rather than one per pair of calendars (design.md section 3.3). */
  result = gchron_calendar_to_epoch_day(from, date, &epoch_day);
  if (result != GCHRON_OK) {
    return result;
  }
  return gchron_calendar_from_epoch_day(to, epoch_day, out);
}

void gchron_calendar_dump(const GCHRON_Calendar * calendar, FILE * stream) {
  const GCHRON_Calendar * cal = effective(calendar);
  if (stream == NULL) {
    return;
  }
  fprintf(stream,
      "GCHRON_Calendar(%s, %d-day week anchored at epoch day %lld)\n",
      cal->id, cal->days_in_week, (long long)cal->week_epoch_day);
}
