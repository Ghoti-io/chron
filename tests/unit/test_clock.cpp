/**
 * @file
 *
 * Clocks and ticks.
 *
 * design.md, mistake M18: "now" called from inside a library is what makes a
 * two-digit-year rule, a certificate-validity check, and anything else that
 * depends on the date untestable. The point of this file is that nothing
 * needs the real clock to be tested.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>
#include <ghoti.io/chron/clock.h>
#include <ghoti.io/chron/format.h>

#include "test_helpers.h"

TEST(Clock, AFixedClockDoesNotMoveAndAllocatesNothing) {
  GCHRON_Instant moment{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1789918200, 123456789, &moment));

  GCHRON_FixedClock clock;
  ASSERT_EQ(GCHRON_OK, gchron_clock_fixed(moment, &clock));

  for (int i = 0; i < 3; ++i) {
    GCHRON_Instant now{};
    ASSERT_EQ(GCHRON_OK, gchron_clock_now(&clock.clock, &now));
    EXPECT_EQ(0, gchron_instant_compare(&moment, &now));
  }

  GCHRON_Duration resolution{};
  ASSERT_EQ(GCHRON_OK,
      gchron_clock_resolution(&clock.clock, &resolution));
  EXPECT_EQ(1, resolution.nsec);
}

TEST(Clock, TheSystemClockAnswersAndSaysHowFinely) {
  const GCHRON_Clock * clock = gchron_clock_system();
  ASSERT_NE(nullptr, clock);

  GCHRON_Instant first{};
  GCHRON_Instant second{};
  ASSERT_EQ(GCHRON_OK, gchron_clock_now(clock, &first));
  ASSERT_EQ(GCHRON_OK, gchron_clock_now(clock, &second));

  EXPECT_TRUE(gchron_instant_is_valid(&first));
  // Some time after 2020 and before 2100, which is as much as a test can say
  // about the wall clock without asserting what year it is.
  EXPECT_GT(first.sec, 1577836800);
  EXPECT_LT(first.sec, 4102444800);
  EXPECT_LE(gchron_instant_compare(&first, &second), 0)
      << "the wall clock may stand still, but a test run should not see it "
         "go backwards";

  GCHRON_Duration resolution{};
  ASSERT_EQ(GCHRON_OK, gchron_clock_resolution(clock, &resolution));
  EXPECT_TRUE(gchron_duration_is_valid(&resolution));
}

// Not a silent fall back to the system clock. A caller who meant the system
// clock says so, and mistake M18 is exactly the library that decided for them.
TEST(Clock, ANullClockIsRefusedRatherThanDefaulted) {
  GCHRON_Instant now{};
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_clock_now(nullptr, &now));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_clock_now(gchron_clock_system(), nullptr));

  GCHRON_Duration resolution{};
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_clock_resolution(nullptr, &resolution));

  // A clock with no resolution function says so rather than guessing.
  GCHRON_Clock bare{};
  bare.now = nullptr;
  bare.resolution = nullptr;
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_clock_resolution(&bare, &resolution));
}

/*
 * Mistake M23: a monotonic reading is not a point on any calendar. It has no
 * epoch, it is not comparable across processes or reboots, and only the
 * difference between two of them means anything - which is why GCHRON_Tick is
 * a separate type with exactly one operation.
 */
TEST(Tick, OnlyTheDifferenceBetweenTwoReadingsMeansAnything) {
  GCHRON_Tick first{};
  GCHRON_Tick second{};
  ASSERT_EQ(GCHRON_OK, gchron_tick_now(&first));
  // Some work, so that the counter has somewhere to go.
  volatile int64_t sink = 0;
  for (int i = 0; i < 100000; ++i) {
    sink += i;
  }
  ASSERT_EQ(GCHRON_OK, gchron_tick_now(&second));
  (void)sink;

  GCHRON_Duration elapsed{};
  ASSERT_EQ(GCHRON_OK, gchron_tick_since(first, second, &elapsed));
  EXPECT_TRUE(gchron_duration_is_valid(&elapsed));
  EXPECT_GE(gchron_duration_sign(&elapsed), 0)
      << "a monotonic counter does not go backwards";

  // And the other way round is negative, which is the only other thing a
  // difference can be.
  GCHRON_Duration backwards{};
  ASSERT_EQ(GCHRON_OK, gchron_tick_since(second, first, &backwards));
  EXPECT_LE(gchron_duration_sign(&backwards), 0);

  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_tick_now(nullptr));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_tick_since(first, second, nullptr));
}

/*
 * The whole point: a rule that depends on the current year is testable,
 * because the year is a parameter. RFC 9110's two-digit-year rule says a
 * timestamp more than fifty years in the future is the most recent past year
 * with the same last two digits - and here is that rule checked from two
 * different "now"s without waiting fifty years for one of them.
 */
TEST(Clock, TheTwoDigitYearRuleIsTestableBecauseNowIsAParameter) {
  const char * text = "Sunday, 06-Nov-94 08:49:37 GMT";
  GCHRON_OffsetDateTime out{};

  // Standing in 2026, "94" is 1994.
  {
    GCHRON_DateTime in_2026 = gchrontest::datetime(2026, 1, 1, 0, 0, 0);
    GCHRON_Instant moment{};
    ASSERT_EQ(GCHRON_OK, gchron_instant_from_utc(&in_2026, &moment));
    GCHRON_FixedClock clock;
    ASSERT_EQ(GCHRON_OK, gchron_clock_fixed(moment, &clock));
    ASSERT_EQ(GCHRON_OK,
        gchron_parse_http_date(text, std::strlen(text), &clock.clock, nullptr,
            &out, nullptr, nullptr));
    EXPECT_EQ(1994, out.civil.date.year);
  }

  // Standing in 2049, "94" is still 1994 - it is fifty-five years back, and
  // 2094 would be forty-five years forward, which the rule permits.
  {
    GCHRON_DateTime in_2049 = gchrontest::datetime(2049, 1, 1, 0, 0, 0);
    GCHRON_Instant moment{};
    ASSERT_EQ(GCHRON_OK, gchron_instant_from_utc(&in_2049, &moment));
    GCHRON_FixedClock clock;
    ASSERT_EQ(GCHRON_OK, gchron_clock_fixed(moment, &clock));
    ASSERT_EQ(GCHRON_OK,
        gchron_parse_http_date(text, std::strlen(text), &clock.clock, nullptr,
            &out, nullptr, nullptr));
    EXPECT_EQ(2094, out.civil.date.year)
        << "in 2049, 2094 is forty-five years away and 1994 is fifty-five";
  }

  // And with no clock at all, the form that needs one is refused rather than
  // guessed at.
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_parse_http_date(text, std::strlen(text), nullptr, nullptr, &out,
          nullptr, nullptr));

  // The four-digit form needs no clock.
  const char * modern = "Sun, 06 Nov 1994 08:49:37 GMT";
  EXPECT_EQ(GCHRON_OK,
      gchron_parse_http_date(modern, std::strlen(modern), nullptr, nullptr,
          &out, nullptr, nullptr));
  EXPECT_EQ(1994, out.civil.date.year);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
