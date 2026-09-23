/**
 * @file
 *
 * Instants, exact arithmetic, and the intervals built on them.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdint>
#include <cstdio>
#include <random>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

GCHRON_Instant instant(int64_t sec, int32_t nsec = 0) {
  GCHRON_Instant i{};
  EXPECT_EQ(GCHRON_OK, gchron_instant_create(sec, nsec, &i));
  return i;
}

GCHRON_Duration exact(int64_t seconds, int32_t nanos = 0) {
  GCHRON_Duration d{};
  EXPECT_EQ(GCHRON_OK,
      gchron_duration_from_exact_seconds(seconds, nanos, &d));
  return d;
}

} // namespace

// The invariant, stated rather than repaired: which way a carry should go for
// a negative second count is exactly what callers get wrong, so the function
// that carries has its own name.
TEST(Instant, CreateRefusesANanosecondOutsideItsRange) {
  GCHRON_Instant i;
  EXPECT_EQ(GCHRON_OK, gchron_instant_create(0, 999999999, &i));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_create(0, 1000000000, &i));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_create(0, -1, &i));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_create(0, 0, nullptr));
}

// java.time's rule: -1ns is {-1, 999999999}, so the nanosecond field is
// always non-negative and the sign lives in the seconds.
TEST(Instant, NormalizeCarriesTheSignIntoTheSeconds) {
  GCHRON_Instant i;
  ASSERT_EQ(GCHRON_OK, gchron_instant_normalize(0, -1, &i));
  EXPECT_EQ(-1, i.sec);
  EXPECT_EQ(999999999, i.nsec);

  ASSERT_EQ(GCHRON_OK, gchron_instant_normalize(0, 1000000001, &i));
  EXPECT_EQ(1, i.sec);
  EXPECT_EQ(1, i.nsec);

  ASSERT_EQ(GCHRON_OK, gchron_instant_normalize(5, -1000000001, &i));
  EXPECT_EQ(3, i.sec);
  EXPECT_EQ(999999999, i.nsec);

  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_instant_normalize(INT64_MAX, INT64_MAX, &i));
}

// The floor division is what makes a pre-epoch instant land on the right day.
// C's truncating `/` would put 1969-12-31T23:59:59Z on day 0.
TEST(Instant, EpochDaySplitFloorsRatherThanTruncating) {
  GCHRON_Instant i = instant(-1);
  int64_t day = 0;
  int64_t nanos = 0;
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_epoch_day(&i, &day, &nanos));
  EXPECT_EQ(-1, day);
  EXPECT_EQ(INT64_C(86399) * GCHRON_NANOS_PER_SECOND, nanos);

  i = instant(0);
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_epoch_day(&i, &day, &nanos));
  EXPECT_EQ(0, day);
  EXPECT_EQ(0, nanos);

  i = instant(-86400);
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_epoch_day(&i, &day, &nanos));
  EXPECT_EQ(-1, day);
  EXPECT_EQ(0, nanos);
}

TEST(Instant, UtcCivilTimeRoundTrips) {
  GCHRON_DateTime dt = gchrontest::datetime(2026, 9, 20, 15, 30, 0, 123456789);
  GCHRON_Instant i;
  GCHRON_DateTime back;
  ASSERT_EQ(GCHRON_OK, gchron_instant_from_utc(&dt, &i));
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&i, &back));
  EXPECT_EQ(0, gchron_datetime_compare(&dt, &back));

  // The epoch itself, and the second before it.
  GCHRON_DateTime epoch = gchrontest::datetime(1970, 1, 1, 0, 0, 0);
  ASSERT_EQ(GCHRON_OK, gchron_instant_from_utc(&epoch, &i));
  EXPECT_EQ(0, i.sec);
  GCHRON_DateTime before = gchrontest::datetime(1969, 12, 31, 23, 59, 59);
  ASSERT_EQ(GCHRON_OK, gchron_instant_from_utc(&before, &i));
  EXPECT_EQ(-1, i.sec);
}

TEST(Instant, UtcCivilTimeRoundTripsOverRandomInstants) {
  std::mt19937_64 rng(20260920);
  // Bounded well inside the supported years so that the conversion is
  // exercised rather than its range check.
  std::uniform_int_distribution<int64_t> secs(
      INT64_C(-70000000000), INT64_C(70000000000));
  std::uniform_int_distribution<int32_t> nsecs(0, 999999999);

  for (int i = 0; i < 100000; ++i) {
    GCHRON_Instant original = instant(secs(rng), nsecs(rng));
    GCHRON_DateTime civil;
    GCHRON_Instant back;
    ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&original, &civil));
    ASSERT_TRUE(gchron_datetime_is_valid(&civil));
    ASSERT_EQ(GCHRON_OK, gchron_instant_from_utc(&civil, &back));
    ASSERT_EQ(0, gchron_instant_compare(&original, &back))
        << original.sec << "." << original.nsec;
  }
}

// design.md section 5.1: the instant {1483228799, 0} is both
// 2016-12-31T23:59:59Z and the leap second 23:59:60Z that followed it,
// because every day in this count is 86,400 seconds long.
TEST(Instant, LeapSecondsAreNotInTheCount) {
  GCHRON_DateTime a = gchrontest::datetime(2016, 12, 31, 23, 59, 59);
  GCHRON_DateTime b = gchrontest::datetime(2017, 1, 1, 0, 0, 0);
  GCHRON_Instant ia;
  GCHRON_Instant ib;
  ASSERT_EQ(GCHRON_OK, gchron_instant_from_utc(&a, &ia));
  ASSERT_EQ(GCHRON_OK, gchron_instant_from_utc(&b, &ib));
  EXPECT_EQ(1483228799, ia.sec);
  // One second apart, though a clock that showed 23:59:60 ticked twice.
  EXPECT_EQ(1, ib.sec - ia.sec);
}

// Mistake M9: a month has no length in seconds, so adding one to a point on
// the timeline is a question with no answer rather than one whose answer is
// thirty days.
TEST(Instant, AddingACalendarUnitToAnInstantIsRefused) {
  GCHRON_Instant i = instant(0);
  GCHRON_Instant out;
  GCHRON_Duration d{};

  d.months = 1;
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_add(&i, &d, &out));
  d = GCHRON_Duration{};
  d.days = 1;
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_add(&i, &d, &out));
  d = GCHRON_Duration{};
  d.weeks = 1;
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_add(&i, &d, &out));
  d = GCHRON_Duration{};
  d.years = 1;
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_add(&i, &d, &out));

  // Twenty-four hours, though, is a number of seconds and is addition.
  d = GCHRON_Duration{};
  d.hours = 24;
  ASSERT_EQ(GCHRON_OK, gchron_instant_add(&i, &d, &out));
  EXPECT_EQ(86400, out.sec);
}

TEST(Instant, AddAndSubtractAreInverses) {
  GCHRON_Instant i = instant(1000, 500000000);
  GCHRON_Duration d = exact(3600, 250000000);
  GCHRON_Instant plus;
  GCHRON_Instant back;
  ASSERT_EQ(GCHRON_OK, gchron_instant_add(&i, &d, &plus));
  EXPECT_EQ(4600, plus.sec);
  EXPECT_EQ(750000000, plus.nsec);
  ASSERT_EQ(GCHRON_OK, gchron_instant_subtract(&plus, &d, &back));
  EXPECT_EQ(0, gchron_instant_compare(&i, &back));
}

// Mistake M24: overflow is detected before it happens, never observed after.
TEST(Instant, OverflowIsReportedRatherThanWrapped) {
  GCHRON_Instant i = instant(INT64_MAX, 0);
  GCHRON_Duration d = exact(1);
  GCHRON_Instant out;
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_instant_add(&i, &d, &out));

  GCHRON_Instant low = instant(INT64_MIN, 0);
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_instant_subtract(&low, &d, &out));

  GCHRON_Duration huge{};
  huge.hours = INT64_MAX;
  GCHRON_Instant zero = instant(0);
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_instant_add(&zero, &huge, &out));
}

TEST(Instant, UntilIsExactAndSigned) {
  GCHRON_Instant a = instant(100, 250000000);
  GCHRON_Instant b = instant(103, 750000000);
  GCHRON_Duration d;

  ASSERT_EQ(GCHRON_OK, gchron_instant_until(&a, &b, &d));
  EXPECT_EQ(3, d.seconds);
  EXPECT_EQ(500000000, d.nsec);
  EXPECT_FALSE(gchron_duration_has_calendar_units(&d));

  ASSERT_EQ(GCHRON_OK, gchron_instant_until(&b, &a, &d));
  // -3.5 seconds is {seconds: -4, nsec: 500000000}, the same normalisation
  // the instant uses.
  EXPECT_EQ(-4, d.seconds);
  EXPECT_EQ(500000000, d.nsec);
  EXPECT_EQ(-1, gchron_duration_sign(&d));
}

TEST(Instant, UnixEncodingsRoundTripAndReportOverflow) {
  GCHRON_Instant i;
  int64_t value = 0;

  ASSERT_EQ(GCHRON_OK, gchron_instant_from_unix_millis(-1, &i));
  EXPECT_EQ(-1, i.sec);
  EXPECT_EQ(999000000, i.nsec);
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_unix_millis(&i, &value));
  EXPECT_EQ(-1, value);

  ASSERT_EQ(GCHRON_OK, gchron_instant_from_unix_micros(1500000, &i));
  EXPECT_EQ(1, i.sec);
  EXPECT_EQ(500000000, i.nsec);
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_unix_micros(&i, &value));
  EXPECT_EQ(1500000, value);

  ASSERT_EQ(GCHRON_OK, gchron_instant_from_unix_nanos(-1, &i));
  EXPECT_EQ(-1, i.sec);
  EXPECT_EQ(999999999, i.nsec);
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_unix_nanos(&i, &value));
  EXPECT_EQ(-1, value);

  // Nanoseconds since 1970 reach only 1677..2262, which is why they are not
  // this library's own encoding.
  GCHRON_Instant far = instant(INT64_C(1000000000000));
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_instant_to_unix_nanos(&far, &value));
}

// Lossy, and named so. The test states the loss rather than pretending there
// is none.
TEST(Instant, AsDoubleIsLossyFarFromTheEpoch) {
  GCHRON_Instant near = instant(1, 1);
  double value = 0;
  ASSERT_EQ(GCHRON_OK, gchron_instant_as_double(&near, &value));
  EXPECT_NEAR(1.000000001, value, 1e-12);

  GCHRON_Instant far = instant(INT64_C(1758000000), 1);
  ASSERT_EQ(GCHRON_OK, gchron_instant_as_double(&far, &value));
  // A double has 53 bits of mantissa, so a nanosecond two billion seconds
  // from the epoch is below its resolution and vanishes.
  EXPECT_EQ(static_cast<double>(INT64_C(1758000000)), value);
}

TEST(Instant, CompareOrdersTheTimelineAndHandlesNull) {
  GCHRON_Instant a = instant(1, 0);
  GCHRON_Instant b = instant(1, 1);
  GCHRON_Instant c = instant(2, 0);
  EXPECT_LT(gchron_instant_compare(&a, &b), 0);
  EXPECT_LT(gchron_instant_compare(&b, &c), 0);
  EXPECT_EQ(0, gchron_instant_compare(&a, &a));
  EXPECT_LT(gchron_instant_compare(nullptr, &a), 0);
  EXPECT_GT(gchron_instant_compare(&a, nullptr), 0);
  EXPECT_EQ(0, gchron_instant_compare(nullptr, nullptr));
}

// Half-open, so abutting intervals tile the line with no overlap and no gap.
TEST(Interval, AbuttingIntervalsDoNotOverlap) {
  GCHRON_Instant t0 = instant(0);
  GCHRON_Instant t1 = instant(10);
  GCHRON_Instant t2 = instant(20);
  GCHRON_Interval a;
  GCHRON_Interval b;
  ASSERT_EQ(GCHRON_OK, gchron_interval_create(&t0, &t1, &a));
  ASSERT_EQ(GCHRON_OK, gchron_interval_create(&t1, &t2, &b));

  EXPECT_FALSE(gchron_interval_overlaps(&a, &b));
  EXPECT_TRUE(gchron_interval_contains(&a, &t0));
  EXPECT_FALSE(gchron_interval_contains(&a, &t1));
  EXPECT_TRUE(gchron_interval_contains(&b, &t1));

  GCHRON_Duration d;
  ASSERT_EQ(GCHRON_OK, gchron_interval_duration(&a, &d));
  EXPECT_EQ(10, d.seconds);
}

TEST(Interval, CreateRefusesAnEndBeforeItsStart) {
  GCHRON_Instant t0 = instant(0);
  GCHRON_Instant t1 = instant(10);
  GCHRON_Interval out;
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_interval_create(&t1, &t0, &out));
  // Equal is an empty interval, not an error.
  EXPECT_EQ(GCHRON_OK, gchron_interval_create(&t0, &t0, &out));
  EXPECT_FALSE(gchron_interval_contains(&out, &t0));
}

TEST(Instant, DumpWritesSomethingForEveryInput) {
  GCHRON_Instant i = instant(1, 2);
  FILE * sink = std::tmpfile();
  ASSERT_NE(nullptr, sink);
  gchron_instant_dump(&i, sink);
  gchron_instant_dump(nullptr, sink);
  gchron_instant_dump(&i, nullptr);
  EXPECT_GT(std::ftell(sink), 0);
  std::fclose(sink);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/*--------------------------------------------------------------------------*
 * Rounding
 *--------------------------------------------------------------------------*/

namespace {

/** Every mode, including the one that refuses. */
const GCHRON_Rounding kAllModes[] = {
  GCHRON_ROUND_REJECT, GCHRON_ROUND_TRUNCATE, GCHRON_ROUND_FLOOR,
  GCHRON_ROUND_CEIL, GCHRON_ROUND_HALF_EXPAND, GCHRON_ROUND_HALF_EVEN
};

/** The units an instant can be divided by. */
const GCHRON_Unit kExactUnits[] = {
  GCHRON_UNIT_NANOSECOND, GCHRON_UNIT_MICROSECOND, GCHRON_UNIT_MILLISECOND,
  GCHRON_UNIT_SECOND, GCHRON_UNIT_MINUTE, GCHRON_UNIT_HOUR, GCHRON_UNIT_DAY
};

GCHRON_Instant At(int64_t sec, int32_t nsec) {
  GCHRON_Instant i;
  i.sec = sec;
  i.nsec = nsec;
  return i;
}

int Cmp(const GCHRON_Instant & a, const GCHRON_Instant & b) {
  return gchron_instant_compare(&a, &b);
}

} // namespace

/*
 * The three properties the phase-5 note names, swept over every unit, every
 * mode and a spread of instants that crosses the epoch.
 *
 * Crossing the epoch is the point of the negative rows. Bucketing a timestamp
 * is written by callers as `t / 900 * 900`, which for a negative `t` rounds
 * toward zero and therefore *forward* in time - so a value at -0.5s lands in
 * the bucket after the one it belongs to, and the bug is invisible in every
 * test whose fixtures are all after 1970.
 */
TEST(InstantRound, IsIdempotentForEveryUnitAndMode) {
  const GCHRON_Instant samples[] = {
    At(0, 0), At(0, 1), At(1, 500000000), At(59, 999999999),
    At(-1, 0), At(-1, 1), At(-1, 500000000), At(-86400, 123456789),
    At(1700000000, 987654321), At(-2208988800LL, 250000000)
  };

  for (GCHRON_Unit unit : kExactUnits) {
    for (GCHRON_Rounding mode : kAllModes) {
      for (const GCHRON_Instant & in : samples) {
        GCHRON_Instant once;
        GCHRON_Instant twice;

        if (gchron_instant_round(&in, unit, 1, mode, &once) != GCHRON_OK) {
          continue;
        }
        ASSERT_EQ(GCHRON_OK, gchron_instant_round(&once, unit, 1, mode,
            &twice)) << "rounding an already-rounded value failed";
        EXPECT_EQ(0, Cmp(once, twice))
            << "unit " << gchron_unit_string(unit) << ", mode " << mode
            << ": rounding twice differs from rounding once ("
            << once.sec << "." << once.nsec << " then "
            << twice.sec << "." << twice.nsec << ")";
      }
    }
  }
}

TEST(InstantRound, FloorNeverMovesForwardAndCeilNeverMovesBack) {
  const GCHRON_Instant samples[] = {
    At(0, 1), At(1, 500000000), At(-1, 500000000), At(-86399, 1),
    At(1700000000, 987654321), At(-2208988800LL, 250000000)
  };

  for (GCHRON_Unit unit : kExactUnits) {
    for (const GCHRON_Instant & in : samples) {
      GCHRON_Instant down;
      GCHRON_Instant up;

      ASSERT_EQ(GCHRON_OK, gchron_instant_round(&in, unit, 1,
          GCHRON_ROUND_FLOOR, &down));
      ASSERT_EQ(GCHRON_OK, gchron_instant_round(&in, unit, 1,
          GCHRON_ROUND_CEIL, &up));

      EXPECT_LE(Cmp(down, in), 0)
          << "FLOOR moved " << in.sec << "." << in.nsec << " forward at "
          << gchron_unit_string(unit);
      EXPECT_GE(Cmp(up, in), 0)
          << "CEIL moved " << in.sec << "." << in.nsec << " back at "
          << gchron_unit_string(unit);
    }
  }
}

TEST(InstantRound, AValueOnABoundaryIsUnchangedByEveryMode) {
  /* One value that is exactly on a boundary of every unit up to a day. */
  const GCHRON_Instant on_every_boundary = At(-86400 * 3, 0);

  for (GCHRON_Unit unit : kExactUnits) {
    for (GCHRON_Rounding mode : kAllModes) {
      GCHRON_Instant got;
      ASSERT_EQ(GCHRON_OK, gchron_instant_round(&on_every_boundary, unit, 1,
          mode, &got))
          << "an exact value was refused at " << gchron_unit_string(unit)
          << " by mode " << mode;
      EXPECT_EQ(0, Cmp(on_every_boundary, got))
          << "mode " << mode << " moved a value already on a "
          << gchron_unit_string(unit) << " boundary";
    }
  }
}

/*
 * The direction of rounding before the epoch, written out rather than swept,
 * because these are the answers the sweep above would agree with while both
 * were wrong.
 */
TEST(InstantRound, NegativeTimesRoundByTheTimelineNotByMagnitude) {
  const GCHRON_Instant half_before = At(-1, 500000000); /* -0.5 s */
  GCHRON_Instant got;

  ASSERT_EQ(GCHRON_OK, gchron_instant_round(&half_before, GCHRON_UNIT_SECOND,
      1, GCHRON_ROUND_FLOOR, &got));
  EXPECT_EQ(-1, got.sec) << "FLOOR on -0.5s must give -1s, not 0s";
  EXPECT_EQ(0, got.nsec);

  ASSERT_EQ(GCHRON_OK, gchron_instant_round(&half_before, GCHRON_UNIT_SECOND,
      1, GCHRON_ROUND_CEIL, &got));
  EXPECT_EQ(0, got.sec) << "CEIL on -0.5s must give 0s";

  ASSERT_EQ(GCHRON_OK, gchron_instant_round(&half_before, GCHRON_UNIT_SECOND,
      1, GCHRON_ROUND_TRUNCATE, &got));
  EXPECT_EQ(0, got.sec) << "TRUNCATE is toward zero, so -0.5s gives 0s";

  /* A tie away from zero is downward before the epoch. */
  ASSERT_EQ(GCHRON_OK, gchron_instant_round(&half_before, GCHRON_UNIT_SECOND,
      1, GCHRON_ROUND_HALF_EXPAND, &got));
  EXPECT_EQ(-1, got.sec) << "HALF_EXPAND on -0.5s goes away from zero, to -1s";

  /*
   * A unit wider than a second, which is where the classic bug actually
   * lives. At a scale of one, C's truncating division and a floor division
   * agree for every input, so every assertion above this one passes against
   * an implementation that divides with `/`. Replacing gchron_floor_div()
   * with `/` was caught only by the sweep, not by the test named after the
   * defect - so the defect's own test now uses a scale where the two differ.
   */
  const GCHRON_Instant ninety_before = At(-90, 0); /* -00:01:30 */

  ASSERT_EQ(GCHRON_OK, gchron_instant_round(&ninety_before,
      GCHRON_UNIT_MINUTE, 1, GCHRON_ROUND_FLOOR, &got));
  EXPECT_EQ(-120, got.sec)
      << "FLOOR on -90s must give -120s; -60s is truncation, which moves a "
      << "value before the epoch forward in time";

  ASSERT_EQ(GCHRON_OK, gchron_instant_round(&ninety_before,
      GCHRON_UNIT_MINUTE, 1, GCHRON_ROUND_TRUNCATE, &got));
  EXPECT_EQ(-60, got.sec) << "TRUNCATE on -90s is toward zero, so -60s";
}

TEST(InstantRound, AnIncrementMustTileItsUnit) {
  const GCHRON_Instant in = At(1700000000, 0);
  GCHRON_Instant got;

  /* 15 minutes tiles an hour. */
  EXPECT_EQ(GCHRON_OK, gchron_instant_round(&in, GCHRON_UNIT_MINUTE, 15,
      GCHRON_ROUND_FLOOR, &got));
  EXPECT_EQ(0, got.sec % (15 * 60))
      << "a 15-minute rounding did not land on a 15-minute boundary";

  /* 7 does not, so the boundary the caller imagines does not exist. */
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_round(&in, GCHRON_UNIT_MINUTE,
      7, GCHRON_ROUND_FLOOR, &got));
  /* Nor does an increment larger than its own unit's next step. */
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_round(&in, GCHRON_UNIT_MINUTE,
      61, GCHRON_ROUND_FLOOR, &got));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_round(&in, GCHRON_UNIT_MINUTE,
      0, GCHRON_ROUND_FLOOR, &got));
  /* A day has no next unit here, so only one day at a time. */
  EXPECT_EQ(GCHRON_OK, gchron_instant_round(&in, GCHRON_UNIT_DAY, 1,
      GCHRON_ROUND_FLOOR, &got));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_round(&in, GCHRON_UNIT_DAY, 2,
      GCHRON_ROUND_FLOOR, &got));
}

TEST(InstantRound, CalendarUnitsAreRefusedRatherThanApproximated) {
  const GCHRON_Instant in = At(1700000000, 0);
  GCHRON_Instant got;

  for (GCHRON_Unit unit : {GCHRON_UNIT_WEEK, GCHRON_UNIT_MONTH,
       GCHRON_UNIT_YEAR}) {
    EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_round(&in, unit, 1,
        GCHRON_ROUND_FLOOR, &got))
        << gchron_unit_string(unit) << " has no length an instant can be "
        << "divided by and must not fall through to a nanosecond count";
  }
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_round(&in,
      GCHRON_UNIT_UNSPECIFIED, 1, GCHRON_ROUND_FLOOR, &got));
}

TEST(InstantRound, RejectRefusesOnlyWhatIsNotAlreadyExact) {
  GCHRON_Instant got;

  const GCHRON_Instant exact = At(120, 0);
  EXPECT_EQ(GCHRON_OK, gchron_instant_round(&exact, GCHRON_UNIT_MINUTE, 1,
      GCHRON_ROUND_REJECT, &got));
  EXPECT_EQ(120, got.sec);

  const GCHRON_Instant inexact = At(121, 0);
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_instant_round(&inexact,
      GCHRON_UNIT_MINUTE, 1, GCHRON_ROUND_REJECT, &got));
}

TEST(InstantRound, HalfEvenBreaksTiesOnTheBucketNumber) {
  GCHRON_Instant got;

  /* 30 s is halfway through the minute at 60..120; bucket 1 is odd, so up. */
  const GCHRON_Instant in_odd = At(90, 0);
  ASSERT_EQ(GCHRON_OK, gchron_instant_round(&in_odd, GCHRON_UNIT_MINUTE, 1,
      GCHRON_ROUND_HALF_EVEN, &got));
  EXPECT_EQ(120, got.sec) << "a tie in an odd bucket rounds up to the even one";

  /* 30 s into the minute at 120..180; bucket 2 is even, so stay. */
  const GCHRON_Instant in_even = At(150, 0);
  ASSERT_EQ(GCHRON_OK, gchron_instant_round(&in_even, GCHRON_UNIT_MINUTE, 1,
      GCHRON_ROUND_HALF_EVEN, &got));
  EXPECT_EQ(120, got.sec) << "a tie in an even bucket stays";
}

TEST(InstantRound, RejectsNullAndInvalidInput) {
  const GCHRON_Instant in = At(1, 0);
  GCHRON_Instant got;
  GCHRON_Instant bad = At(1, -1);

  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_round(nullptr,
      GCHRON_UNIT_SECOND, 1, GCHRON_ROUND_FLOOR, &got));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_round(&in, GCHRON_UNIT_SECOND,
      1, GCHRON_ROUND_FLOOR, nullptr));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_instant_round(&bad, GCHRON_UNIT_SECOND,
      1, GCHRON_ROUND_FLOOR, &got));
}
