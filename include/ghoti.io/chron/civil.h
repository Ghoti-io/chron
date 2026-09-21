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
 * Civil dates and times: the reading on a wall clock and a calendar, with no
 * time zone and no point on the timeline attached to it.
 *
 * Tier 0 (design.md section 3): needs nothing, allocates nothing, touches no
 * operating-system call. Everything here is proleptic Gregorian; the other
 * calendars arrive with calendar.h.
 *
 * Reference: ISO 8601-1:2019; Howard Hinnant, *chrono-Compatible Low-Level
 * Date Algorithms*, for `days_from_civil` and `civil_from_days`.
 */

#ifndef GHOTI_IO_GCHRON_CIVIL_H
#define GHOTI_IO_GCHRON_CIVIL_H

#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A calendar date.
 *
 * Years are astronomical: year 0 exists and is 1 BCE, year -1 is 2 BCE. "1
 * BCE" is a *presentation* of year 0 and is produced only by an era format
 * letter (design.md section 3.2).
 *
 * Months are 1..12 and days are 1..the length of that month, in this type and
 * in every other one here. Nothing in this library is zero-based or
 * 1900-based (design.md, mistake M7).
 */
typedef struct GCHRON_Date {
  int32_t year;  ///< GCHRON_YEAR_MIN..GCHRON_YEAR_MAX; 0 is 1 BCE.
  uint8_t month; ///< 1..12.
  uint8_t day;   ///< 1..gchron_date_days_in_month().
} GCHRON_Date;

/**
 * @brief A time of day.
 *
 * `second` is 0..59. A `:60` read from text becomes `:59` of the same minute
 * with the same fraction, and the fact that it was `:60` survives in
 * GCHRON_ParseInfo::leap_second (design.md section 5.1).
 */
typedef struct GCHRON_Time {
  uint8_t hour;   ///< 0..23.
  uint8_t minute; ///< 0..59.
  uint8_t second; ///< 0..59.
  int32_t nsec;   ///< 0..999999999.
} GCHRON_Time;

/** @brief A date and a time of day, with no zone and no offset. */
typedef struct GCHRON_DateTime {
  GCHRON_Date date; ///< The date part.
  GCHRON_Time time; ///< The time part.
} GCHRON_DateTime;

/** @brief A year and a month, for "the month of" questions. */
typedef struct GCHRON_YearMonth {
  int32_t year;  ///< GCHRON_YEAR_MIN..GCHRON_YEAR_MAX.
  uint8_t month; ///< 1..12.
} GCHRON_YearMonth;

/**
 * @brief A month and a day, with no year.
 *
 * This is the type a recurrence keeps: "the 29th of February" is a
 * GCHRON_MonthDay, and asking for it in a common year is an error rather than
 * a silent 28th (design.md section 3.1).
 */
typedef struct GCHRON_MonthDay {
  uint8_t month; ///< 1..12.
  uint8_t day;   ///< 1..the longest that month ever is (29 for February).
} GCHRON_MonthDay;

/**
 * @brief An ISO 8601 week date: `2026-W38-7`.
 *
 * The week-year is not the calendar year. 2027-01-01 is a Friday and belongs
 * to week 53 of week-year 2026; printing it with the calendar year is the
 * defect behind a New Year's Eve outage somewhere every few years (design.md,
 * mistake M8).
 */
typedef struct GCHRON_IsoWeekDate {
  int32_t week_year; ///< The year the *week* belongs to.
  uint8_t week;      ///< 1..53.
  uint8_t day;       ///< 1 = Monday .. 7 = Sunday.
} GCHRON_IsoWeekDate;

/** Monday, as gchron_date_day_of_week() numbers it. ISO 8601 section 3.1.1.7. */
#define GCHRON_MONDAY 1
/** Sunday, as gchron_date_day_of_week() numbers it. */
#define GCHRON_SUNDAY 7

/**
 * @brief The epoch day of 1970-01-01, which is 0 by definition.
 *
 * Present so that a reader of a conversion table has the anchor in front of
 * them; see gchron_date_to_epoch_day().
 */
#define GCHRON_EPOCH_DAY_UNIX INT64_C(0)

/** The Julian Day Number of 1970-01-01. design.md section 3.3. */
#define GCHRON_JDN_UNIX_EPOCH INT64_C(2440588)

/** The Rata Die of 1970-01-01. design.md section 3.3. */
#define GCHRON_RD_UNIX_EPOCH INT64_C(719163)

/*--------------------------------------------------------------------------*
 * Construction and validity
 *--------------------------------------------------------------------------*/

/**
 * @brief Build a date, checking that it exists.
 *
 * @param year Astronomical year, GCHRON_YEAR_MIN..GCHRON_YEAR_MAX.
 * @param month 1..12.
 * @param day 1..the length of that month in that year.
 * @param out Receives the date on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_RANGE when the year is outside the supported
 *   range; GCHRON_ERR_INVALID when the month or the day is not a real one, or
 *   @p out is NULL.
 */
GCHRON_API GCHRON_Result gchron_date_create(int32_t year, int month, int day,
    GCHRON_Date * out);

/**
 * @brief Whether a date exists.
 *
 * @param date The date. NULL is not valid.
 * @return `true` when every field is in range and the day exists in that
 *   month of that year.
 */
GCHRON_API bool gchron_date_is_valid(const GCHRON_Date * date);

/**
 * @brief Build a time of day, checking its fields.
 *
 * @param hour 0..23.
 * @param minute 0..59.
 * @param second 0..59. `60` is not accepted here; a leap second reaches this
 *   type through a parser and a GCHRON_Leap policy (design.md section 5.4).
 * @param nsec 0..999999999.
 * @param out Receives the time on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_time_create(int hour, int minute, int second,
    int32_t nsec, GCHRON_Time * out);

/**
 * @brief Whether a time of day is in range.
 *
 * @param time The time. NULL is not valid.
 * @return `true` when every field is in range.
 */
GCHRON_API bool gchron_time_is_valid(const GCHRON_Time * time);

/**
 * @brief Whether a date-time is a real date and an in-range time.
 *
 * @param dt The date-time. NULL is not valid.
 * @return `true` when both halves are valid.
 */
GCHRON_API bool gchron_datetime_is_valid(const GCHRON_DateTime * dt);

/**
 * @brief Build a year-month, checking its fields.
 *
 * @param year Astronomical year.
 * @param month 1..12.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_RANGE or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_year_month_create(int32_t year, int month,
    GCHRON_YearMonth * out);

/**
 * @brief Build a month-day, checking its fields.
 *
 * February 29 is accepted: a GCHRON_MonthDay is a recurrence, not a date, and
 * the year is where it can fail.
 *
 * @param month 1..12.
 * @param day 1..the longest that month ever is.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_month_day_create(int month, int day,
    GCHRON_MonthDay * out);

/**
 * @brief Place a month-day in a year.
 *
 * @param md The month and day.
 * @param year The year to place it in.
 * @param out Receives the date on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_RANGE when that day does not exist in that
 *   year - February 29 of a common year - which is an answer, not a clamp.
 */
GCHRON_API GCHRON_Result gchron_month_day_in_year(const GCHRON_MonthDay * md,
    int32_t year, GCHRON_Date * out);

/*--------------------------------------------------------------------------*
 * Calendar facts
 *--------------------------------------------------------------------------*/

/**
 * @brief Whether a year is a leap year in the proleptic Gregorian calendar.
 *
 * @param year The astronomical year.
 * @param out Receives the answer on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_RANGE when the year is unsupported.
 */
GCHRON_API GCHRON_Result gchron_year_is_leap(int32_t year, bool * out);

/**
 * @brief How many days a month has.
 *
 * @param year The astronomical year, which February needs.
 * @param month 1..12.
 * @param out Receives 28..31 on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_RANGE or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_date_days_in_month(int32_t year, int month,
    int * out);

/**
 * @brief How many days a year has: 365 or 366.
 *
 * @param year The astronomical year.
 * @param out Receives the answer on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_date_days_in_year(int32_t year, int * out);

/*--------------------------------------------------------------------------*
 * The epoch day, and the day counts the literature uses
 *--------------------------------------------------------------------------*/

/**
 * @brief Days since 1970-01-01, which is day 0.
 *
 * The epoch day is the currency between calendars, and between civil time and
 * instants (design.md section 3.3).
 *
 * @param date A valid date.
 * @param out Receives the count on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID when the date is not a real one.
 */
GCHRON_API GCHRON_Result gchron_date_to_epoch_day(const GCHRON_Date * date,
    int64_t * out);

/**
 * @brief The date an epoch day names.
 *
 * @param epoch_day Days since 1970-01-01.
 * @param out Receives the date on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_RANGE when that day falls outside the
 *   supported years.
 */
GCHRON_API GCHRON_Result gchron_date_from_epoch_day(int64_t epoch_day,
    GCHRON_Date * out);

/**
 * @brief The Julian Day Number of an epoch day.
 *
 * The JDN is the integer civil day, counted from -4713-11-24 proleptic
 * Gregorian. The astronomical Julian Date, which begins at noon and carries a
 * fraction, is not a type in this library (design.md section 3.3).
 *
 * @param epoch_day Days since 1970-01-01.
 * @param out Receives the JDN on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_RANGE on overflow.
 */
GCHRON_API GCHRON_Result gchron_epoch_day_to_jdn(int64_t epoch_day,
    int64_t * out);

/**
 * @brief The epoch day of a Julian Day Number.
 *
 * @param jdn The Julian Day Number.
 * @param out Receives the epoch day on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_RANGE on overflow.
 */
GCHRON_API GCHRON_Result gchron_jdn_to_epoch_day(int64_t jdn, int64_t * out);

/**
 * @brief The Rata Die of an epoch day: days from 0001-01-01, which is R.D. 1.
 *
 * @param epoch_day Days since 1970-01-01.
 * @param out Receives the R.D. on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_RANGE on overflow.
 */
GCHRON_API GCHRON_Result gchron_epoch_day_to_rd(int64_t epoch_day,
    int64_t * out);

/**
 * @brief The epoch day of a Rata Die.
 *
 * @param rd The Rata Die.
 * @param out Receives the epoch day on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_RANGE on overflow.
 */
GCHRON_API GCHRON_Result gchron_rd_to_epoch_day(int64_t rd, int64_t * out);

/*--------------------------------------------------------------------------*
 * Derived fields
 *--------------------------------------------------------------------------*/

/**
 * @brief The day of the week, ISO 8601 numbering: Monday is 1, Sunday is 7.
 *
 * One numbering, everywhere in this library. `%w`'s Sunday-is-zero and
 * `struct tm`'s `tm_wday` exist only as format letters and as interop
 * conversions (design.md, mistake M7).
 *
 * @param date A valid date.
 * @param out Receives 1..7 on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_date_day_of_week(const GCHRON_Date * date,
    int * out);

/**
 * @brief The day of the year: 1..365, or 1..366 in a leap year.
 *
 * @param date A valid date.
 * @param out Receives the ordinal day on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_date_day_of_year(const GCHRON_Date * date,
    int * out);

/**
 * @brief The date an ordinal day names: the ISO 8601 ordinal date, `2026-263`.
 *
 * @param year The astronomical year.
 * @param day_of_year 1..the length of that year.
 * @param out Receives the date on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_RANGE or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_date_from_ordinal(int32_t year, int day_of_year,
    GCHRON_Date * out);

/**
 * @brief The ISO 8601 week date of a date.
 *
 * Week 1 is the week holding 4 January, equivalently the week holding the
 * year's first Thursday (ISO 8601-1:2019 section 4.1.4).
 *
 * @param date A valid date.
 * @param out Receives the week date on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID, or GCHRON_ERR_RANGE when the
 *   week-year would fall outside the supported range.
 */
GCHRON_API GCHRON_Result gchron_date_to_iso_week(const GCHRON_Date * date,
    GCHRON_IsoWeekDate * out);

/**
 * @brief The date an ISO 8601 week date names.
 *
 * @param week A week date. Week 53 of a week-year that has only 52 is an
 *   error, not the first week of the next one.
 * @param out Receives the date on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_date_from_iso_week(
    const GCHRON_IsoWeekDate * week, GCHRON_Date * out);

/**
 * @brief How many ISO weeks a week-year has: 52 or 53.
 *
 * @param week_year The week-year.
 * @param out Receives the answer on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_iso_weeks_in_year(int32_t week_year, int * out);

/**
 * @brief The nth weekday of a month: "the third Monday in January", "the last
 * Sunday in March".
 *
 * This is the primitive a time-zone rule, a holiday table and an `RRULE` all
 * need, and computing it by stepping a loop over days is how each of them
 * gets it wrong at a month boundary.
 *
 * @param year The astronomical year.
 * @param month 1..12.
 * @param weekday 1 = Monday .. 7 = Sunday.
 * @param nth 1..5 counting from the start of the month, or -1..-5 counting
 *   back from the end. 0 is an error.
 * @param out Receives the date on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID for a bad argument; GCHRON_ERR_RANGE
 *   when that month has no such weekday - there is no fifth Monday in most
 *   Februaries - which is an answer, not a clamp to the fourth.
 */
GCHRON_API GCHRON_Result gchron_date_nth_weekday(int32_t year, int month,
    int weekday, int nth, GCHRON_Date * out);

/*--------------------------------------------------------------------------*
 * Arithmetic on days, and on the time of day
 *--------------------------------------------------------------------------*/

/**
 * @brief Add a whole number of days to a date.
 *
 * This is exact: a date has no zone, so no day here is 23 hours long.
 *
 * @param date A valid date.
 * @param days The number of days; may be negative.
 * @param out Receives the date on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE when the result
 *   leaves the supported years or the addition would overflow.
 */
GCHRON_API GCHRON_Result gchron_date_add_days(const GCHRON_Date * date,
    int64_t days, GCHRON_Date * out);

/**
 * @brief Nanoseconds since midnight.
 *
 * @param time A valid time of day.
 * @param out Receives 0..86399999999999 on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_time_to_nanos_of_day(const GCHRON_Time * time,
    int64_t * out);

/**
 * @brief The time of day a nanosecond count names.
 *
 * @param nanos 0..86399999999999. A value outside that is an error rather
 *   than a wrap into the next day: the caller owns the date.
 * @param out Receives the time on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_time_from_nanos_of_day(int64_t nanos,
    GCHRON_Time * out);

/*--------------------------------------------------------------------------*
 * Comparison
 *--------------------------------------------------------------------------*/

/**
 * @brief Order two dates.
 *
 * `memcmp` on a GCHRON_Date is wrong: the struct has padding, and its value
 * is not its bytes.
 *
 * @param a The first date.
 * @param b The second date.
 * @return Negative when @p a is earlier, 0 when equal, positive when later.
 *   A NULL argument sorts before a non-NULL one, so that the function is a
 *   total order however it is called.
 */
GCHRON_API int gchron_date_compare(const GCHRON_Date * a, const GCHRON_Date * b);

/**
 * @brief Order two times of day.
 *
 * @param a The first time.
 * @param b The second time.
 * @return Negative, zero or positive, as gchron_date_compare().
 */
GCHRON_API int gchron_time_compare(const GCHRON_Time * a, const GCHRON_Time * b);

/**
 * @brief Order two civil date-times.
 *
 * @param a The first date-time.
 * @param b The second date-time.
 * @return Negative, zero or positive, as gchron_date_compare().
 */
GCHRON_API int gchron_datetime_compare(const GCHRON_DateTime * a,
    const GCHRON_DateTime * b);

/**
 * @brief Order two year-months.
 *
 * @param a The first year-month.
 * @param b The second year-month.
 * @return Negative, zero or positive, as gchron_date_compare().
 */
GCHRON_API int gchron_year_month_compare(const GCHRON_YearMonth * a,
    const GCHRON_YearMonth * b);

/**
 * @brief Order two month-days, within a notional year.
 *
 * @param a The first month-day.
 * @param b The second month-day.
 * @return Negative, zero or positive, as gchron_date_compare().
 */
GCHRON_API int gchron_month_day_compare(const GCHRON_MonthDay * a,
    const GCHRON_MonthDay * b);

/*--------------------------------------------------------------------------*
 * Debugging
 *--------------------------------------------------------------------------*/

/**
 * @brief Write a human-readable description of a date-time to a stream.
 *
 * For debugging and for tests. The output is not a grammar and is not
 * promised to be stable; parse.h has the formats that are.
 *
 * @param dt The date-time. NULL prints as such.
 * @param stream Where to write. NULL is ignored.
 */
GCHRON_API void gchron_datetime_dump(const GCHRON_DateTime * dt, FILE * stream);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_CIVIL_H
