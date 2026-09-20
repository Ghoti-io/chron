/**
 * @file
 *
 * The `strftime` lowering, checked against glibc's own `strftime` in the C
 * locale.
 *
 * design.md section 12 names this differential. It runs in-process rather
 * than through a driver pair, because the oracle is a function in libc and
 * the only thing that needs arranging is the locale - which is already C,
 * and which this test asserts rather than assumes.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <clocale>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>
#include <ghoti.io/chron/format.h>

#include "test_helpers.h"

namespace {

/**
 * The specifiers this library implements, one pattern each.
 *
 * `%U` and `%W` are absent because this library refuses them: they count
 * weeks from the first Sunday or Monday of the year with the days before in
 * "week 0", neither is the ISO week, and a caller who wants a week number
 * almost always wants `%V`. A test for that refusal is below, so the absence
 * here is stated rather than an oversight.
 */
const char * const SPECIFIERS[] = {
  "%a", "%A", "%b", "%B", "%C", "%d", "%D", "%e", "%F", "%g", "%G", "%h",
  "%H", "%I", "%j", "%k", "%l", "%m", "%M", "%n", "%p", "%r", "%R", "%S",
  "%t", "%T", "%u", "%V", "%w", "%x", "%X", "%y", "%Y", "%%",
  "%Y-%m-%dT%H:%M:%S",
  "%a %b %e %H:%M:%S %Y",
  "week %V of %G, day %u",
  "[%d/%b/%Y:%H:%M:%S]",
};

struct Moment {
  int32_t year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
};

/**
 * Moments chosen where the answers move: either side of noon and midnight,
 * the first and last days of a year, a leap day, and the New Year's Eves
 * where the ISO week-year is not the calendar year.
 */
const Moment MOMENTS[] = {
  { 2026, 6, 15, 12, 0, 0 },
  { 2026, 1, 1, 0, 0, 0 },
  { 2026, 12, 31, 23, 59, 59 },
  { 2027, 1, 1, 0, 0, 0 },   // a Friday: ISO week 53 of 2026
  { 2024, 2, 29, 13, 5, 9 },
  { 2021, 1, 1, 1, 1, 1 },   // a Friday: ISO week 53 of 2020
  { 2019, 12, 30, 7, 0, 0 }, // a Monday: ISO week 1 of 2020
  { 1970, 1, 1, 0, 0, 0 },
  { 1999, 12, 31, 23, 0, 0 },
  { 2000, 2, 29, 0, 30, 0 },
  { 1900, 1, 1, 6, 0, 0 },
  { 2038, 1, 19, 3, 14, 7 },
};

/** What glibc says, for a UTC civil reading. */
bool ask_glibc(const char * pattern, const Moment & m, std::string * out) {
  std::tm parts{};
  char buffer[512];
  size_t length;

  parts.tm_year = static_cast<int>(m.year) - 1900;
  parts.tm_mon = m.month - 1;
  parts.tm_mday = m.day;
  parts.tm_hour = m.hour;
  parts.tm_min = m.minute;
  parts.tm_sec = m.second;
  parts.tm_isdst = 0;

  // timegm normalises and fills in tm_wday and tm_yday, which several
  // specifiers need and which a hand-filled struct does not have.
  std::time_t when = ::timegm(&parts);
  if (when == static_cast<std::time_t>(-1)) {
    return false;
  }
  if (::gmtime_r(&when, &parts) == nullptr) {
    return false;
  }
  parts.tm_gmtoff = 0;
  parts.tm_zone = "UTC";

  length = std::strftime(buffer, sizeof(buffer), pattern, &parts);
  if (length == 0 && pattern[0] != '\0' && std::strcmp(pattern, "%n") != 0
      && std::strcmp(pattern, "%t") != 0) {
    return false;
  }
  *out = std::string(buffer, length);
  return true;
}

} // namespace

TEST(Strftime, TheCLocaleIsWhatThisTestAssumes) {
  // A differential against a locale-dependent function has to say which
  // locale, and this library never calls setlocale at all - so the C locale
  // is what a process gets unless something else changed it.
  const char * current = std::setlocale(LC_TIME, nullptr);
  ASSERT_NE(nullptr, current);
  EXPECT_TRUE(std::strcmp(current, "C") == 0
      || std::strcmp(current, "POSIX") == 0)
      << "LC_TIME is " << current << "; this differential needs the C locale";
}

TEST(Strftime, EverySpecifierAgreesWithGlibc) {
  size_t checked = 0;
  for (const char * pattern : SPECIFIERS) {
    GCHRON_Format * format = nullptr;
    ASSERT_EQ(GCHRON_OK,
        gchron_format_compile(pattern, std::strlen(pattern),
            GCHRON_FORMAT_STRFTIME, nullptr, nullptr, &format, nullptr))
        << pattern;

    for (const Moment & m : MOMENTS) {
      std::string expected;
      if (!ask_glibc(pattern, m, &expected)) {
        continue;
      }

      GCHRON_DateTime dt =
          gchrontest::datetime(m.year, m.month, m.day, m.hour, m.minute,
              m.second);
      GCHRON_OffsetDateTime odt{};
      ASSERT_EQ(GCHRON_OK, gchron_offset_create(&dt, 0, false, &odt));
      GCHRON_FormatContext context{};
      context.abbreviation = "UTC";

      char buffer[512];
      size_t length = 0;
      ASSERT_EQ(GCHRON_OK,
          gchron_format_offset(format, &odt, &context, buffer,
              sizeof(buffer), &length))
          << pattern << " at " << m.year << "-" << m.month << "-" << m.day;

      EXPECT_EQ(expected, std::string(buffer, length))
          << "pattern " << pattern << " at " << m.year << "-" << m.month
          << "-" << m.day << " " << m.hour << ":" << m.minute;
      ++checked;
    }
    gchron_format_destroy(format);
  }
  std::printf("[          ] %zu strftime comparisons against glibc\n",
      checked);
  EXPECT_GT(checked, 300u);
}

// `%z` and `%Z` need an offset and an abbreviation, which a UTC civil reading
// alone does not carry - so they get their own case with a real zone.
TEST(Strftime, TheZoneSpecifiersAgreeWithGlibcInARealZone) {
  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_system(nullptr, nullptr, &db));
  const GCHRON_Zone * zone = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_zone(db, "America/New_York", &zone));

  const char * patterns[] = { "%z", "%Z", "%Y-%m-%d %H:%M:%S %z (%Z)" };
  const int64_t moments[] = { 1781539200, 1768478400, 1793514600 };

  ::setenv("TZ", "America/New_York", 1);
  ::tzset();

  size_t checked = 0;
  for (const char * pattern : patterns) {
    GCHRON_Format * format = nullptr;
    ASSERT_EQ(GCHRON_OK,
        gchron_format_compile(pattern, std::strlen(pattern),
            GCHRON_FORMAT_STRFTIME, nullptr, nullptr, &format, nullptr));

    for (int64_t seconds : moments) {
      std::tm parts{};
      std::time_t when = static_cast<std::time_t>(seconds);
      ASSERT_NE(nullptr, ::localtime_r(&when, &parts));
      char expected[256];
      size_t length = std::strftime(expected, sizeof(expected), pattern,
          &parts);

      GCHRON_Instant instant{};
      ASSERT_EQ(GCHRON_OK, gchron_instant_create(seconds, 0, &instant));
      GCHRON_ZonedDateTime zoned{};
      ASSERT_EQ(GCHRON_OK,
          gchron_zoned_from_instant(instant, zone, &zoned));

      char actual[256];
      size_t actual_length = 0;
      ASSERT_EQ(GCHRON_OK,
          gchron_format_zoned(format, &zoned, nullptr, actual,
              sizeof(actual), &actual_length));
      EXPECT_EQ(std::string(expected, length),
          std::string(actual, actual_length))
          << pattern << " at " << seconds;
      ++checked;
    }
    gchron_format_destroy(format);
  }
  ::unsetenv("TZ");
  ::tzset();
  gchron_zonedb_destroy(db);
  EXPECT_GT(checked, 5u);
}

/*
 * `%U` and `%W` count weeks from the first Sunday or Monday of the year, with
 * the days before in "week 0". Neither is the ISO week, neither has an LDML
 * letter, and a caller who asks for a week number almost always wants `%V`.
 * Refused rather than approximated - and the refusal is a test so that adding
 * them later is a deliberate act.
 */
TEST(Strftime, TheTwoNonIsoWeekNumbersAreRefusedRatherThanApproximated) {
  GCHRON_Format * format = nullptr;
  GCHRON_Error err{};
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_format_compile("%U", 2, GCHRON_FORMAT_STRFTIME, nullptr, nullptr,
          &format, &err));
  EXPECT_EQ(GCHRON_DIAG_PATTERN_LETTER_UNKNOWN, err.diag);
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_format_compile("%W", 2, GCHRON_FORMAT_STRFTIME, nullptr, nullptr,
          &format, &err));
  // And %V, which is the ISO week, is fine.
  EXPECT_EQ(GCHRON_OK,
      gchron_format_compile("%V", 2, GCHRON_FORMAT_STRFTIME, nullptr, nullptr,
          &format, &err));
  gchron_format_destroy(format);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
