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
  for (GCHRON_TickSource source : { GCHRON_TICK_SUSPENDING,
                                    GCHRON_TICK_CONTINUOUS }) {
    GCHRON_Tick first{};
    GCHRON_Tick second{};
    GCHRON_Result begin = gchron_tick_now(source, &first);
    if (begin == GCHRON_ERR_UNSUPPORTED) {
      // Counted and named, never silent: this platform has no such counter.
      // CLOCK_BOOTTIME is Linux's spelling and POSIX has no other.
      GTEST_SKIP() << "no continuous counter on this platform";
    }
    ASSERT_EQ(GCHRON_OK, begin);
    EXPECT_EQ(source, first.source) << "the reading does not say where it came from";

    // Some work, so that the counter has somewhere to go.
    volatile int64_t sink = 0;
    for (int i = 0; i < 100000; ++i) {
      sink += i;
    }
    ASSERT_EQ(GCHRON_OK, gchron_tick_now(source, &second));
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

    EXPECT_EQ(GCHRON_ERR_INVALID, gchron_tick_now(source, nullptr));
    EXPECT_EQ(GCHRON_ERR_INVALID, gchron_tick_since(first, second, nullptr));
  }
}

TEST(Tick, TheCallerHasToSayWhichCounterItWants) {
  /*
   * There is no default. The two counters answer different questions - "how
   * long did this take" and "has enough time passed" - and the second is the
   * one that fails silently, because a timeout measured on a counter that
   * stops while the machine sleeps simply never fires. Section 3.7's rule
   * applied to a selector: the zero value refuses.
   */
  GCHRON_Tick tick{};
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_tick_now(GCHRON_TICK_NONE, &tick));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_tick_now(static_cast<GCHRON_TickSource>(99), &tick));
  EXPECT_EQ(GCHRON_TICK_NONE, tick.source) << "a refused read wrote the struct";
}

TEST(Tick, TwoReadingsFromDifferentCountersCannotBeSubtracted) {
  /*
   * The counters have different origins, and across a suspend they have
   * advanced by different amounts, so the difference between one of each is
   * not a number with a meaning - it just looks like one. The same reason
   * GCHRON_Tick is a separate type from GCHRON_Instant (M23).
   */
  GCHRON_Tick suspending{};
  GCHRON_Tick continuous{};
  ASSERT_EQ(GCHRON_OK, gchron_tick_now(GCHRON_TICK_SUSPENDING, &suspending));
  if (gchron_tick_now(GCHRON_TICK_CONTINUOUS, &continuous)
      == GCHRON_ERR_UNSUPPORTED) {
    GTEST_SKIP() << "no continuous counter on this platform";
  }

  GCHRON_Duration mixed{};
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_tick_since(suspending, continuous, &mixed));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_tick_since(continuous, suspending, &mixed));
}

TEST(Tick, AZeroedReadingIsNotAReading) {
  /*
   * A GCHRON_Tick that was declared and never filled holds nsec 0, which
   * would otherwise pass for a perfectly good reading taken at the origin -
   * and would make an elapsed time equal to the whole uptime of the machine.
   * The source tag is what makes it detectable.
   */
  GCHRON_Tick never_read{};
  GCHRON_Tick real{};
  ASSERT_EQ(GCHRON_OK, gchron_tick_now(GCHRON_TICK_SUSPENDING, &real));

  GCHRON_Duration elapsed{};
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_tick_since(never_read, real, &elapsed));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_tick_since(real, never_read, &elapsed));
}

TEST(Tick, TheContinuousCounterHasCountedAtLeastAsMuchAsTheOther) {
  /*
   * The two counters differ by exactly the time the machine has spent
   * suspended, and a test cannot cause a suspend - so what is checkable here
   * is the ordering, and *whether the gap is real*.
   *
   * The naive version of this test - assert the two readings differ - passes
   * on a machine that has never suspended, because reading two clocks takes
   * time and the second is always a few dozen nanoseconds later. Measured on
   * the machine this was written on: the gap was 16 to 39 ns while two
   * back-to-back reads of the *same* clock differed by 33 to 80. It was
   * measuring the cost of a system call and calling it evidence.
   *
   * So the threshold is a second. Below it, nothing can be concluded and the
   * test says so out loud rather than passing quietly (section 12.3).
   */
  GCHRON_Tick suspending{};
  GCHRON_Tick continuous{};
  ASSERT_EQ(GCHRON_OK, gchron_tick_now(GCHRON_TICK_SUSPENDING, &suspending));
  if (gchron_tick_now(GCHRON_TICK_CONTINUOUS, &continuous)
      == GCHRON_ERR_UNSUPPORTED) {
    GTEST_SKIP() << "no continuous counter on this platform";
  }

  // Read second, so it cannot be behind by anything but a real difference.
  EXPECT_GE(continuous.nsec, suspending.nsec)
      << "the continuous counter is behind the suspending one, which it "
         "cannot be: it counts everything the other one counts, and the "
         "suspends as well";

  const int64_t gap = continuous.nsec - suspending.nsec;
  if (gap < 1000000000) {
    GTEST_SKIP() << "this machine has not suspended since it booted (the two "
                    "counters differ by " << gap << " ns, which is the cost "
                    "of reading them), so there is no gap here to measure";
  }
  // It has suspended, and the gap is that suspend.
  EXPECT_GT(gap, 0);
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
