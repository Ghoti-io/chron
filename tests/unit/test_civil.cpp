/**
 * @file
 *
 * The proleptic Gregorian calendar, checked exhaustively and against Python.
 *
 * design.md section 12 asks for three things here, and they answer different
 * questions:
 *
 * - an **exhaustive** sweep over +/-100,000 years, which proves the labelling
 *   is a bijection that steps one day at a time;
 * - a **property** check over the full nine-digit range, which proves the
 *   arithmetic holds where no sweep can reach;
 * - an **oracle**, Python's `datetime.date`, which proves the labelling is
 *   the Gregorian one rather than merely a consistent one. The sweep alone
 *   would pass with August thirty days long and September thirty-one.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

/** The epoch day of GCHRON_YEAR_MIN-01-01, as the library's own bound. */
constexpr int64_t kEpochDayMin = INT64_C(-365243219162);

/** The epoch day of GCHRON_YEAR_MAX-12-31. */
constexpr int64_t kEpochDayMax = INT64_C(365241780471);

/** Open a committed vector file, failing the test when it is not there. */
std::ifstream open_vectors(const std::string & name) {
  std::string path = gchrontest::data_dir() + "/vectors/calendar/" + name;
  std::ifstream in(path);
  // A missing corpus fails rather than skips. A gate that turns a broken
  // harness into a green run is not a gate (design.md section 12.3).
  EXPECT_TRUE(in.is_open()) << "missing vector file: " << path;
  return in;
}

} // namespace

// The bounds are literals in civil_internal.h because they bound the input to
// a conversion that cannot compute them without first accepting an
// out-of-range day. This is what keeps the two from drifting apart.
TEST(Civil, TheEpochDayBoundsAreTheSupportedYearsEndpoints) {
  GCHRON_Date d;
  int64_t day;

  ASSERT_EQ(GCHRON_OK, gchron_date_create(GCHRON_YEAR_MIN, 1, 1, &d));
  ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&d, &day));
  EXPECT_EQ(kEpochDayMin, day);

  ASSERT_EQ(GCHRON_OK, gchron_date_create(GCHRON_YEAR_MAX, 12, 31, &d));
  ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&d, &day));
  EXPECT_EQ(kEpochDayMax, day);

  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_date_from_epoch_day(kEpochDayMin - 1, &d));
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_date_from_epoch_day(kEpochDayMax + 1, &d));
  EXPECT_EQ(GCHRON_OK, gchron_date_from_epoch_day(kEpochDayMin, &d));
  EXPECT_EQ(GCHRON_OK, gchron_date_from_epoch_day(kEpochDayMax, &d));
}

TEST(Civil, TheEpochDayOfTheUnixEpochIsZero) {
  GCHRON_Date d = gchrontest::date(1970, 1, 1);
  int64_t day = -1;
  ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&d, &day));
  EXPECT_EQ(GCHRON_EPOCH_DAY_UNIX, day);
}

/*
 * The exhaustive sweep. Every epoch day in the range gets four checks, and
 * the three that are not the round trip are the ones that catch a labelling
 * that round-trips and is still wrong:
 *
 *   1. from_epoch_day then to_epoch_day is the identity;
 *   2. the date advances by exactly one day - the next day of the same month,
 *      or the first of the next month, or 1 January of the next year;
 *   3. the day of the week advances by one, modulo seven;
 *   4. the day of the year advances by one, or resets to 1 at a year end
 *      whose predecessor was the last day of that year.
 */
TEST(Civil, EpochDayRoundTripsAndStepsOneDayAtATimeAcrossTheSweep) {
  const int years = gchrontest::sweep_years();
  GCHRON_Date first;
  GCHRON_Date last;
  int64_t from_day;
  int64_t to_day;

  ASSERT_EQ(GCHRON_OK, gchron_date_create(-years, 1, 1, &first));
  ASSERT_EQ(GCHRON_OK, gchron_date_create(years, 12, 31, &last));
  ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&first, &from_day));
  ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&last, &to_day));

  std::printf("[          ] sweeping %d years, %lld days\n", years * 2,
      static_cast<long long>(to_day - from_day + 1));

  GCHRON_Date previous{};
  int previous_dow = 0;
  int previous_doy = 0;
  bool have_previous = false;

  for (int64_t day = from_day; day <= to_day; ++day) {
    GCHRON_Date date;
    int64_t back = 0;
    int dow = 0;
    int doy = 0;

    ASSERT_EQ(GCHRON_OK, gchron_date_from_epoch_day(day, &date))
        << "epoch day " << day;
    ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&date, &back));
    ASSERT_EQ(day, back) << "epoch day " << day;
    ASSERT_EQ(GCHRON_OK, gchron_date_day_of_week(&date, &dow));
    ASSERT_EQ(GCHRON_OK, gchron_date_day_of_year(&date, &doy));

    if (have_previous) {
      int length = 0;
      ASSERT_EQ(GCHRON_OK,
          gchron_date_days_in_month(previous.year, previous.month, &length));

      if (previous.day < length) {
        ASSERT_EQ(previous.year, date.year) << "epoch day " << day;
        ASSERT_EQ(previous.month, date.month) << "epoch day " << day;
        ASSERT_EQ(previous.day + 1, date.day) << "epoch day " << day;
        ASSERT_EQ(previous_doy + 1, doy) << "epoch day " << day;
      }
      else if (previous.month < 12) {
        ASSERT_EQ(previous.year, date.year) << "epoch day " << day;
        ASSERT_EQ(previous.month + 1, date.month) << "epoch day " << day;
        ASSERT_EQ(1, date.day) << "epoch day " << day;
        ASSERT_EQ(previous_doy + 1, doy) << "epoch day " << day;
      }
      else {
        int year_length = 0;
        ASSERT_EQ(GCHRON_OK,
            gchron_date_days_in_year(previous.year, &year_length));
        ASSERT_EQ(previous.year + 1, date.year) << "epoch day " << day;
        ASSERT_EQ(1, date.month) << "epoch day " << day;
        ASSERT_EQ(1, date.day) << "epoch day " << day;
        ASSERT_EQ(year_length, previous_doy) << "epoch day " << day;
        ASSERT_EQ(1, doy) << "epoch day " << day;
      }

      ASSERT_EQ((previous_dow % 7) + 1, dow) << "epoch day " << day;
    }

    previous = date;
    previous_dow = dow;
    previous_doy = doy;
    have_previous = true;
  }
}

// The property no sweep can reach: the nine-digit range is 7.3e11 days, and
// what holds there is checked by sampling rather than by enumeration.
TEST(Civil, EpochDayRoundTripsOverTheWholeNineDigitRange) {
  std::mt19937_64 rng(20260920);
  std::uniform_int_distribution<int64_t> days(kEpochDayMin, kEpochDayMax);

  for (int i = 0; i < 200000; ++i) {
    int64_t day = days(rng);
    GCHRON_Date date;
    int64_t back = 0;
    ASSERT_EQ(GCHRON_OK, gchron_date_from_epoch_day(day, &date))
        << "epoch day " << day;
    ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&date, &back));
    ASSERT_EQ(day, back) << "epoch day " << day;
    ASSERT_GE(date.year, GCHRON_YEAR_MIN);
    ASSERT_LE(date.year, GCHRON_YEAR_MAX);
  }
}

// Python's datetime.date is the oracle for *which* labelling this is. The
// month lengths are the half the sweep cannot check.
TEST(Civil, AgreesWithPythonOnEveryYearFromOneToNineThousandNineHundred) {
  std::ifstream in = open_vectors("gregorian_years.vec");
  ASSERT_TRUE(in.is_open());

  std::string line;
  int rows = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream fields(line);
    long year = 0;
    long long jan1 = 0;
    int weekday = 0;
    int days_in_year = 0;
    std::string lengths;
    ASSERT_TRUE(fields >> year >> jan1 >> weekday >> days_in_year >> lengths)
        << line;

    GCHRON_Date date;
    int64_t day = 0;
    int value = 0;
    ASSERT_EQ(GCHRON_OK,
        gchron_date_create(static_cast<int32_t>(year), 1, 1, &date));
    ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&date, &day));
    ASSERT_EQ(jan1, day) << "year " << year;
    ASSERT_EQ(GCHRON_OK, gchron_date_day_of_week(&date, &value));
    ASSERT_EQ(weekday, value) << "year " << year;
    ASSERT_EQ(GCHRON_OK,
        gchron_date_days_in_year(static_cast<int32_t>(year), &value));
    ASSERT_EQ(days_in_year, value) << "year " << year;

    std::istringstream months(lengths);
    std::string field;
    int month = 1;
    while (std::getline(months, field, ',')) {
      ASSERT_EQ(GCHRON_OK,
          gchron_date_days_in_month(static_cast<int32_t>(year), month, &value));
      ASSERT_EQ(std::stoi(field), value) << "year " << year << " month "
                                         << month;
      ++month;
    }
    ASSERT_EQ(13, month) << "year " << year;
    ++rows;
  }
  EXPECT_EQ(9999, rows);
}

// The same oracle for the derived fields: the ISO week date, the ordinal day
// and the Rata Die, sampled on a stride that walks through weekdays, leap
// rules and month boundaries rather than around them.
TEST(Civil, AgreesWithPythonOnWeekDatesOrdinalsAndRataDie) {
  std::ifstream in = open_vectors("gregorian_dates.vec");
  ASSERT_TRUE(in.is_open());

  std::string line;
  int rows = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream fields(line);
    std::string ymd;
    std::string iso;
    long long epoch_day = 0;
    int weekday = 0;
    int day_of_year = 0;
    long long rata_die = 0;
    ASSERT_TRUE(
        fields >> ymd >> epoch_day >> weekday >> day_of_year >> iso >> rata_die)
        << line;

    int year = std::stoi(ymd.substr(0, 4));
    int month = std::stoi(ymd.substr(5, 2));
    int day = std::stoi(ymd.substr(8, 2));

    GCHRON_Date date;
    ASSERT_EQ(GCHRON_OK,
        gchron_date_create(static_cast<int32_t>(year), month, day, &date))
        << line;

    int64_t value64 = 0;
    ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&date, &value64));
    ASSERT_EQ(epoch_day, value64) << line;

    ASSERT_EQ(GCHRON_OK, gchron_epoch_day_to_rd(value64, &value64));
    ASSERT_EQ(rata_die, value64) << line;

    int value = 0;
    ASSERT_EQ(GCHRON_OK, gchron_date_day_of_week(&date, &value));
    ASSERT_EQ(weekday, value) << line;
    ASSERT_EQ(GCHRON_OK, gchron_date_day_of_year(&date, &value));
    ASSERT_EQ(day_of_year, value) << line;

    // The ordinal date is a representation, so it goes back the other way too.
    GCHRON_Date from_ordinal;
    ASSERT_EQ(GCHRON_OK,
        gchron_date_from_ordinal(static_cast<int32_t>(year), day_of_year,
            &from_ordinal));
    ASSERT_EQ(0, gchron_date_compare(&date, &from_ordinal)) << line;

    // "2026-W38-7"
    size_t w = iso.find("-W");
    ASSERT_NE(std::string::npos, w) << line;
    GCHRON_IsoWeekDate week;
    ASSERT_EQ(GCHRON_OK, gchron_date_to_iso_week(&date, &week));
    ASSERT_EQ(std::stol(iso.substr(0, w)), week.week_year) << line;
    ASSERT_EQ(std::stoi(iso.substr(w + 2, 2)), week.week) << line;
    ASSERT_EQ(std::stoi(iso.substr(w + 5, 1)), week.day) << line;

    GCHRON_Date from_week;
    ASSERT_EQ(GCHRON_OK, gchron_date_from_iso_week(&week, &from_week));
    ASSERT_EQ(0, gchron_date_compare(&date, &from_week)) << line;
    ++rows;
  }
  EXPECT_EQ(3664, rows);
}

TEST(Civil, JulianDayNumberAndRataDieHaveTheDocumentedAnchors) {
  int64_t value = 0;
  ASSERT_EQ(GCHRON_OK, gchron_epoch_day_to_jdn(0, &value));
  EXPECT_EQ(GCHRON_JDN_UNIX_EPOCH, value);
  ASSERT_EQ(GCHRON_OK, gchron_jdn_to_epoch_day(GCHRON_JDN_UNIX_EPOCH, &value));
  EXPECT_EQ(0, value);

  ASSERT_EQ(GCHRON_OK, gchron_epoch_day_to_rd(0, &value));
  EXPECT_EQ(GCHRON_RD_UNIX_EPOCH, value);
  ASSERT_EQ(GCHRON_OK, gchron_rd_to_epoch_day(GCHRON_RD_UNIX_EPOCH, &value));
  EXPECT_EQ(0, value);

  // 0001-01-01 is Rata Die 1, which is what "R.D." means.
  GCHRON_Date d = gchrontest::date(1, 1, 1);
  int64_t day = 0;
  ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&d, &day));
  ASSERT_EQ(GCHRON_OK, gchron_epoch_day_to_rd(day, &value));
  EXPECT_EQ(1, value);
}

TEST(Civil, JulianDayNumberOverflowIsReportedNotWrapped) {
  int64_t value = 0;
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_epoch_day_to_jdn(INT64_MAX, &value));
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_rd_to_epoch_day(INT64_MIN, &value));
}

TEST(Civil, DateCreateRefusesADayThatMonthDoesNotHave) {
  GCHRON_Date d;
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_date_create(2021, 2, 29, &d));
  EXPECT_EQ(GCHRON_OK, gchron_date_create(2020, 2, 29, &d));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_date_create(2020, 2, 30, &d));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_date_create(2020, 4, 31, &d));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_date_create(2020, 13, 1, &d));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_date_create(2020, 0, 1, &d));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_date_create(2020, 1, 0, &d));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_date_create(2020, 1, 1, nullptr));
}

TEST(Civil, DateCreateRefusesAYearOutsideTheSupportedRange) {
  GCHRON_Date d;
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_date_create(static_cast<int32_t>(GCHRON_YEAR_MAX) + 1, 1, 1, &d));
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_date_create(static_cast<int32_t>(GCHRON_YEAR_MIN) - 1, 1, 1, &d));
  EXPECT_EQ(GCHRON_OK, gchron_date_create(GCHRON_YEAR_MAX, 1, 1, &d));
  EXPECT_EQ(GCHRON_OK, gchron_date_create(GCHRON_YEAR_MIN, 1, 1, &d));
}

// Year 0 exists and is 1 BCE; year -1 is 2 BCE (design.md section 3.2). The
// leap rule applies to them the same way, which is what makes an astronomical
// year numbering worth having.
TEST(Civil, YearZeroExistsAndIsALeapYear) {
  bool leap = false;
  ASSERT_EQ(GCHRON_OK, gchron_year_is_leap(0, &leap));
  EXPECT_TRUE(leap);
  ASSERT_EQ(GCHRON_OK, gchron_year_is_leap(-4, &leap));
  EXPECT_TRUE(leap);
  ASSERT_EQ(GCHRON_OK, gchron_year_is_leap(-1, &leap));
  EXPECT_FALSE(leap);
  ASSERT_EQ(GCHRON_OK, gchron_year_is_leap(-100, &leap));
  EXPECT_FALSE(leap);
  ASSERT_EQ(GCHRON_OK, gchron_year_is_leap(-400, &leap));
  EXPECT_TRUE(leap);

  GCHRON_Date d;
  EXPECT_EQ(GCHRON_OK, gchron_date_create(0, 2, 29, &d));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_date_create(-1, 2, 29, &d));
}

TEST(Civil, TimeCreateRefusesSixtySeconds) {
  GCHRON_Time t;
  // A leap second reaches GCHRON_Time only through a parser and a
  // GCHRON_Leap policy, which is the seam the flag travels through.
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_time_create(23, 59, 60, 0, &t));
  EXPECT_EQ(GCHRON_OK, gchron_time_create(23, 59, 59, 999999999, &t));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_time_create(24, 0, 0, 0, &t));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_time_create(0, 60, 0, 0, &t));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_time_create(0, 0, 0, 1000000000, &t));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_time_create(0, 0, 0, -1, &t));
}

TEST(Civil, NanosecondsOfDayRoundTripAtEveryBoundary) {
  const int64_t last = GCHRON_SECONDS_PER_DAY * GCHRON_NANOS_PER_SECOND - 1;
  GCHRON_Time t;
  int64_t nanos = 0;

  ASSERT_EQ(GCHRON_OK, gchron_time_from_nanos_of_day(0, &t));
  EXPECT_EQ(0, t.hour);
  ASSERT_EQ(GCHRON_OK, gchron_time_to_nanos_of_day(&t, &nanos));
  EXPECT_EQ(0, nanos);

  ASSERT_EQ(GCHRON_OK, gchron_time_from_nanos_of_day(last, &t));
  EXPECT_EQ(23, t.hour);
  EXPECT_EQ(59, t.minute);
  EXPECT_EQ(59, t.second);
  EXPECT_EQ(999999999, t.nsec);
  ASSERT_EQ(GCHRON_OK, gchron_time_to_nanos_of_day(&t, &nanos));
  EXPECT_EQ(last, nanos);

  // Not a wrap into the next day: the caller owns the date.
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_time_from_nanos_of_day(last + 1, &t));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_time_from_nanos_of_day(-1, &t));
}

TEST(Civil, AddDaysCrossesMonthYearAndTheEpoch) {
  GCHRON_Date out;
  GCHRON_Date jan31 = gchrontest::date(2020, 1, 31);
  ASSERT_EQ(GCHRON_OK, gchron_date_add_days(&jan31, 1, &out));
  EXPECT_EQ(2020, out.year);
  EXPECT_EQ(2, out.month);
  EXPECT_EQ(1, out.day);

  GCHRON_Date jan1 = gchrontest::date(1970, 1, 1);
  ASSERT_EQ(GCHRON_OK, gchron_date_add_days(&jan1, -1, &out));
  EXPECT_EQ(1969, out.year);
  EXPECT_EQ(12, out.month);
  EXPECT_EQ(31, out.day);

  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_date_add_days(&jan1, INT64_MAX, &out));
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_date_add_days(&jan1, INT64_MIN, &out));
}

// ISO 8601 section 4.1.4: week 1 holds 4 January. The dates below are the
// ones where the week-year is not the calendar year, which is mistake M8 and
// the reason the week date carries its own year field.
TEST(Civil, WeekYearDiffersFromTheCalendarYearAtTheBoundaries) {
  struct Case {
    int32_t year;
    int month;
    int day;
    int32_t week_year;
    int week;
    int weekday;
  };
  // Checked against Python's date.isocalendar(); the sweep above checks 3664
  // more, and these are here so a reader can see the shape of the case.
  const Case cases[] = {
    { 2027, 1, 1, 2026, 53, 5 },  // a Friday in week 53 of the year before
    { 2026, 1, 1, 2026, 1, 4 },
    { 2021, 1, 1, 2020, 53, 5 },
    { 2020, 12, 31, 2020, 53, 4 },
    { 2019, 12, 30, 2020, 1, 1 }, // a Monday in week 1 of the year after
    { 1977, 1, 1, 1976, 53, 6 },
  };
  for (const Case & c : cases) {
    GCHRON_Date d = gchrontest::date(c.year, c.month, c.day);
    GCHRON_IsoWeekDate week;
    ASSERT_EQ(GCHRON_OK, gchron_date_to_iso_week(&d, &week))
        << c.year << "-" << c.month << "-" << c.day;
    EXPECT_EQ(c.week_year, week.week_year);
    EXPECT_EQ(c.week, week.week);
    EXPECT_EQ(c.weekday, week.day);

    GCHRON_Date back;
    ASSERT_EQ(GCHRON_OK, gchron_date_from_iso_week(&week, &back));
    EXPECT_EQ(0, gchron_date_compare(&d, &back));
  }
}

TEST(Civil, WeekFiftyThreeOfAFiftyTwoWeekYearIsRefused) {
  GCHRON_IsoWeekDate week{};
  GCHRON_Date out;
  int weeks = 0;

  ASSERT_EQ(GCHRON_OK, gchron_iso_weeks_in_year(2021, &weeks));
  EXPECT_EQ(52, weeks);
  ASSERT_EQ(GCHRON_OK, gchron_iso_weeks_in_year(2020, &weeks));
  EXPECT_EQ(53, weeks);

  week.week_year = 2021;
  week.week = 53;
  week.day = 1;
  // Not a quiet roll into week 1 of 2022: a caller who wrote it meant
  // something, and guessing what is how a week-number bug survives to the
  // following December.
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_date_from_iso_week(&week, &out));

  week.week = 52;
  EXPECT_EQ(GCHRON_OK, gchron_date_from_iso_week(&week, &out));

  week.week = 0;
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_date_from_iso_week(&week, &out));
  week.week = 1;
  week.day = 8;
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_date_from_iso_week(&week, &out));
}

// The primitive a time-zone rule, a holiday table and an RRULE all need.
TEST(Civil, NthWeekdayCountsFromBothEndsOfTheMonth) {
  GCHRON_Date out;

  // The second Sunday in March 2026 - when the United States changes its
  // clocks - is the 8th.
  ASSERT_EQ(GCHRON_OK,
      gchron_date_nth_weekday(2026, 3, GCHRON_SUNDAY, 2, &out));
  EXPECT_EQ(8, out.day);

  // The last Sunday in March 2026 - when the European Union does - is the
  // 29th, and asking for "the fifth" gets the same day.
  ASSERT_EQ(GCHRON_OK,
      gchron_date_nth_weekday(2026, 3, GCHRON_SUNDAY, -1, &out));
  EXPECT_EQ(29, out.day);
  ASSERT_EQ(GCHRON_OK,
      gchron_date_nth_weekday(2026, 3, GCHRON_SUNDAY, 5, &out));
  EXPECT_EQ(29, out.day);

  ASSERT_EQ(GCHRON_OK,
      gchron_date_nth_weekday(2026, 1, GCHRON_MONDAY, 1, &out));
  EXPECT_EQ(5, out.day);
}

TEST(Civil, NthWeekdayReportsAMonthThatHasNoSuchWeekday) {
  GCHRON_Date out;
  // February 2026 starts on a Sunday and has 28 days, so it has four Sundays
  // and no fifth. GCHRON_ERR_RANGE, not the fourth.
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_date_nth_weekday(2026, 2, GCHRON_SUNDAY, 5, &out));
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_date_nth_weekday(2026, 2, GCHRON_SUNDAY, -5, &out));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_date_nth_weekday(2026, 2, GCHRON_SUNDAY, 0, &out));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_date_nth_weekday(2026, 2, 8, 1, &out));
}

// The 29th of February is a GCHRON_MonthDay and not a GCHRON_Date, and asking
// for it in a common year is an answer rather than a silent 28th.
TEST(Civil, MonthDayInYearRefusesFebruaryTwentyNineOfACommonYear) {
  GCHRON_MonthDay md;
  GCHRON_Date out;
  ASSERT_EQ(GCHRON_OK, gchron_month_day_create(2, 29, &md));

  ASSERT_EQ(GCHRON_OK, gchron_month_day_in_year(&md, 2020, &out));
  EXPECT_EQ(29, out.day);
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_month_day_in_year(&md, 2021, &out));
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_month_day_in_year(&md, 2100, &out));
  EXPECT_EQ(GCHRON_OK, gchron_month_day_in_year(&md, 2000, &out));

  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_month_day_create(2, 30, &md));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_month_day_create(4, 31, &md));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_month_day_create(13, 1, &md));
}

TEST(Civil, ComparisonsAreTotalOrdersAndHandleNull) {
  GCHRON_Date a = gchrontest::date(2026, 9, 20);
  GCHRON_Date b = gchrontest::date(2026, 9, 21);
  GCHRON_Date c = gchrontest::date(2026, 10, 1);
  GCHRON_Date d = gchrontest::date(2027, 1, 1);

  EXPECT_LT(gchron_date_compare(&a, &b), 0);
  EXPECT_LT(gchron_date_compare(&b, &c), 0);
  EXPECT_LT(gchron_date_compare(&c, &d), 0);
  EXPECT_GT(gchron_date_compare(&d, &a), 0);
  EXPECT_EQ(0, gchron_date_compare(&a, &a));
  EXPECT_LT(gchron_date_compare(nullptr, &a), 0);
  EXPECT_GT(gchron_date_compare(&a, nullptr), 0);
  EXPECT_EQ(0, gchron_date_compare(nullptr, nullptr));

  GCHRON_Time t1 = gchrontest::timeofday(1, 2, 3, 4);
  GCHRON_Time t2 = gchrontest::timeofday(1, 2, 3, 5);
  EXPECT_LT(gchron_time_compare(&t1, &t2), 0);
  EXPECT_EQ(0, gchron_time_compare(&t1, &t1));

  GCHRON_DateTime dt1 = gchrontest::datetime(2026, 9, 20, 1, 2, 3);
  GCHRON_DateTime dt2 = gchrontest::datetime(2026, 9, 20, 1, 2, 4);
  EXPECT_LT(gchron_datetime_compare(&dt1, &dt2), 0);

  GCHRON_YearMonth ym1{ 2026, 9 };
  GCHRON_YearMonth ym2{ 2026, 10 };
  EXPECT_LT(gchron_year_month_compare(&ym1, &ym2), 0);

  GCHRON_MonthDay md1{ 2, 28 };
  GCHRON_MonthDay md2{ 2, 29 };
  EXPECT_LT(gchron_month_day_compare(&md1, &md2), 0);
}

TEST(Civil, DumpWritesSomethingForEveryInput) {
  GCHRON_DateTime dt = gchrontest::datetime(2026, 9, 20, 15, 30, 0);
  FILE * sink = std::tmpfile();
  ASSERT_NE(nullptr, sink);
  gchron_datetime_dump(&dt, sink);
  gchron_datetime_dump(nullptr, sink);
  gchron_datetime_dump(&dt, nullptr);
  EXPECT_GT(std::ftell(sink), 0);
  std::fclose(sink);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
