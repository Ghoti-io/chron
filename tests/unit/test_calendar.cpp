/**
 * @file
 *
 * The Julian and hybrid calendars, against `convertdate`'s implementation of
 * Reingold and Dershowitz's algorithms.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/chron/calendar.h>
#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

std::ifstream open_vectors(const std::string & name) {
  std::string path = gchrontest::data_dir() + "/vectors/calendar/" + name;
  std::ifstream in(path);
  // A missing corpus fails rather than skips (design.md section 12.3).
  EXPECT_TRUE(in.is_open()) << "missing vector file: " << path;
  return in;
}

/** Parse `1582-10-04`, which may carry a one- to four-digit year. */
bool parse_label(const std::string & text, GCHRON_Date * out) {
  size_t first = text.find('-');
  if (first == std::string::npos) {
    return false;
  }
  size_t second = text.find('-', first + 1);
  if (second == std::string::npos) {
    return false;
  }
  out->year = static_cast<int32_t>(std::stol(text.substr(0, first)));
  out->month =
      static_cast<uint8_t>(std::stoi(text.substr(first + 1, second - first - 1)));
  out->day = static_cast<uint8_t>(std::stoi(text.substr(second + 1)));
  return true;
}

} // namespace

TEST(Calendar, TheDefaultIsGregorianAndNullMeansIt) {
  EXPECT_STREQ("gregory", gchron_calendar_id(nullptr));
  EXPECT_STREQ("gregory", gchron_calendar_id(gchron_calendar_gregorian()));
  EXPECT_STREQ("julian", gchron_calendar_id(gchron_calendar_julian()));

  GCHRON_Date a{};
  GCHRON_Date b{};
  ASSERT_EQ(GCHRON_OK, gchron_calendar_from_epoch_day(nullptr, 0, &a));
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_from_epoch_day(gchron_calendar_gregorian(), 0, &b));
  EXPECT_EQ(0, gchron_date_compare(&a, &b));
  EXPECT_EQ(1970, a.year);
}

TEST(Calendar, JulianAgreesWithConvertdateOnEveryVector) {
  std::ifstream in = open_vectors("julian.vec");
  ASSERT_TRUE(in.is_open());
  const GCHRON_Calendar * julian = gchron_calendar_julian();

  std::string line;
  int rows = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    size_t tab = line.find('\t');
    ASSERT_NE(std::string::npos, tab) << line;
    int64_t epoch_day = std::strtoll(line.substr(0, tab).c_str(), nullptr, 10);
    GCHRON_Date expected{};
    ASSERT_TRUE(parse_label(line.substr(tab + 1), &expected)) << line;

    GCHRON_Date actual{};
    ASSERT_EQ(GCHRON_OK,
        gchron_calendar_from_epoch_day(julian, epoch_day, &actual))
        << "epoch day " << epoch_day;
    EXPECT_EQ(0, gchron_date_compare(&expected, &actual))
        << "epoch day " << epoch_day << ": expected " << expected.year << "-"
        << static_cast<int>(expected.month) << "-"
        << static_cast<int>(expected.day) << " got " << actual.year << "-"
        << static_cast<int>(actual.month) << "-"
        << static_cast<int>(actual.day);

    int64_t back = 0;
    ASSERT_EQ(GCHRON_OK,
        gchron_calendar_to_epoch_day(julian, &actual, &back));
    EXPECT_EQ(epoch_day, back);
    ++rows;
  }
  EXPECT_GT(rows, 35000);
  std::printf("[          ] %d Julian vectors checked\n", rows);
}

// The two rules disagree at every century that is not a multiple of 400, and
// that is where a reader which confused them shows it.
TEST(Calendar, TheJulianLeapRuleHasNoCenturyException) {
  bool leap = false;
  for (int32_t year : { 1700, 1800, 1900, 2100 }) {
    ASSERT_EQ(GCHRON_OK,
        gchron_calendar_is_leap_year(gchron_calendar_julian(), year, &leap));
    EXPECT_TRUE(leap) << year << " is a Julian leap year";
    ASSERT_EQ(GCHRON_OK, gchron_calendar_is_leap_year(nullptr, year, &leap));
    EXPECT_FALSE(leap) << year << " is not a Gregorian leap year";
  }
  for (int32_t year : { 1600, 2000, 2400 }) {
    ASSERT_EQ(GCHRON_OK,
        gchron_calendar_is_leap_year(gchron_calendar_julian(), year, &leap));
    EXPECT_TRUE(leap);
    ASSERT_EQ(GCHRON_OK, gchron_calendar_is_leap_year(nullptr, year, &leap));
    EXPECT_TRUE(leap);
  }

  // Proleptic, and astronomical: year 0 exists, year -4 is a leap year in
  // both, and the Julian rule applies to them with no century exception.
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_is_leap_year(gchron_calendar_julian(), -100, &leap));
  EXPECT_TRUE(leap);
  ASSERT_EQ(GCHRON_OK, gchron_calendar_is_leap_year(nullptr, -100, &leap));
  EXPECT_FALSE(leap);
}

TEST(Calendar, ConvertingBetweenCalendarsGoesThroughTheEpochDay) {
  // The day the Gregorian calendar began in Rome, in both labellings.
  GCHRON_Date gregorian = gchrontest::date(1582, 10, 15);
  GCHRON_Date as_julian{};
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_convert(nullptr, &gregorian, gchron_calendar_julian(),
          &as_julian));
  EXPECT_EQ(1582, as_julian.year);
  EXPECT_EQ(10, as_julian.month);
  EXPECT_EQ(5, as_julian.day);

  GCHRON_Date back{};
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_convert(gchron_calendar_julian(), &as_julian, nullptr,
          &back));
  EXPECT_EQ(0, gchron_date_compare(&gregorian, &back));

  // Today's thirteen-day difference.
  GCHRON_Date today = gchrontest::date(2026, 9, 20);
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_convert(nullptr, &today, gchron_calendar_julian(),
          &as_julian));
  EXPECT_EQ(7, as_julian.day);
  EXPECT_EQ(9, as_julian.month);
}

// The days of the week ran on through the reform - the one thing about
// October 1582 that did not change - so a calendar's labelling does not move
// them.
TEST(Calendar, TheWeekIsTheSameWhicheverCalendarLabelsTheDay) {
  GCHRON_Date gregorian = gchrontest::date(1582, 10, 15);
  GCHRON_Date julian_label{ 1582, 10, 5 };
  int a = 0;
  int b = 0;
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_day_of_week(nullptr, &gregorian, &a));
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_day_of_week(gchron_calendar_julian(), &julian_label,
          &b));
  EXPECT_EQ(a, b);
  EXPECT_EQ(5, a) << "1582-10-15 was a Friday";
}

/*
 * The hybrid calendar, and the days the reform deleted. Mistake M21: applying
 * the proleptic Gregorian calendar to a date that predates it is what every
 * "historical" date computed in a database gets wrong.
 */
TEST(Calendar, TheHybridCalendarAgreesWithConvertdateAroundEveryCutover) {
  std::ifstream in = open_vectors("hybrid.vec");
  ASSERT_TRUE(in.is_open());

  struct Named { const char * name; int64_t cutover; };
  const Named cutovers[] = {
    { "rome", GCHRON_CUTOVER_ROME },
    { "britain", GCHRON_CUTOVER_BRITAIN },
    { "russia", GCHRON_CUTOVER_RUSSIA },
  };

  std::string line;
  int rows = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream fields(line);
    std::string name;
    std::string which;
    std::string label;
    long long epoch_day = 0;
    ASSERT_TRUE(std::getline(fields, name, '\t'));
    fields >> epoch_day;
    fields.ignore(1);
    ASSERT_TRUE(std::getline(fields, which, '\t'));
    ASSERT_TRUE(std::getline(fields, label));

    int64_t cutover = 0;
    for (const Named & entry : cutovers) {
      if (name == entry.name) {
        cutover = entry.cutover;
      }
    }
    ASSERT_NE(0, cutover) << line;

    GCHRON_Calendar * hybrid = nullptr;
    ASSERT_EQ(GCHRON_OK,
        gchron_calendar_hybrid(cutover, nullptr, &hybrid));

    GCHRON_Date expected{};
    ASSERT_TRUE(parse_label(label, &expected)) << line;
    GCHRON_Date actual{};
    ASSERT_EQ(GCHRON_OK,
        gchron_calendar_from_epoch_day(hybrid, epoch_day, &actual)) << line;
    EXPECT_EQ(0, gchron_date_compare(&expected, &actual)) << line;

    int64_t back = 0;
    ASSERT_EQ(GCHRON_OK, gchron_calendar_to_epoch_day(hybrid, &actual, &back))
        << line;
    EXPECT_EQ(epoch_day, back) << line;

    gchron_calendar_destroy(hybrid);
    ++rows;
  }
  EXPECT_EQ(120, rows);
}

TEST(Calendar, TheDaysAReformDeletedAreAGapAndNotADate) {
  GCHRON_Calendar * rome = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_hybrid(GCHRON_CUTOVER_ROME, nullptr, &rome));

  // 5 to 14 October 1582 did not happen in Rome.
  for (int day = 5; day <= 14; ++day) {
    GCHRON_Date missing{ 1582, 10, static_cast<uint8_t>(day) };
    int64_t epoch_day = 0;
    // GCHRON_ERR_GAP, the same result a daylight-saving gap gives, because it
    // is the same question: a label that names no day.
    EXPECT_EQ(GCHRON_ERR_GAP,
        gchron_calendar_to_epoch_day(rome, &missing, &epoch_day))
        << "1582-10-" << day;
    EXPECT_FALSE(gchron_calendar_date_is_valid(rome, &missing));
  }
  // The days either side of the hole do exist.
  for (int day : { 1, 2, 3, 4, 15, 16, 31 }) {
    GCHRON_Date present{ 1582, 10, static_cast<uint8_t>(day) };
    int64_t epoch_day = 0;
    EXPECT_EQ(GCHRON_OK,
        gchron_calendar_to_epoch_day(rome, &present, &epoch_day))
        << "1582-10-" << day;
  }

  // And the cut-over year is genuinely short.
  int length = 0;
  ASSERT_EQ(GCHRON_OK, gchron_calendar_days_in_year(rome, 1582, &length));
  EXPECT_EQ(355, length) << "1582 lost ten days in Rome";
  ASSERT_EQ(GCHRON_OK, gchron_calendar_days_in_year(rome, 1583, &length));
  EXPECT_EQ(365, length);

  gchron_calendar_destroy(rome);
}

TEST(Calendar, EachCountryLostADifferentNumberOfDays) {
  struct Case { int64_t cutover; int32_t year; int length; const char * why; };
  const Case cases[] = {
    { GCHRON_CUTOVER_ROME, 1582, 355, "Rome lost ten days" },
    { GCHRON_CUTOVER_BRITAIN, 1752, 355, "Britain lost eleven, from a "
                                         "leap year" },
    { GCHRON_CUTOVER_RUSSIA, 1918, 352, "Russia lost thirteen" },
  };
  for (const Case & c : cases) {
    GCHRON_Calendar * hybrid = nullptr;
    ASSERT_EQ(GCHRON_OK, gchron_calendar_hybrid(c.cutover, nullptr, &hybrid));
    int length = 0;
    ASSERT_EQ(GCHRON_OK,
        gchron_calendar_days_in_year(hybrid, c.year, &length));
    EXPECT_EQ(c.length, length) << c.why;
    gchron_calendar_destroy(hybrid);
  }
}

// Newton was born on Christmas Day 1642 in England and on 4 January 1643 in
// Rome, and both are true. This is the whole reason the cut-over is a
// parameter.
TEST(Calendar, TheSameDayHasDifferentLabelsInDifferentCountries) {
  GCHRON_Calendar * britain = nullptr;
  GCHRON_Calendar * rome = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_hybrid(GCHRON_CUTOVER_BRITAIN, nullptr, &britain));
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_hybrid(GCHRON_CUTOVER_ROME, nullptr, &rome));

  GCHRON_Date english{ 1642, 12, 25 };
  int64_t epoch_day = 0;
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_to_epoch_day(britain, &english, &epoch_day));

  GCHRON_Date roman{};
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_from_epoch_day(rome, epoch_day, &roman));
  EXPECT_EQ(1643, roman.year);
  EXPECT_EQ(1, roman.month);
  EXPECT_EQ(4, roman.day);

  gchron_calendar_destroy(britain);
  gchron_calendar_destroy(rome);
}

// The leap day is in February, so the calendar that decides whether there is
// one is the one in force just after it.
TEST(Calendar, TheHybridLeapRuleFollowsWhicheverCalendarWasInForce) {
  GCHRON_Calendar * britain = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_calendar_hybrid(GCHRON_CUTOVER_BRITAIN, nullptr, &britain));
  bool leap = false;

  // Britain adopted in September 1752, so February 1700 was still Julian and
  // 1700 was a leap year there.
  ASSERT_EQ(GCHRON_OK, gchron_calendar_is_leap_year(britain, 1700, &leap));
  EXPECT_TRUE(leap);
  // 1800 was after the change, and was not.
  ASSERT_EQ(GCHRON_OK, gchron_calendar_is_leap_year(britain, 1800, &leap));
  EXPECT_FALSE(leap);

  gchron_calendar_destroy(britain);
}

TEST(Calendar, DestroyIgnoresTheStaticCalendarsAndNull) {
  // A caller holding "whichever calendar the user chose" should not have to
  // remember which kind it is.
  gchron_calendar_destroy(nullptr);
  gchron_calendar_destroy(
      const_cast<GCHRON_Calendar *>(gchron_calendar_gregorian()));
  gchron_calendar_destroy(
      const_cast<GCHRON_Calendar *>(gchron_calendar_julian()));
  // Still usable afterwards.
  EXPECT_STREQ("gregory", gchron_calendar_id(gchron_calendar_gregorian()));
}

TEST(Calendar, DumpWritesSomethingForEveryInput) {
  FILE * sink = std::tmpfile();
  ASSERT_NE(nullptr, sink);
  gchron_calendar_dump(nullptr, sink);
  gchron_calendar_dump(gchron_calendar_julian(), sink);
  gchron_calendar_dump(gchron_calendar_julian(), nullptr);
  EXPECT_GT(std::ftell(sink), 0);
  std::fclose(sink);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
