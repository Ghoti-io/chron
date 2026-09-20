/**
 * @file
 *
 * The tabular calendar engine.
 *
 * design.md section 5.5 promises this test by name: the Gregorian and Julian
 * calendars are built *as tabular calendars* and proved identical to the
 * shipped closed-form ones over the supported year range. That is what says
 * the engine is right, and it costs nothing to write - the shipped calendars
 * are already checked against outside oracles, so anything the tabular engine
 * reproduces exactly is checked against those oracles too.
 *
 * The rest is a calendar no world has: ten months of thirty-six days and a
 * five-day festival, which is the case mistake M19 is about. A library whose
 * set of calendars is closed cannot have it at all.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/chron/calendar.h>
#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

/** A leap-rule bitmap for a cycle, from a predicate. */
template <typename Predicate>
std::vector<uint8_t> pattern_for(int cycle_years, Predicate is_leap) {
  std::vector<uint8_t> bits(static_cast<size_t>((cycle_years + 7) / 8), 0);
  for (int year = 0; year < cycle_years; ++year) {
    if (is_leap(year)) {
      bits[static_cast<size_t>(year / 8)] |=
          static_cast<uint8_t>(1u << (year % 8));
    }
  }
  return bits;
}

/** The epoch day of year 0, month 1, day 1 in a shipped calendar. */
int64_t anchor_of(const GCHRON_Calendar * calendar) {
  GCHRON_Date origin{ 0, 1, 1 };
  int64_t day = 0;
  EXPECT_EQ(GCHRON_OK,
      gchron_calendar_to_epoch_day(calendar, &origin, &day));
  return day;
}

/**
 * Check that a tabular calendar agrees with a shipped one over a span of
 * days, in both directions.
 */
void expect_identical(const GCHRON_Calendar * shipped,
    const GCHRON_Calendar * tabular, int64_t first, int64_t last) {
  int64_t mismatches = 0;
  for (int64_t day = first; day <= last; ++day) {
    GCHRON_Date a{};
    GCHRON_Date b{};
    ASSERT_EQ(GCHRON_OK, gchron_calendar_from_epoch_day(shipped, day, &a))
        << "epoch day " << day;
    ASSERT_EQ(GCHRON_OK, gchron_calendar_from_epoch_day(tabular, day, &b))
        << "epoch day " << day;
    if (gchron_date_compare(&a, &b) != 0) {
      if (mismatches < 5) {
        ADD_FAILURE() << "epoch day " << day << ": shipped " << a.year << "-"
                      << static_cast<int>(a.month) << "-"
                      << static_cast<int>(a.day) << ", tabular " << b.year
                      << "-" << static_cast<int>(b.month) << "-"
                      << static_cast<int>(b.day);
      }
      ++mismatches;
      continue;
    }
    int64_t back = 0;
    ASSERT_EQ(GCHRON_OK, gchron_calendar_to_epoch_day(tabular, &b, &back));
    if (back != day) {
      if (mismatches < 5) {
        ADD_FAILURE() << "epoch day " << day << " round-tripped to " << back;
      }
      ++mismatches;
    }
  }
  EXPECT_EQ(0, mismatches);
}

} // namespace

TEST(TabularCalendar, TheGregorianCalendarDescribedRatherThanCoded) {
  static const uint16_t months[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };
  std::vector<uint8_t> bits = pattern_for(400, [](int year) {
    return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
  });

  GCHRON_TabularCalendar description{};
  description.id = "tabular:gregory";
  description.month_count = 12;
  description.month_days = months;
  description.leap_month = 2;
  description.leap_days = 1;
  description.leap_rule.cycle_years = 400;
  description.leap_rule.pattern = bits.data();
  description.leap_rule.pattern_bytes = bits.size();
  description.epoch_day_of_year_zero = anchor_of(nullptr);
  description.days_in_week = 7;
  description.week_epoch_day = -3;

  GCHRON_Calendar * tabular = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_tabular(&description, nullptr, &tabular));

  // Roughly +/- 1,100 years around the epoch, which crosses three 400-year
  // cycles and every century rule inside them.
  expect_identical(nullptr, tabular, -400000, 400000);

  // And the derived queries agree too.
  for (int32_t year : { -400, -100, 0, 1, 1582, 1700, 1900, 2000, 2026 }) {
    bool a = false;
    bool b = false;
    int la = 0;
    int lb = 0;
    ASSERT_EQ(GCHRON_OK, gchron_calendar_is_leap_year(nullptr, year, &a));
    ASSERT_EQ(GCHRON_OK, gchron_calendar_is_leap_year(tabular, year, &b));
    EXPECT_EQ(a, b) << year;
    ASSERT_EQ(GCHRON_OK, gchron_calendar_days_in_year(nullptr, year, &la));
    ASSERT_EQ(GCHRON_OK, gchron_calendar_days_in_year(tabular, year, &lb));
    EXPECT_EQ(la, lb) << year;
    for (int month = 1; month <= 12; ++month) {
      ASSERT_EQ(GCHRON_OK,
          gchron_calendar_days_in_month(nullptr, year, month, &la));
      ASSERT_EQ(GCHRON_OK,
          gchron_calendar_days_in_month(tabular, year, month, &lb));
      EXPECT_EQ(la, lb) << year << "-" << month;
    }
  }
  gchron_calendar_destroy(tabular);
}

TEST(TabularCalendar, TheJulianCalendarDescribedRatherThanCoded) {
  static const uint16_t months[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };
  // Every fourth year, with no century rule: a cycle of four with one bit.
  std::vector<uint8_t> bits =
      pattern_for(4, [](int year) { return year % 4 == 0; });

  GCHRON_TabularCalendar description{};
  description.id = "tabular:julian";
  description.month_count = 12;
  description.month_days = months;
  description.leap_month = 2;
  description.leap_days = 1;
  description.leap_rule.cycle_years = 4;
  description.leap_rule.pattern = bits.data();
  description.leap_rule.pattern_bytes = bits.size();
  description.epoch_day_of_year_zero = anchor_of(gchron_calendar_julian());
  description.days_in_week = 7;
  description.week_epoch_day = -3;

  GCHRON_Calendar * tabular = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_tabular(&description, nullptr, &tabular));
  expect_identical(gchron_calendar_julian(), tabular, -400000, 400000);
  gchron_calendar_destroy(tabular);
}

/*
 * Mistake M19: a library whose set of calendars is closed cannot have this
 * one at all. Ten months of thirty-six days and a five-day festival, with a
 * leap rule nobody on Earth uses.
 */
TEST(TabularCalendar, ACalendarNoWorldHas) {
  static const uint16_t months[11] = {
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 5
  };
  // A leap day every fourth year except every hundredth - the designer's own
  // rule, which happens to be simpler than the Gregorian one.
  std::vector<uint8_t> bits = pattern_for(100, [](int year) {
    return year % 4 == 0 && year != 0;
  });

  GCHRON_TabularCalendar description{};
  description.id = "tabular:shire";
  description.month_count = 11;
  description.month_days = months;
  description.leap_month = 11;
  description.leap_days = 1;
  description.leap_rule.cycle_years = 100;
  description.leap_rule.pattern = bits.data();
  description.leap_rule.pattern_bytes = bits.size();
  description.epoch_day_of_year_zero = 0;
  description.days_in_week = 5;  // and a five-day week, because why not
  description.week_epoch_day = 0;

  GCHRON_Calendar * shire = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_calendar_tabular(&description, nullptr,
      &shire));

  EXPECT_STREQ("tabular:shire", gchron_calendar_id(shire));
  int months_count = 0;
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_months_in_year(shire, 0, &months_count));
  EXPECT_EQ(11, months_count);

  int length = 0;
  ASSERT_EQ(GCHRON_OK, gchron_calendar_days_in_year(shire, 1, &length));
  EXPECT_EQ(365, length) << "ten months of thirty-six and a festival of five";
  ASSERT_EQ(GCHRON_OK, gchron_calendar_days_in_year(shire, 4, &length));
  EXPECT_EQ(366, length);
  ASSERT_EQ(GCHRON_OK, gchron_calendar_days_in_year(shire, 100, &length));
  EXPECT_EQ(365, length) << "every hundredth is not a leap year";

  // Year 0, month 1, day 1 is the epoch by construction.
  GCHRON_Date origin{ 0, 1, 1 };
  int64_t day = 0;
  ASSERT_EQ(GCHRON_OK, gchron_calendar_to_epoch_day(shire, &origin, &day));
  EXPECT_EQ(0, day);

  // A round trip over a long span, and the week counted five days at a time.
  for (int64_t probe = -200000; probe <= 200000; probe += 7) {
    GCHRON_Date date{};
    int64_t back = 0;
    ASSERT_EQ(GCHRON_OK,
        gchron_calendar_from_epoch_day(shire, probe, &date)) << probe;
    ASSERT_EQ(GCHRON_OK, gchron_calendar_to_epoch_day(shire, &date, &back));
    ASSERT_EQ(probe, back) << probe;
    ASSERT_GE(date.month, 1);
    ASSERT_LE(date.month, 11);

    int weekday = 0;
    ASSERT_EQ(GCHRON_OK,
        gchron_calendar_day_of_week(shire, &date, &weekday));
    ASSERT_GE(weekday, 1);
    ASSERT_LE(weekday, 5);
  }

  // Converting into it and back out of the Gregorian calendar.
  GCHRON_Date today = gchrontest::date(2026, 9, 20);
  GCHRON_Date in_shire{};
  GCHRON_Date back{};
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_convert(nullptr, &today, shire, &in_shire));
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_convert(shire, &in_shire, nullptr, &back));
  EXPECT_EQ(0, gchron_date_compare(&today, &back));

  gchron_calendar_destroy(shire);
}

TEST(TabularCalendar, ADescriptionThatIsNotACalendarIsRefused) {
  static const uint16_t months[2] = { 30, 30 };
  static const uint16_t zero_month[2] = { 30, 0 };
  std::vector<uint8_t> bits = pattern_for(4, [](int y) { return y == 0; });

  GCHRON_TabularCalendar good{};
  good.id = "tabular:test";
  good.month_count = 2;
  good.month_days = months;
  good.leap_rule.cycle_years = 4;
  good.leap_rule.pattern = bits.data();
  good.leap_rule.pattern_bytes = bits.size();
  good.days_in_week = 7;

  GCHRON_Calendar * calendar = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_calendar_tabular(&good, nullptr, &calendar));
  gchron_calendar_destroy(calendar);

  struct Case { GCHRON_TabularCalendar description; const char * why; };
  GCHRON_TabularCalendar bad;

  bad = good;
  bad.month_count = 0;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_calendar_tabular(&bad, nullptr, &calendar)) << "no months";

  bad = good;
  bad.month_days = zero_month;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_calendar_tabular(&bad, nullptr, &calendar))
      << "a month of no days would make the month walk ambiguous";

  bad = good;
  bad.leap_rule.cycle_years = 0;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_calendar_tabular(&bad, nullptr, &calendar)) << "no cycle";

  bad = good;
  bad.leap_rule.cycle_years = GCHRON_LEAP_CYCLE_MAX + 1;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_calendar_tabular(&bad, nullptr, &calendar)) << "cycle too long";

  bad = good;
  bad.leap_rule.pattern_bytes = 0;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_calendar_tabular(&bad, nullptr, &calendar))
      << "a pattern too short for its own cycle";

  bad = good;
  bad.days_in_week = 0;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_calendar_tabular(&bad, nullptr, &calendar)) << "no week";

  bad = good;
  bad.leap_month = 3;
  bad.leap_days = 1;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_calendar_tabular(&bad, nullptr, &calendar))
      << "a leap month past the end of the year";

  bad = good;
  bad.month_count = GCHRON_TABULAR_MONTH_MAX + 1;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_calendar_tabular(&bad, nullptr, &calendar)) << "too many months";

  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_calendar_tabular(nullptr, nullptr, &calendar));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_calendar_tabular(&good, nullptr, nullptr));
}

// The description is copied, so a caller may build one on the stack and let
// it go.
TEST(TabularCalendar, TheDescriptionIsCopied) {
  GCHRON_Calendar * calendar = nullptr;
  {
    std::vector<uint16_t> months = { 10, 20 };
    std::vector<uint8_t> bits = pattern_for(2, [](int y) { return y == 0; });
    std::string id = "tabular:scoped";
    GCHRON_TabularCalendar description{};
    description.id = id.c_str();
    description.month_count = 2;
    description.month_days = months.data();
    description.leap_rule.cycle_years = 2;
    description.leap_rule.pattern = bits.data();
    description.leap_rule.pattern_bytes = bits.size();
    description.days_in_week = 7;
    ASSERT_EQ(GCHRON_OK,
        gchron_calendar_tabular(&description, nullptr, &calendar));
  }
  // Everything the description pointed at is gone; the calendar still works.
  EXPECT_STREQ("tabular:scoped", gchron_calendar_id(calendar));
  int length = 0;
  ASSERT_EQ(GCHRON_OK, gchron_calendar_days_in_month(calendar, 5, 2,
      &length));
  EXPECT_EQ(20, length);
  gchron_calendar_destroy(calendar);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
