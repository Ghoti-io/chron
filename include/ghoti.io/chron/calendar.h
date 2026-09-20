/**
 * @file
 *
 * Calendars: the other ways of labelling a day.
 *
 * A calendar here is the function pair that turns an epoch day into a
 * `(year, month, day)` label and back, plus the facts a formatter and an
 * arithmetic routine need. It does **not** touch instants, offsets, zones or
 * the time of day: the time-zone database defines its rules in Gregorian and
 * this library evaluates them in Gregorian, so a Julian-calendar application
 * sees Julian dates *and* the same instants, offsets and zones as everybody
 * else (design.md section 5.2).
 *
 * Tier 0 (design.md section 3): needs nothing, allocates only for the
 * calendars that are built at run time.
 *
 * `civil.h` is this header with the calendar left out. Every function there
 * is the Gregorian case of one here, spelled without the argument because the
 * Gregorian case is what `text` and `compress` and `image` want and a
 * parameter they always pass `NULL` to is noise. A `NULL` calendar means
 * Gregorian everywhere one is accepted.
 *
 * Reference: Edward M. Reingold and Nachum Dershowitz, *Calendrical
 * Calculations: The Ultimate Edition* (2018), for the Julian algorithm and
 * the sample-date tables the tests use.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_CALENDAR_H
#define GHOTI_IO_GCHRON_CALENDAR_H

#include <ghoti.io/chron/allocator.h>
#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Forward declaration, so the vtable can name its own type. */
typedef struct GCHRON_Calendar GCHRON_Calendar;

/**
 * @brief A way of labelling the days.
 *
 * The struct is public because mistake M19 is a library whose set of
 * calendars is closed: ICU's is, and `java.time`'s needs a JAR. An
 * application with a calendar nobody anticipated fills one of these in and
 * every function in the library that takes a calendar takes theirs.
 *
 * The four query functions are not allowed to fail. A calendar that cannot
 * answer "how many days in that month" for a year inside
 * GCHRON_YEAR_MIN..GCHRON_YEAR_MAX is not a calendar; the conversions, which
 * *can* fail, are where a year out of range or a day that does not exist is
 * reported.
 */
struct GCHRON_Calendar {
  /** A stable identifier, e.g. `"gregory"`, `"julian"`, `"tabular:shire"`. */
  const char * id;

  /**
   * The label this calendar gives an epoch day.
   *
   * @return GCHRON_OK, or GCHRON_ERR_RANGE when the day falls outside the
   *   years this calendar can label.
   */
  GCHRON_Result (*from_epoch_day)(const GCHRON_Calendar * self,
      int64_t epoch_day, GCHRON_Date * out);

  /**
   * The epoch day a label names.
   *
   * @return GCHRON_OK; GCHRON_ERR_INVALID for a day that does not exist in
   *   that month; GCHRON_ERR_RANGE for a year out of range; GCHRON_ERR_GAP
   *   for a day a calendar reform deleted (design.md, mistake M21).
   */
  GCHRON_Result (*to_epoch_day)(const GCHRON_Calendar * self,
      const GCHRON_Date * date, int64_t * out);

  /** How many months that year has. */
  int (*months_in_year)(const GCHRON_Calendar * self, int32_t year);

  /** How many days that month of that year has. */
  int (*days_in_month)(const GCHRON_Calendar * self, int32_t year, int month);

  /** How many days that year has. */
  int (*days_in_year)(const GCHRON_Calendar * self, int32_t year);

  /** Whether that year is a leap year in this calendar's own sense. */
  bool (*is_leap_year)(const GCHRON_Calendar * self, int32_t year);

  /** Days in a week. Seven for every calendar the real world has used. */
  int days_in_week;

  /**
   * An epoch day that is day 1 of some week.
   *
   * What makes gchron_calendar_day_of_week() mean anything for a calendar
   * whose week is not the Gregorian one.
   */
  int64_t week_epoch_day;

  /** Whatever the implementation needs. Ignored by this library. */
  void * ctx;
};

/*--------------------------------------------------------------------------*
 * The calendars this library ships
 *--------------------------------------------------------------------------*/

/**
 * @brief The proleptic Gregorian calendar - the default everywhere.
 *
 * The same algorithm `civil.h` uses, behind the vtable, so that a caller
 * writing calendar-generic code has one to compare against.
 *
 * @return A borrowed, immutable calendar that lives for the life of the
 *   program. Never freed.
 */
GCHRON_API const GCHRON_Calendar * gchron_calendar_gregorian(void);

/**
 * @brief The proleptic Julian calendar.
 *
 * Every fourth year is a leap year, with no century rule - which is why it
 * has drifted a day per 128 years against the seasons, and why the reform of
 * 1582 happened. Used for historical dates, for Orthodox liturgical
 * computation, and by games.
 *
 * Years are astronomical here as everywhere else: year 0 exists and is 1 BCE
 * (design.md section 3.2).
 *
 * @return A borrowed, immutable calendar. Never freed.
 */
GCHRON_API const GCHRON_Calendar * gchron_calendar_julian(void);

/** The epoch day of the Gregorian reform in Rome: 1582-10-15. */
#define GCHRON_CUTOVER_ROME INT64_C(-141427)

/** The epoch day Britain and its colonies adopted: 1752-09-14. */
#define GCHRON_CUTOVER_BRITAIN INT64_C(-79366)

/** The epoch day Russia adopted: 1918-02-14. */
#define GCHRON_CUTOVER_RUSSIA INT64_C(-18949)

/**
 * @brief A calendar that is Julian before a cut-over day and Gregorian from
 * it.
 *
 * What a historical record actually uses, and what every "historical" date
 * computed in a database gets wrong by applying the proleptic Gregorian
 * calendar to a date that predates it (design.md, mistake M21).
 *
 * The cut-over is a parameter because it was a different day in every
 * country: GCHRON_CUTOVER_ROME, GCHRON_CUTOVER_BRITAIN and
 * GCHRON_CUTOVER_RUSSIA are the three that come up most.
 *
 * **The days the reform deleted do not exist**, and asking for one is
 * `GCHRON_ERR_GAP` - the same result a daylight-saving gap gives, resolved
 * with the same GCHRON_Resolve policy, because it is the same kind of
 * question. In Rome, 5 to 14 October 1582 are those days.
 *
 * @param cutover_epoch_day The first day labelled by the Gregorian calendar.
 * @param allocator Allocator. NULL means gchron_allocator_default().
 * @param out Receives a calendar the caller owns and frees with
 *   gchron_calendar_destroy(); untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID, GCHRON_ERR_RANGE or GCHRON_ERR_OOM.
 */
GCHRON_API GCHRON_Result gchron_calendar_hybrid(int64_t cutover_epoch_day,
    const GCHRON_Allocator * allocator, GCHRON_Calendar ** out);

/*--------------------------------------------------------------------------*
 * Calendars for worlds that do not exist
 *--------------------------------------------------------------------------*/

/**
 * @brief Which years of a repeating cycle are leap years.
 *
 * A cycle length and a bit per year of it. That one shape expresses the
 * Julian rule (a cycle of 4 with one bit set), the Gregorian rule (a cycle of
 * 400 with ninety-seven), the thirty-year tabular Islamic cycle, the
 * thirty-three-year Persian approximation, and whatever a game designer wrote
 * down.
 */
typedef struct GCHRON_LeapRule {
  /** Years in the cycle, 1..GCHRON_LEAP_CYCLE_MAX. */
  int cycle_years;

  /**
   * One bit per year of the cycle, least-significant bit of byte 0 first.
   *
   * Bit `n` set means year `n` of the cycle is a leap year, where year 0 of
   * the cycle is the one whose number is a multiple of @ref cycle_years.
   *
   * Borrowed for the duration of the gchron_calendar_tabular() call and
   * copied; the caller may free it afterwards.
   */
  const uint8_t * pattern;

  /** Bytes at @ref pattern; must be at least `(cycle_years + 7) / 8`. */
  size_t pattern_bytes;
} GCHRON_LeapRule;

/** The longest leap cycle a tabular calendar may have. */
#define GCHRON_LEAP_CYCLE_MAX 4096

/** The most months a tabular calendar may have in a year. */
#define GCHRON_TABULAR_MONTH_MAX 64

/**
 * @brief A calendar with fixed month lengths and a cyclic leap rule.
 *
 * design.md section 5.5: the user's stated need is a game, a game's calendar
 * is usually simple in structure and arbitrary in its constants, and what it
 * must never require is a C compiler. Ten months of thirty-six days and a
 * five-day festival is this struct, filled in.
 *
 * A calendar with a different *day* - a thirty-hour world - is out of scope.
 * GCHRON_Time is 24 hours of 60 minutes of 60 SI seconds because the instant
 * is, and a world with a different day is a scaling the application applies
 * to instants before handing them here.
 */
typedef struct GCHRON_TabularCalendar {
  /** A stable identifier. Copied. */
  const char * id;

  /** Months in a year, 1..GCHRON_TABULAR_MONTH_MAX. */
  int month_count;

  /**
   * The length of each month in a common year, @ref month_count of them.
   * Copied.
   */
  const uint16_t * month_days;

  /** The 1-based month that gains @ref leap_days in a leap year, or 0. */
  int leap_month;

  /** How many days it gains. */
  int leap_days;

  /** Which years of the cycle are leap years. */
  GCHRON_LeapRule leap_rule;

  /** The epoch day on which year 0, month 1, day 1 falls. */
  int64_t epoch_day_of_year_zero;

  /** Days in a week. */
  int days_in_week;

  /** An epoch day that is day 1 of some week. */
  int64_t week_epoch_day;
} GCHRON_TabularCalendar;

/**
 * @brief Build a calendar out of a description.
 *
 * The Gregorian and Julian calendars this library ships are **not**
 * implemented this way - a closed-form algorithm is faster and is what the
 * oracles were checked against - but
 * `tests/unit/test_calendar_tabular.cpp` constructs both as tabular calendars
 * and proves them identical to the shipped ones across the whole supported
 * year range. That is the test that proves the tabular engine correct, and it
 * costs nothing to write.
 *
 * @param description The calendar. Everything it points at is copied.
 * @param allocator Allocator. NULL means gchron_allocator_default().
 * @param out Receives a calendar the caller owns and frees with
 *   gchron_calendar_destroy(); untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID for a description that is not a
 *   calendar - no months, a month of zero days, a leap cycle with no leap
 *   years in it but a leap month named; GCHRON_ERR_RANGE when a year of the
 *   cycle would not fit; GCHRON_ERR_OOM.
 */
GCHRON_API GCHRON_Result gchron_calendar_tabular(
    const GCHRON_TabularCalendar * description,
    const GCHRON_Allocator * allocator, GCHRON_Calendar ** out);

/**
 * @brief Free a calendar built by gchron_calendar_hybrid() or
 * gchron_calendar_tabular().
 *
 * The shipped calendars - gchron_calendar_gregorian() and
 * gchron_calendar_julian() - are static and passing one here is ignored, so a
 * caller holding "whichever calendar the user chose" need not remember which
 * kind it is.
 *
 * @param calendar The calendar. NULL is ignored.
 */
GCHRON_API void gchron_calendar_destroy(GCHRON_Calendar * calendar);

/*--------------------------------------------------------------------------*
 * Using a calendar
 *--------------------------------------------------------------------------*/

/**
 * @brief The label a calendar gives an epoch day.
 *
 * @param calendar The calendar. NULL means Gregorian.
 * @param epoch_day Days since 1970-01-01.
 * @param out Receives the date on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_RANGE or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_calendar_from_epoch_day(
    const GCHRON_Calendar * calendar, int64_t epoch_day, GCHRON_Date * out);

/**
 * @brief The epoch day a label names in a calendar.
 *
 * @param calendar The calendar. NULL means Gregorian.
 * @param date The date, read in that calendar.
 * @param out Receives the epoch day on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE; GCHRON_ERR_GAP
 *   when that day was deleted by a calendar reform.
 */
GCHRON_API GCHRON_Result gchron_calendar_to_epoch_day(
    const GCHRON_Calendar * calendar, const GCHRON_Date * date, int64_t * out);

/**
 * @brief Whether a date exists in a calendar.
 *
 * @param calendar The calendar. NULL means Gregorian.
 * @param date The date.
 * @return `true` when every field is in range and the day exists.
 */
GCHRON_API bool gchron_calendar_date_is_valid(
    const GCHRON_Calendar * calendar, const GCHRON_Date * date);

/**
 * @brief How many days a month has in a calendar.
 *
 * @param calendar The calendar. NULL means Gregorian.
 * @param year The year.
 * @param month 1..gchron_calendar_months_in_year().
 * @param out Receives the answer on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_RANGE or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_calendar_days_in_month(
    const GCHRON_Calendar * calendar, int32_t year, int month, int * out);

/**
 * @brief How many months a year has in a calendar.
 *
 * Twelve in every calendar the real world has used, and whatever a game says
 * in one it wrote.
 *
 * @param calendar The calendar. NULL means Gregorian.
 * @param year The year.
 * @param out Receives the answer on success; untouched on failure.
 * @return GCHRON_OK or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_calendar_months_in_year(
    const GCHRON_Calendar * calendar, int32_t year, int * out);

/**
 * @brief How many days a year has in a calendar.
 *
 * @param calendar The calendar. NULL means Gregorian.
 * @param year The year.
 * @param out Receives the answer on success; untouched on failure.
 * @return GCHRON_OK or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_calendar_days_in_year(
    const GCHRON_Calendar * calendar, int32_t year, int * out);

/**
 * @brief Whether a year is a leap year in a calendar's own sense.
 *
 * @param calendar The calendar. NULL means Gregorian.
 * @param year The year.
 * @param out Receives the answer on success; untouched on failure.
 * @return GCHRON_OK or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_calendar_is_leap_year(
    const GCHRON_Calendar * calendar, int32_t year, bool * out);

/**
 * @brief The day of the week, in a calendar whose week may not be seven days.
 *
 * Counted from the calendar's own GCHRON_Calendar::week_epoch_day, and
 * numbered 1..GCHRON_Calendar::days_in_week. For every calendar with a
 * seven-day week anchored the way the real ones are, this agrees with
 * gchron_date_day_of_week()'s ISO numbering.
 *
 * @param calendar The calendar. NULL means Gregorian.
 * @param date A valid date in that calendar.
 * @param out Receives 1..days_in_week on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_calendar_day_of_week(
    const GCHRON_Calendar * calendar, const GCHRON_Date * date, int * out);

/**
 * @brief The day of the year, 1-based.
 *
 * @param calendar The calendar. NULL means Gregorian.
 * @param date A valid date in that calendar.
 * @param out Receives the ordinal day on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_calendar_day_of_year(
    const GCHRON_Calendar * calendar, const GCHRON_Date * date, int * out);

/**
 * @brief A calendar's identifier.
 *
 * @param calendar The calendar. NULL means Gregorian.
 * @return A borrowed string, never NULL.
 */
GCHRON_API const char * gchron_calendar_id(const GCHRON_Calendar * calendar);

/**
 * @brief Convert a date from one calendar to another.
 *
 * By way of the epoch day, which is the currency between calendars and the
 * reason there is only ever one conversion to write rather than one per pair
 * (design.md section 3.3).
 *
 * @param from The calendar @p date is written in. NULL means Gregorian.
 * @param date The date.
 * @param to The calendar to write it in. NULL means Gregorian.
 * @param out Receives the date on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID, GCHRON_ERR_RANGE or GCHRON_ERR_GAP.
 */
GCHRON_API GCHRON_Result gchron_calendar_convert(
    const GCHRON_Calendar * from, const GCHRON_Date * date,
    const GCHRON_Calendar * to, GCHRON_Date * out);

/**
 * @brief Write a human-readable description of a calendar to a stream.
 *
 * @param calendar The calendar. NULL prints as Gregorian.
 * @param stream Where to write. NULL is ignored.
 */
GCHRON_API void gchron_calendar_dump(const GCHRON_Calendar * calendar,
    FILE * stream);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_CALENDAR_H
