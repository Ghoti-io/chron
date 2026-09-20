/**
 * @file
 *
 * Weeks, ordinal days, and the nth weekday of a month.
 *
 * Reference: ISO 8601-1:2019 sections 4.1.3 (ordinal date) and 4.1.4 (week
 * date). Week 1 is the week holding 4 January, equivalently the week holding
 * the year's first Thursday.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>

#include "../core/core_internal.h"
#include "civil_internal.h"

/**
 * The ISO day of the week of an epoch day: Monday 1 .. Sunday 7.
 *
 * Epoch day 0 is 1970-01-01, a Thursday, which is 4; the `+3` moves the count
 * so that the modulo lands Monday on 0. The floor modulo is what makes it
 * right before the epoch, where C's `%` would give a negative day.
 */
static int dow_of_epoch_day(int64_t epoch_day) {
  return (int)gchron_floor_mod(epoch_day + 3, 7) + 1;
}

/** How many ISO weeks a year has, with no range check. */
static int weeks_in_year_unchecked(int64_t year) {
  int64_t jan1 = gchron_gregory_to_epoch_day(year, 1, 1);
  int dow = dow_of_epoch_day(jan1);
  /*
   * A year has 53 weeks when it starts on a Thursday, or when it is a leap
   * year starting on a Wednesday - in both cases 4 January and 28 December
   * fall far enough apart to need one more.
   */
  if (dow == 4 || (dow == 3 && gchron_gregory_is_leap(year))) {
    return 53;
  }
  return 52;
}

GCHRON_Result gchron_date_day_of_week(const GCHRON_Date * date, int * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_date_is_valid(date)) {
    return GCHRON_ERR_INVALID;
  }
  *out = dow_of_epoch_day(
      gchron_gregory_to_epoch_day(date->year, date->month, date->day));
  return GCHRON_OK;
}

GCHRON_Result gchron_date_day_of_year(const GCHRON_Date * date, int * out) {
  int64_t jan1;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_date_is_valid(date)) {
    return GCHRON_ERR_INVALID;
  }
  jan1 = gchron_gregory_to_epoch_day(date->year, 1, 1);
  *out = (int)(gchron_gregory_to_epoch_day(date->year, date->month, date->day)
      - jan1 + 1);
  return GCHRON_OK;
}

GCHRON_Result gchron_date_from_ordinal(int32_t year, int day_of_year,
    GCHRON_Date * out) {
  int64_t epoch_day;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (year < GCHRON_YEAR_MIN || year > GCHRON_YEAR_MAX) {
    return GCHRON_ERR_RANGE;
  }
  if (day_of_year < 1
      || day_of_year > (gchron_gregory_is_leap(year) ? 366 : 365)) {
    return GCHRON_ERR_INVALID;
  }
  epoch_day = gchron_gregory_to_epoch_day(year, 1, 1) + day_of_year - 1;
  return gchron_date_from_epoch_day(epoch_day, out);
}

GCHRON_Result gchron_iso_weeks_in_year(int32_t week_year, int * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (week_year < GCHRON_YEAR_MIN || week_year > GCHRON_YEAR_MAX) {
    return GCHRON_ERR_RANGE;
  }
  *out = weeks_in_year_unchecked(week_year);
  return GCHRON_OK;
}

GCHRON_Result gchron_date_to_iso_week(const GCHRON_Date * date,
    GCHRON_IsoWeekDate * out) {
  int64_t epoch_day;
  int64_t jan1;
  int dow;
  int doy;
  int64_t week;
  int64_t week_year;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_date_is_valid(date)) {
    return GCHRON_ERR_INVALID;
  }
  epoch_day = gchron_gregory_to_epoch_day(date->year, date->month, date->day);
  jan1 = gchron_gregory_to_epoch_day(date->year, 1, 1);
  dow = dow_of_epoch_day(epoch_day);
  doy = (int)(epoch_day - jan1 + 1);

  /*
   * The week a date falls in, counted from the year's own 1 January. The
   * result can be 0 or one past the year's count, and those are the two cases
   * where the week-year is not the calendar year - which is the whole of
   * mistake M8, and the reason this type carries a separate week_year field.
   */
  week = (doy - dow + 10) / 7;
  week_year = date->year;
  if (week < 1) {
    week_year = (int64_t)date->year - 1;
    if (week_year < GCHRON_YEAR_MIN) {
      return GCHRON_ERR_RANGE;
    }
    week = weeks_in_year_unchecked(week_year);
  }
  else if (week > weeks_in_year_unchecked(date->year)) {
    week_year = (int64_t)date->year + 1;
    if (week_year > GCHRON_YEAR_MAX) {
      return GCHRON_ERR_RANGE;
    }
    week = 1;
  }

  out->week_year = (int32_t)week_year;
  out->week = (uint8_t)week;
  out->day = (uint8_t)dow;
  return GCHRON_OK;
}

GCHRON_Result gchron_date_from_iso_week(const GCHRON_IsoWeekDate * week,
    GCHRON_Date * out) {
  int64_t jan4;
  int64_t week1_monday;
  int64_t epoch_day;

  if (week == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (week->week_year < GCHRON_YEAR_MIN || week->week_year > GCHRON_YEAR_MAX) {
    return GCHRON_ERR_RANGE;
  }
  if (week->day < 1 || week->day > 7) {
    return GCHRON_ERR_INVALID;
  }
  if (week->week < 1 || week->week > weeks_in_year_unchecked(week->week_year)) {
    /*
     * Week 53 of a 52-week year is an error, not a quiet roll into week 1 of
     * the next: a caller who wrote it meant something, and guessing what is
     * how a week-number bug survives to the following December.
     */
    return GCHRON_ERR_INVALID;
  }

  jan4 = gchron_gregory_to_epoch_day(week->week_year, 1, 4);
  week1_monday = jan4 - (dow_of_epoch_day(jan4) - 1);
  epoch_day = week1_monday + ((int64_t)week->week - 1) * 7 + week->day - 1;
  return gchron_date_from_epoch_day(epoch_day, out);
}

GCHRON_Result gchron_date_nth_weekday(int32_t year, int month, int weekday,
    int nth, GCHRON_Date * out) {
  int64_t anchor;
  int64_t target;
  int delta;
  GCHRON_Result result;
  GCHRON_Date candidate;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (year < GCHRON_YEAR_MIN || year > GCHRON_YEAR_MAX) {
    return GCHRON_ERR_RANGE;
  }
  if (month < 1 || month > 12 || weekday < 1 || weekday > 7 || nth == 0
      || nth < -5 || nth > 5) {
    return GCHRON_ERR_INVALID;
  }

  if (nth > 0) {
    anchor = gchron_gregory_to_epoch_day(year, month, 1);
    delta = (weekday - dow_of_epoch_day(anchor) + 7) % 7;
    target = anchor + delta + (int64_t)(nth - 1) * 7;
  }
  else {
    anchor = gchron_gregory_to_epoch_day(year, month,
        gchron_gregory_days_in_month(year, month));
    delta = (dow_of_epoch_day(anchor) - weekday + 7) % 7;
    target = anchor - delta + (int64_t)(nth + 1) * 7;
  }

  result = gchron_date_from_epoch_day(target, &candidate);
  if (result != GCHRON_OK) {
    return result;
  }
  if (candidate.year != year || candidate.month != month) {
    /*
     * There is no fifth Monday in most Februaries. GCHRON_ERR_RANGE rather
     * than the fourth: a caller computing "the fifth Sunday" for a recurrence
     * needs to know the month has none.
     */
    return GCHRON_ERR_RANGE;
  }
  *out = candidate;
  return GCHRON_OK;
}
