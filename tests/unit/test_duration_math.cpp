/**
 * @file
 *
 * Duration arithmetic: applying one to a calendar, balancing, differencing,
 * rounding, and the ISO 8601 grammar.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/chron/calendar.h>
#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

GCHRON_Duration months(int64_t n) {
  GCHRON_Duration d{};
  d.months = n;
  return d;
}

GCHRON_Duration days(int64_t n) {
  GCHRON_Duration d{};
  d.days = n;
  return d;
}

std::string iso(const GCHRON_Duration & d, GCHRON_Result * result) {
  char buf[GCHRON_RFC3339_DURATION_MAX];
  size_t len = 0;
  *result = gchron_write_iso8601_duration(&d, buf, sizeof(buf), &len);
  return (*result == GCHRON_OK) ? std::string(buf, len) : std::string();
}

GCHRON_Result parse_iso(const std::string & text, GCHRON_Duration * out,
    GCHRON_Error * err = nullptr) {
  return gchron_parse_iso8601_duration(text.data(), text.size(), nullptr, out,
      nullptr, err);
}

} // namespace

/*
 * design.md section 4.3, stated twice in the headers and checked here:
 * `Jan 31 + 1 month` has no correct answer, it has a chosen one, and the
 * caller who did not choose is told so.
 */
TEST(DurationMath, MonthEndOverflowIsAChoiceAndZeroRefusesToMakeIt) {
  GCHRON_DateTime jan31 = gchrontest::datetime(2026, 1, 31, 0, 0, 0);
  GCHRON_DateTime out{};
  GCHRON_Duration one = months(1);

  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_datetime_add(&jan31, &one, nullptr, GCHRON_OVERFLOW_REJECT,
          &out));
  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_add(&jan31, &one, nullptr, GCHRON_OVERFLOW_CONSTRAIN,
          &out));
  EXPECT_EQ(2, out.date.month);
  EXPECT_EQ(28, out.date.day);

  // A leap year clamps to the 29th instead.
  GCHRON_DateTime jan31_leap = gchrontest::datetime(2024, 1, 31, 0, 0, 0);
  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_add(&jan31_leap, &one, nullptr,
          GCHRON_OVERFLOW_CONSTRAIN, &out));
  EXPECT_EQ(29, out.date.day);
}

// The consequence the header states: calendar-unit arithmetic is neither
// associative nor commutative, and a caller who needs a stable "same day next
// month" keeps a GCHRON_MonthDay and re-derives the date.
TEST(DurationMath, CalendarArithmeticIsNotAssociative) {
  GCHRON_DateTime jan31 = gchrontest::datetime(2026, 1, 31, 0, 0, 0);
  GCHRON_DateTime once{};
  GCHRON_DateTime twice{};
  GCHRON_DateTime at_once{};

  GCHRON_Duration one = months(1);
  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_add(&jan31, &one, nullptr, GCHRON_OVERFLOW_CONSTRAIN,
          &once));
  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_add(&once, &one, nullptr, GCHRON_OVERFLOW_CONSTRAIN,
          &twice));
  GCHRON_Duration two = months(2);
  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_add(&jan31, &two, nullptr, GCHRON_OVERFLOW_CONSTRAIN,
          &at_once));

  EXPECT_EQ(28, twice.date.day) << "Jan 31 + P1M + P1M is March 28";
  EXPECT_EQ(31, at_once.date.day) << "Jan 31 + P2M is March 31";
  EXPECT_NE(0, gchron_datetime_compare(&twice, &at_once));
}

/*
 * design.md section 4.4: `until` walks the calendar forward from the start,
 * so `Jan 31 until Mar 1` in months is `1 month 1 day`. The asymmetry that
 * follows is real and is documented rather than hidden by symmetrising.
 */
TEST(DurationMath, UntilWalksForwardFromTheStart) {
  GCHRON_DateTime jan31 = gchrontest::datetime(2026, 1, 31, 0, 0, 0);
  GCHRON_DateTime mar1 = gchrontest::datetime(2026, 3, 1, 0, 0, 0);
  GCHRON_Duration d{};

  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_until(&jan31, &mar1, GCHRON_UNIT_MONTH, nullptr, &d));
  EXPECT_EQ(1, d.months);
  EXPECT_EQ(1, d.days) << "not 1 month -2 days";
  EXPECT_TRUE(gchron_duration_is_valid(&d));
}

TEST(DurationMath, UntilIsNotTheNegationOfItsReverse) {
  // Feb 28 forward to Mar 31 is a month and three days, because Feb 28 + 1
  // month is Mar 28. Mar 31 back to Feb 28 is exactly a month, because Mar 31
  // - 1 month clamps to Feb 28. Both are right; they are different questions.
  GCHRON_DateTime feb28 = gchrontest::datetime(2026, 2, 28, 0, 0, 0);
  GCHRON_DateTime mar31 = gchrontest::datetime(2026, 3, 31, 0, 0, 0);
  GCHRON_Duration forward{};
  GCHRON_Duration backward{};

  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_until(&feb28, &mar31, GCHRON_UNIT_MONTH, nullptr,
          &forward));
  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_until(&mar31, &feb28, GCHRON_UNIT_MONTH, nullptr,
          &backward));

  EXPECT_EQ(1, forward.months);
  EXPECT_EQ(3, forward.days);
  EXPECT_EQ(-1, backward.months);
  EXPECT_EQ(0, backward.days);
  EXPECT_NE(forward.days, -backward.days);
}

TEST(DurationMath, UntilInYearsCarriesEverythingBelow) {
  GCHRON_DateTime from = gchrontest::datetime(2000, 2, 29, 0, 0, 0);
  GCHRON_DateTime to = gchrontest::datetime(2026, 9, 20, 12, 30, 0);
  GCHRON_Duration d{};
  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_until(&from, &to, GCHRON_UNIT_YEAR, nullptr, &d));
  EXPECT_EQ(26, d.years);
  EXPECT_EQ(6, d.months);
  // Twenty-two days, not twenty-three, and the difference is the point.
  // gchron_datetime_add() applies years and months *together*, so
  // 2000-02-29 + P26Y6M is 2026-08-29 - the day survives because August has
  // thirty-one - while +P26Y and then +P6M would clamp to February 28 first
  // and land on August 28. `until` measures the way `add` composes, so that
  // the duration it produces adds back up to its own endpoint.
  EXPECT_EQ(22, d.days);
  EXPECT_EQ(12, d.hours);
  EXPECT_EQ(30, d.minutes);
  // Weeks are a unit a caller opts into; asking for years does not produce
  // "3 weeks and 1 day" instead of 22 days.
  EXPECT_EQ(0, d.weeks);

  // And adding it back lands exactly where it came from.
  GCHRON_DateTime back{};
  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_add(&from, &d, nullptr, GCHRON_OVERFLOW_CONSTRAIN,
          &back));
  EXPECT_EQ(0, gchron_datetime_compare(&to, &back));
}

TEST(DurationMath, WeeksAppearOnlyWhenAskedFor) {
  GCHRON_DateTime from = gchrontest::datetime(2026, 1, 1, 0, 0, 0);
  GCHRON_DateTime to = gchrontest::datetime(2026, 1, 21, 0, 0, 0);
  GCHRON_Duration d{};

  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_until(&from, &to, GCHRON_UNIT_MONTH, nullptr, &d));
  EXPECT_EQ(0, d.months);
  EXPECT_EQ(0, d.weeks);
  EXPECT_EQ(20, d.days);

  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_until(&from, &to, GCHRON_UNIT_WEEK, nullptr, &d));
  EXPECT_EQ(2, d.weeks);
  EXPECT_EQ(6, d.days);
}

TEST(DurationMath, UntilRoundTripsOverManyPairs) {
  // The property the header promises: from + until(from, to) == to.
  const int32_t years[] = { 1600, 1900, 1999, 2000, 2024, 2026, 2100 };
  const int months_list[] = { 1, 2, 3, 8, 12 };
  const int days_list[] = { 1, 15, 28, 29, 30, 31 };
  GCHRON_Unit units[] = {
    GCHRON_UNIT_YEAR, GCHRON_UNIT_MONTH, GCHRON_UNIT_WEEK, GCHRON_UNIT_DAY,
    GCHRON_UNIT_HOUR
  };

  int checked = 0;
  for (int32_t y1 : years) {
    for (int m1 : months_list) {
      for (int d1 : days_list) {
        GCHRON_Date from_date{};
        if (gchron_date_create(y1, m1, d1, &from_date) != GCHRON_OK) {
          continue;
        }
        for (int32_t y2 : years) {
          GCHRON_Date to_date{};
          if (gchron_date_create(y2, 7, 4, &to_date) != GCHRON_OK) {
            continue;
          }
          GCHRON_DateTime from{ from_date, gchrontest::timeofday(3, 15, 0) };
          GCHRON_DateTime to{ to_date, gchrontest::timeofday(19, 45, 30) };
          for (GCHRON_Unit unit : units) {
            GCHRON_Duration d{};
            ASSERT_EQ(GCHRON_OK,
                gchron_datetime_until(&from, &to, unit, nullptr, &d))
                << y1 << "-" << m1 << "-" << d1 << " to " << y2;
            ASSERT_TRUE(gchron_duration_is_valid(&d));
            GCHRON_DateTime back{};
            ASSERT_EQ(GCHRON_OK,
                gchron_datetime_add(&from, &d, nullptr,
                    GCHRON_OVERFLOW_CONSTRAIN, &back));
            ASSERT_EQ(0, gchron_datetime_compare(&to, &back))
                << y1 << "-" << m1 << "-" << d1 << " to " << y2
                << " unit " << gchron_unit_string(unit);
            ++checked;
          }
        }
      }
    }
  }
  EXPECT_GT(checked, 500);
}

/*
 * design.md section 4.2: a largest unit at or above days needs something to
 * be relative to, and asking for GCHRON_UNIT_DAY without one is an error -
 * not "assume twenty-four hours".
 */
TEST(DurationMath, BalancingToDaysNeedsSomethingToBeRelativeTo) {
  GCHRON_Duration ninety{};
  ninety.minutes = 90;
  GCHRON_Duration out{};

  ASSERT_EQ(GCHRON_OK,
      gchron_duration_balance(&ninety, GCHRON_UNIT_HOUR, nullptr, nullptr,
          &out));
  EXPECT_EQ(1, out.hours);
  EXPECT_EQ(30, out.minutes);

  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_duration_balance(&ninety, GCHRON_UNIT_DAY, nullptr, nullptr,
          &out));

  GCHRON_DateTime anchor = gchrontest::datetime(2026, 9, 20, 0, 0, 0);
  GCHRON_Duration long_one{};
  long_one.hours = 50;
  ASSERT_EQ(GCHRON_OK,
      gchron_duration_balance(&long_one, GCHRON_UNIT_DAY, &anchor, nullptr,
          &out));
  EXPECT_EQ(2, out.days);
  EXPECT_EQ(2, out.hours);
}

// The library never rewrites 90 minutes as 1 hour 30 unless asked.
TEST(DurationMath, BalancingIsSomethingTheCallerAsksFor) {
  GCHRON_Duration ninety{};
  ninety.minutes = 90;
  GCHRON_Duration copy = ninety;
  GCHRON_DateTime anchor = gchrontest::datetime(2026, 9, 20, 0, 0, 0);
  GCHRON_DateTime moved{};

  // Using it changes nothing about it.
  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_add(&anchor, &ninety, nullptr, GCHRON_OVERFLOW_REJECT,
          &moved));
  EXPECT_TRUE(gchron_duration_identical(&copy, &ninety));
  EXPECT_EQ(1, moved.time.hour);
  EXPECT_EQ(30, moved.time.minute);
}

TEST(DurationMath, RoundingToAnExactUnitFollowsTheModeAndZeroRefuses) {
  GCHRON_Duration d{};
  d.seconds = 90;
  GCHRON_Duration out{};

  // The zero value refuses an inexact result rather than choosing for the
  // caller (design.md section 3.7).
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_duration_round(&d, GCHRON_UNIT_MINUTE, GCHRON_ROUND_REJECT,
          nullptr, nullptr, &out));

  ASSERT_EQ(GCHRON_OK,
      gchron_duration_round(&d, GCHRON_UNIT_MINUTE, GCHRON_ROUND_TRUNCATE,
          nullptr, nullptr, &out));
  EXPECT_EQ(1, out.minutes);
  EXPECT_EQ(0, out.seconds);

  ASSERT_EQ(GCHRON_OK,
      gchron_duration_round(&d, GCHRON_UNIT_MINUTE, GCHRON_ROUND_HALF_EXPAND,
          nullptr, nullptr, &out));
  EXPECT_EQ(2, out.minutes);

  ASSERT_EQ(GCHRON_OK,
      gchron_duration_round(&d, GCHRON_UNIT_MINUTE, GCHRON_ROUND_CEIL,
          nullptr, nullptr, &out));
  EXPECT_EQ(2, out.minutes);

  // An exact value rounds to itself under every mode, the refusing one too.
  GCHRON_Duration exact{};
  exact.seconds = 120;
  for (int mode = 0; mode <= GCHRON_ROUND_HALF_EVEN; ++mode) {
    ASSERT_EQ(GCHRON_OK,
        gchron_duration_round(&exact, GCHRON_UNIT_MINUTE,
            static_cast<GCHRON_Rounding>(mode), nullptr, nullptr, &out))
        << "mode " << mode;
    EXPECT_EQ(2, out.minutes) << "mode " << mode;
  }
}

TEST(DurationMath, RoundingToACalendarUnitUsesThatUnitsActualLength) {
  // Twenty days from 1 January is two thirds of the way through a 31-day
  // month, so it rounds up; twenty days from 1 February is more than two
  // thirds of a 28-day one and rounds up too, but ten days from 1 January
  // does not.
  GCHRON_DateTime january = gchrontest::datetime(2026, 1, 1, 0, 0, 0);
  GCHRON_Duration out{};

  GCHRON_Duration twenty = days(20);
  ASSERT_EQ(GCHRON_OK,
      gchron_duration_round(&twenty, GCHRON_UNIT_MONTH,
          GCHRON_ROUND_HALF_EXPAND, &january, nullptr, &out));
  EXPECT_EQ(1, out.months);

  GCHRON_Duration ten = days(10);
  ASSERT_EQ(GCHRON_OK,
      gchron_duration_round(&ten, GCHRON_UNIT_MONTH, GCHRON_ROUND_HALF_EXPAND,
          &january, nullptr, &out));
  EXPECT_EQ(0, out.months);

  // And a calendar unit needs something to measure from.
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_duration_round(&twenty, GCHRON_UNIT_MONTH,
          GCHRON_ROUND_HALF_EXPAND, nullptr, nullptr, &out));
}

TEST(DurationMath, ArithmeticWorksInTheJulianCalendarToo) {
  const GCHRON_Calendar * julian = gchron_calendar_julian();
  // Julian 1700 is a leap year; Gregorian 1700 is not, so the same addition
  // gives different answers in the two calendars.
  GCHRON_DateTime jan31 = gchrontest::datetime(1700, 1, 31, 0, 0, 0);
  GCHRON_DateTime in_julian{};
  GCHRON_DateTime in_gregorian{};
  GCHRON_Duration one = months(1);

  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_add(&jan31, &one, julian, GCHRON_OVERFLOW_CONSTRAIN,
          &in_julian));
  ASSERT_EQ(GCHRON_OK,
      gchron_datetime_add(&jan31, &one, nullptr, GCHRON_OVERFLOW_CONSTRAIN,
          &in_gregorian));
  EXPECT_EQ(29, in_julian.date.day);
  EXPECT_EQ(28, in_gregorian.date.day);
}

TEST(DurationMath, ADateRefusesAnExactUnit) {
  GCHRON_Date date = gchrontest::date(2026, 9, 20);
  GCHRON_Date out{};
  GCHRON_Duration hour{};
  hour.hours = 1;
  // A date has no time of day for an hour to act on, and saying so beats
  // silently discarding it.
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_date_add(&date, &hour, nullptr, GCHRON_OVERFLOW_REJECT, &out));

  GCHRON_Duration week{};
  week.weeks = 1;
  ASSERT_EQ(GCHRON_OK,
      gchron_date_add(&date, &week, nullptr, GCHRON_OVERFLOW_REJECT, &out));
  EXPECT_EQ(27, out.day);
}

/*--------------------------------------------------------------------------*
 * The ISO 8601 grammar
 *--------------------------------------------------------------------------*/

// The two duration grammars are different, and this is where they part.
TEST(Iso8601Duration, AcceptsWhatAppendixARefuses) {
  struct Case { const char * text; const char * why; };
  const Case cases[] = {
    { "P1Y2D", "no nesting here, so years may be followed by days" },
    { "PT1H2S", "nor here" },
    { "PT0.5S", "a fraction on the smallest unit present" },
    { "PT0,5S", "the comma separator ISO 8601 itself prefers" },
    { "-P1D", "a leading sign" },
    { "P-1D", "a sign inside a component, which is ISO 8601-2" },
    { "P1W2D", "weeks alongside other units" },
  };
  for (const Case & c : cases) {
    GCHRON_Duration iso_value{};
    GCHRON_Duration rfc_value{};
    EXPECT_EQ(GCHRON_OK, parse_iso(c.text, &iso_value))
        << c.text << ": " << c.why;
    EXPECT_NE(GCHRON_OK,
        gchron_parse_rfc3339_duration(c.text, std::strlen(c.text), nullptr,
            &rfc_value, nullptr, nullptr))
        << c.text << " should still be refused by appendix A";
  }
}

TEST(Iso8601Duration, TheFractionLandsOnTheNanosecondField) {
  GCHRON_Duration d{};
  ASSERT_EQ(GCHRON_OK, parse_iso("PT0.5S", &d));
  EXPECT_EQ(0, d.seconds);
  EXPECT_EQ(500000000, d.nsec);

  ASSERT_EQ(GCHRON_OK, parse_iso("PT1.25S", &d));
  EXPECT_EQ(1, d.seconds);
  EXPECT_EQ(250000000, d.nsec);

  // Negative sub-second durations normalise the way an instant does.
  ASSERT_EQ(GCHRON_OK, parse_iso("-PT0.5S", &d));
  EXPECT_EQ(-1, d.seconds);
  EXPECT_EQ(500000000, d.nsec);
  EXPECT_EQ(-1, gchron_duration_sign(&d));
}

// A fraction on a calendar unit would mean choosing a length for a year.
TEST(Iso8601Duration, AFractionOnACalendarUnitIsRefused) {
  GCHRON_Duration d{};
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, parse_iso("P1.5Y", &d));
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, parse_iso("P0.5M", &d));
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, parse_iso("P1.5D", &d));
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, parse_iso("PT1.5H", &d));
}

TEST(Iso8601Duration, TheGrammarsEdgesAreRefused) {
  GCHRON_Duration d{};
  const char * bad[] = {
    "", "P", "PT", "P1YT", "1Y", "P1", "PT1H1H", "P1D1Y", "PT1S1M",
    "P1D ", " P1D", "P1D\n", "P1e2D", "PT.5S", "PTS",
  };
  for (const char * text : bad) {
    EXPECT_NE(GCHRON_OK, parse_iso(text, &d)) << "\"" << text << "\"";
  }
  // Components of disagreeing sign are refused by the *type*, not the
  // grammar: a duration has one sign (design.md section 4.1).
  GCHRON_Error err{};
  EXPECT_EQ(GCHRON_ERR_INVALID, parse_iso("P1M-1D", &d, &err));
  EXPECT_EQ(GCHRON_DIAG_DURATION_SIGN, err.diag);
}

TEST(Iso8601Duration, OnlyTheLeadingSignIsProduced) {
  GCHRON_Duration d{};
  GCHRON_Result result;
  ASSERT_EQ(GCHRON_OK, parse_iso("P-1D", &d));
  EXPECT_EQ("-P1D", iso(d, &result));
  EXPECT_EQ(GCHRON_OK, result);
  ASSERT_EQ(GCHRON_OK, parse_iso("-P1D", &d));
  EXPECT_EQ("-P1D", iso(d, &result));
}

TEST(Iso8601Duration, ParseOfWriteIsTheIdentity) {
  const char * texts[] = {
    "PT0S", "P1Y", "P1Y2M3DT4H5M6S", "PT0.5S", "-PT0.5S", "P3W", "-P1Y2M",
    "PT1H30M", "P1DT12H", "PT0.123456789S", "-P1Y2M3DT4H5M6.5S",
  };
  for (const char * text : texts) {
    GCHRON_Duration d{};
    ASSERT_EQ(GCHRON_OK, parse_iso(text, &d)) << text;
    GCHRON_Result result;
    EXPECT_EQ(std::string(text), iso(d, &result)) << text;
    EXPECT_EQ(GCHRON_OK, result) << text;
  }
}

TEST(Iso8601Duration, WeeksAreWrittenAloneOrNotAtAll) {
  GCHRON_Duration d{};
  d.weeks = 1;
  d.days = 1;
  GCHRON_Result result;
  iso(d, &result);
  // Turning a week into seven days would be rewriting the caller's units.
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, result);
}

TEST(Iso8601Duration, AComponentTooLargeToHoldIsRangeNotFormat) {
  GCHRON_Duration d{};
  EXPECT_EQ(GCHRON_ERR_RANGE,
      parse_iso("P999999999999999999999999999999D", &d));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
