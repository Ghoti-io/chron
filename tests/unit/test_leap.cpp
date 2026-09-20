/**
 * @file
 *
 * leap.h: the table, and the conversion to and from TAI.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/chron.h>
#include <gtest/gtest.h>
#include <string>

namespace {

/** 2017-01-01T00:00:00Z, the first instant at TAI-UTC = 37. */
constexpr int64_t k2017 = 1483228800;
/** 2016-12-31T23:59:59Z - and also the `:60` that followed it. */
constexpr int64_t kLastSecondOf2016 = k2017 - 1;

const GCHRON_LeapTable * builtin() {
  return gchron_leap_table_builtin();
}

TEST(Leap, TheBuiltInTableStartsWhereUtcBecameASteppedScale) {
  GCHRON_LeapEntry first{};
  ASSERT_EQ(GCHRON_OK, gchron_leap_table_entry(builtin(), 0, &first));

  // 1972-01-01T00:00:00Z, TAI already ten seconds ahead.
  EXPECT_EQ(63072000, first.at.sec);
  EXPECT_EQ(10, first.tai_minus_utc);
  // The first row is not a leap second; it is where the count began.
  EXPECT_FALSE(first.negative);
}

TEST(Leap, BeforeTheTableThereIsNoAnswerRatherThanAWrongOne) {
  // 1971: UTC ran on rubber seconds and TAI-UTC was not an integer. RANGE,
  // not a silent extrapolation of the 1972 offset backwards.
  GCHRON_Instant before{ 63072000 - 1, 0 };
  int32_t offset = 0;
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_leap_offset_at(builtin(), before, &offset));
}

TEST(Leap, PastTheExpiryEveryConversionSaysSoRatherThanGuessing) {
  GCHRON_Instant expiry{};
  ASSERT_EQ(GCHRON_OK, gchron_leap_table_expiry(builtin(), &expiry));

  int32_t offset = 0;
  GCHRON_Instant just_before{ expiry.sec - 1, 0 };
  EXPECT_EQ(GCHRON_OK,
      gchron_leap_offset_at(builtin(), just_before, &offset));

  // The instant of expiry is already past it: the file can no longer say
  // whether a leap second has been announced since (design.md 5.1 item 3).
  EXPECT_EQ(GCHRON_ERR_EXPIRED,
      gchron_leap_offset_at(builtin(), expiry, &offset));

  GCHRON_TaiInstant tai{};
  EXPECT_EQ(GCHRON_ERR_EXPIRED,
      gchron_tai_from_instant(builtin(), expiry, false, &tai));
}

TEST(Leap, TaiIsThirtySevenSecondsAheadOfUtcToday) {
  GCHRON_Instant now{ k2017, 0 };
  int32_t offset = 0;
  ASSERT_EQ(GCHRON_OK, gchron_leap_offset_at(builtin(), now, &offset));
  EXPECT_EQ(37, offset);

  GCHRON_TaiInstant tai{};
  ASSERT_EQ(GCHRON_OK, gchron_tai_from_instant(builtin(), now, false, &tai));
  EXPECT_EQ(k2017 + 37, tai.sec);
  EXPECT_EQ(0, tai.nsec);
}

TEST(Leap, TheThreeSecondsAcrossTheLastLeapAreConsecutiveInTai) {
  // The whole reason the type exists: on TAI nothing repeats, so the three
  // seconds spanning 2016-12-31T23:59:59, :60 and 2017-01-01T00:00:00 are
  // three consecutive numbers, which on Unix time they are not.
  GCHRON_TaiInstant before{};
  GCHRON_TaiInstant leap{};
  GCHRON_TaiInstant after{};

  GCHRON_Instant last{ kLastSecondOf2016, 0 };
  GCHRON_Instant next{ k2017, 0 };

  ASSERT_EQ(GCHRON_OK, gchron_tai_from_instant(builtin(), last, false, &before));
  ASSERT_EQ(GCHRON_OK, gchron_tai_from_instant(builtin(), last, true, &leap));
  ASSERT_EQ(GCHRON_OK, gchron_tai_from_instant(builtin(), next, false, &after));

  EXPECT_EQ(before.sec + 1, leap.sec);
  EXPECT_EQ(leap.sec + 1, after.sec);
  // And on Unix time two of the three are the same number.
  EXPECT_EQ(last.sec, kLastSecondOf2016);
}

TEST(Leap, AColonSixtyOnADayWithNoLeapSecondIsRefused) {
  // 1999-06-30 had no leap second. Claiming its `:60` is a caller error and
  // is reported as one, rather than converted into a plausible instant.
  GCHRON_Date date{};
  ASSERT_EQ(GCHRON_OK, gchron_date_create(1999, 6, 30, &date));
  bool is_leap = true;
  ASSERT_EQ(GCHRON_OK, gchron_leap_is_leap_day(builtin(), &date, &is_leap));
  EXPECT_FALSE(is_leap);

  int64_t day = 0;
  ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&date, &day));
  GCHRON_Instant last_second{ (day + 1) * 86400 - 1, 0 };
  GCHRON_TaiInstant tai{};
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_tai_from_instant(builtin(), last_second, true, &tai));

  // 1998-12-31 did have one.
  ASSERT_EQ(GCHRON_OK, gchron_date_create(1998, 12, 31, &date));
  ASSERT_EQ(GCHRON_OK, gchron_leap_is_leap_day(builtin(), &date, &is_leap));
  EXPECT_TRUE(is_leap);
}

TEST(Leap, EverySecondRoundTripsThroughTai) {
  // Across the four leap seconds since 2000 and a stretch either side, every
  // second must survive UTC -> TAI -> UTC unchanged, and the leap flag must
  // come back as it went in.
  static const int64_t kSpans[] = {
    946684800,   // 2000-01-01
    1136073600,  // 2006-01-01, the day after a leap second
    1230768000,  // 2009-01-01, likewise
    1341100800,  // 2012-07-01, likewise
    1483228800,  // 2017-01-01, likewise
  };

  size_t checked = 0;
  for (int64_t anchor : kSpans) {
    for (int64_t delta = -90000; delta <= 90000; ++delta) {
      GCHRON_Instant utc{ anchor + delta, 123456789 };
      GCHRON_TaiInstant tai{};
      if (gchron_tai_from_instant(builtin(), utc, false, &tai) != GCHRON_OK) {
        continue;
      }
      GCHRON_Instant back{};
      bool leap = true;
      ASSERT_EQ(GCHRON_OK,
          gchron_tai_to_instant(builtin(), tai, &back, &leap));
      EXPECT_EQ(utc.sec, back.sec) << "at " << utc.sec;
      EXPECT_EQ(utc.nsec, back.nsec) << "at " << utc.sec;
      EXPECT_FALSE(leap) << "at " << utc.sec;
      ++checked;
    }
  }
  EXPECT_GT(checked, 800000u);
}

TEST(Leap, TheInsertedSecondRoundTripsAsItself) {
  GCHRON_Instant utc{ kLastSecondOf2016, 500000000 };
  GCHRON_TaiInstant tai{};
  ASSERT_EQ(GCHRON_OK, gchron_tai_from_instant(builtin(), utc, true, &tai));

  GCHRON_Instant back{};
  bool leap = false;
  ASSERT_EQ(GCHRON_OK, gchron_tai_to_instant(builtin(), tai, &back, &leap));
  EXPECT_EQ(utc.sec, back.sec);
  EXPECT_EQ(utc.nsec, back.nsec);
  // This is the bit Unix time cannot carry, and the reason the flag exists.
  EXPECT_TRUE(leap);
}

TEST(Leap, ElapsedCountsTheLeapSecondThatInstantDifferenceDoesNot) {
  // The same pair of instants, asked two different questions. Both answers
  // are right; they are answers to different questions.
  GCHRON_Instant from{ kLastSecondOf2016 - 1, 0 };
  GCHRON_Instant to{ k2017, 0 };

  GCHRON_Duration naive{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_until(&from, &to, &naive));
  EXPECT_EQ(2, naive.seconds);

  GCHRON_Duration real{};
  ASSERT_EQ(GCHRON_OK, gchron_leap_elapsed(builtin(), from, to, &real));
  EXPECT_EQ(3, real.seconds) << "the leap second was not counted";
}

TEST(Leap, ParsingRejectsAFileWithNoExpiry) {
  // A table that never expires would answer confidently forever, which is
  // the failure mode leap.h exists to avoid.
  static const char kNoExpiry[] =
      "#$\t3992312697\n"
      "2272060800\t10\t# 1 Jan 1972\n";
  GCHRON_LeapTable * table = nullptr;
  GCHRON_Error err{};
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_leap_table_parse(kNoExpiry, sizeof(kNoExpiry) - 1, nullptr,
          &table, &err));
  EXPECT_EQ(GCHRON_DIAG_LEAP_TABLE_NO_EXPIRY, err.diag);
  EXPECT_EQ(nullptr, table);
}

TEST(Leap, ParsingRejectsAnEmptyTableAndOneOutOfOrder) {
  GCHRON_LeapTable * table = nullptr;
  GCHRON_Error err{};

  static const char kEmpty[] = "# nothing but prose\n#@\t4023129600\n";
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_leap_table_parse(kEmpty, sizeof(kEmpty) - 1, nullptr, &table,
          &err));
  EXPECT_EQ(GCHRON_DIAG_LEAP_TABLE_EMPTY, err.diag);

  static const char kBackwards[] =
      "#@\t4023129600\n"
      "2287785600\t11\n"
      "2272060800\t10\n";
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_leap_table_parse(kBackwards, sizeof(kBackwards) - 1, nullptr,
          &table, &err));
  EXPECT_EQ(GCHRON_DIAG_LEAP_TABLE_ORDER, err.diag);
}

TEST(Leap, ANegativeLeapSecondIsRecordedAsOne) {
  // None has ever been issued. The file format can express one, and a reader
  // that assumed otherwise would misreport the day if one ever were
  // (design.md 5.1 item 5). Synthesised, because no oracle can supply it.
  static const char kNegative[] =
      "#$\t3992312697\n"
      "#@\t4023129600\n"
      "2272060800\t10\n"
      "2287785600\t11\n"
      "2303683200\t10\n";
  GCHRON_LeapTable * table = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_leap_table_parse(kNegative, sizeof(kNegative) - 1, nullptr,
          &table, nullptr));
  ASSERT_EQ(3u, gchron_leap_table_count(table));

  GCHRON_LeapEntry third{};
  ASSERT_EQ(GCHRON_OK, gchron_leap_table_entry(table, 2, &third));
  EXPECT_TRUE(third.negative);
  EXPECT_EQ(10, third.tai_minus_utc);

  // The day it shortened is not a day that gained a second.
  GCHRON_Date date{};
  ASSERT_EQ(GCHRON_OK, gchron_date_create(1972, 12, 31, &date));
  bool is_leap = true;
  ASSERT_EQ(GCHRON_OK, gchron_leap_is_leap_day(table, &date, &is_leap));
  EXPECT_FALSE(is_leap);

  gchron_leap_table_destroy(table);
}

TEST(Leap, DestroyingTheBuiltInTableIsIgnored) {
  // It is static, and a caller who frees every table it holds must not have
  // to special-case this one.
  gchron_leap_table_destroy(
      const_cast<GCHRON_LeapTable *>(gchron_leap_table_builtin()));
  gchron_leap_table_destroy(nullptr);

  GCHRON_LeapEntry first{};
  EXPECT_EQ(GCHRON_OK, gchron_leap_table_entry(builtin(), 0, &first));
  EXPECT_EQ(10, first.tai_minus_utc);
}

TEST(Leap, NullsAreRefusedRatherThanCrashed) {
  GCHRON_Instant instant{ k2017, 0 };
  int32_t offset = 0;
  GCHRON_TaiInstant tai{};
  GCHRON_Instant back{};

  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_leap_offset_at(nullptr, instant, &offset));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_leap_offset_at(builtin(), instant, nullptr));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_tai_from_instant(nullptr, instant, false, &tai));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_tai_to_instant(nullptr, tai, &back, nullptr));
  EXPECT_EQ(0u, gchron_leap_table_count(nullptr));
}


/*
 * GCHRON_LEAP_TABLE, which was declared in phase 0 and answered
 * GCHRON_ERR_UNSUPPORTED until this phase had a table to consult. The cases
 * are design.md section 5.4's own: the ones that separate TABLE from MINUTE.
 */
TEST(LeapPolicy, TableAcceptsARealLeapSecondAndRefusesAnInventedOne) {
  GCHRON_ParseOptions opts{};
  gchron_parse_options_default(&opts);
  opts.leap = GCHRON_LEAP_TABLE;
  opts.leap_table = gchron_leap_table_builtin();

  GCHRON_OffsetDateTime out{};
  GCHRON_ParseInfo info{};
  GCHRON_Error err{};

  // 1998-12-31 gained a second, and the suite requires this one to be valid.
  static const char kReal[] = "1998-12-31T23:59:60Z";
  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc3339_date_time(kReal, sizeof(kReal) - 1, &opts, &out,
          &info, &err))
      << gchron_diag_string(err.diag);
  EXPECT_TRUE(info.leap_second);
  EXPECT_EQ(59, out.civil.time.second);

  // 1999-06-30 did not. This is the whole difference between TABLE and
  // MINUTE: MINUTE accepts it, TABLE knows better.
  static const char kInvented[] = "1999-06-30T23:59:60Z";
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parse_rfc3339_date_time(kInvented, sizeof(kInvented) - 1, &opts,
          &out, &info, &err));
  EXPECT_EQ(GCHRON_DIAG_LEAP_SECOND_NOT_IN_TABLE, err.diag);

  // And under MINUTE the same text is accepted, which is the point.
  opts.leap = GCHRON_LEAP_MINUTE;
  EXPECT_EQ(GCHRON_OK,
      gchron_parse_rfc3339_date_time(kInvented, sizeof(kInvented) - 1, &opts,
          &out, &info, &err));
}

TEST(LeapPolicy, TableAsksAboutTheUtcDayNotTheLocalOne) {
  // The JSON Schema suite's own vector. Local 1998-12-31T15:59:60-08:00 is
  // 1998-12-31T23:59:60Z, so the day to ask the table about is the UTC one -
  // and here they happen to agree.
  GCHRON_ParseOptions opts{};
  gchron_parse_options_default(&opts);
  opts.leap = GCHRON_LEAP_TABLE;
  opts.leap_table = gchron_leap_table_builtin();

  GCHRON_OffsetDateTime out{};
  GCHRON_Error err{};
  static const char kText[] = "1998-12-31T15:59:60.123-08:00";
  EXPECT_EQ(GCHRON_OK,
      gchron_parse_rfc3339_date_time(kText, sizeof(kText) - 1, &opts, &out,
          nullptr, &err))
      << gchron_diag_string(err.diag);

  // And one where they do not: local 1999-01-01T10:59:60+11:00 is
  // 1998-12-31T23:59:60Z. The local date lists no leap second; the UTC date
  // does, and the UTC date is the one that counts.
  static const char kNextDay[] = "1999-01-01T10:59:60+11:00";
  EXPECT_EQ(GCHRON_OK,
      gchron_parse_rfc3339_date_time(kNextDay, sizeof(kNextDay) - 1, &opts,
          &out, nullptr, &err))
      << gchron_diag_string(err.diag);
}

TEST(LeapPolicy, TableWithoutATableIsRefusedRatherThanLoosened) {
  GCHRON_ParseOptions opts{};
  gchron_parse_options_default(&opts);
  opts.leap = GCHRON_LEAP_TABLE;
  opts.leap_table = nullptr;

  GCHRON_OffsetDateTime out{};
  GCHRON_Error err{};
  static const char kText[] = "1998-12-31T23:59:60Z";
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_parse_rfc3339_date_time(kText, sizeof(kText) - 1, &opts, &out,
          nullptr, &err));
}

TEST(LeapPolicy, TableStillEnforcesTheMinuteRule) {
  // TABLE is MINUTE *and* the table, so everything MINUTE refuses it refuses.
  GCHRON_ParseOptions opts{};
  gchron_parse_options_default(&opts);
  opts.leap = GCHRON_LEAP_TABLE;
  opts.leap_table = gchron_leap_table_builtin();

  GCHRON_OffsetDateTime out{};
  GCHRON_Error err{};
  static const char kWrongMinute[] = "1998-12-31T23:58:60Z";
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parse_rfc3339_date_time(kWrongMinute, sizeof(kWrongMinute) - 1,
          &opts, &out, nullptr, &err));
  EXPECT_EQ(GCHRON_DIAG_LEAP_SECOND_WRONG_MINUTE, err.diag);
}

TEST(LeapPolicy, ATimeWithNoDateCannotBeCheckedAgainstTheTable) {
  // A `full-time` has no day, and the table's question is about a day. The
  // parse is refused rather than quietly settled at MINUTE's strictness.
  GCHRON_ParseOptions opts{};
  gchron_parse_options_default(&opts);
  opts.leap = GCHRON_LEAP_TABLE;
  opts.leap_table = gchron_leap_table_builtin();

  GCHRON_OffsetTime out{};
  GCHRON_Error err{};
  static const char kText[] = "23:59:60Z";
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_parse_rfc3339_full_time(kText, sizeof(kText) - 1, &opts, &out,
          nullptr, &err));
}

TEST(LeapPolicy, PastTheTablesExpiryTheAnswerIsExpiredNotInvalid) {
  // "I cannot know" is a different answer from "that is not a leap second",
  // and a caller may reasonably treat them differently - so the distinction
  // survives all the way out of the parser.
  GCHRON_ParseOptions opts{};
  gchron_parse_options_default(&opts);
  opts.leap = GCHRON_LEAP_TABLE;

  // A table whose expiry is 1973, so that a 1998 timestamp is past it.
  static const char kStale[] =
      "#$\t3992312697\n"
      "#@\t2303683200\n"
      "2272060800\t10\n"
      "2287785600\t11\n";
  GCHRON_LeapTable * stale = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_leap_table_parse(kStale, sizeof(kStale) - 1, nullptr, &stale,
          nullptr));
  opts.leap_table = stale;

  GCHRON_OffsetDateTime out{};
  GCHRON_Error err{};
  static const char kText[] = "1998-12-31T23:59:60Z";
  EXPECT_EQ(GCHRON_ERR_EXPIRED,
      gchron_parse_rfc3339_date_time(kText, sizeof(kText) - 1, &opts, &out,
          nullptr, &err));

  gchron_leap_table_destroy(stale);
}


/*
 * The three defects fuzz_leap found. None of them is reachable with a real
 * leap-seconds.list, which is the point: a file the library parses is a file
 * somebody can hand it.
 */

TEST(Leap, AnExpiryWithTrailingJunkIsRefusedRatherThanTruncated) {
  /*
   * `read_u64` stops at the first non-digit, so `40231296p0` used to parse as
   * 40231296 - the right shape, off by a factor of a hundred, no complaint.
   * An expiry that is silently wrong is the one failure this module exists to
   * prevent: everything past it reports EXPIRED, so a corruption that
   * shortened it would never be noticed.
   */
  static const char kJunk[] =
      "#@\t40231296p0\n"
      "2272060800\t10\n";
  GCHRON_LeapTable * table = nullptr;
  GCHRON_Error err{};
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_leap_table_parse(kJunk, sizeof(kJunk) - 1, nullptr, &table, &err));
  EXPECT_EQ(GCHRON_DIAG_LEAP_TABLE_MALFORMED, err.diag);
  EXPECT_EQ(nullptr, table);

  // The same shape on the update line, which had the same hole.
  static const char kUpdated[] =
      "#$\t3992312697x\n"
      "#@\t4023129600\n"
      "2272060800\t10\n";
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_leap_table_parse(kUpdated, sizeof(kUpdated) - 1, nullptr, &table,
          &err));

  // And a comment after the number is not junk - the real file has them.
  static const char kComment[] =
      "#@\t4023129600 # expires 28 June 2027\n"
      "2272060800\t10\n";
  EXPECT_EQ(GCHRON_OK,
      gchron_leap_table_parse(kComment, sizeof(kComment) - 1, nullptr, &table,
          &err));
  gchron_leap_table_destroy(table);
}

TEST(Leap, AnOffsetThatMovesByMoreThanASecondIsRefused) {
  /*
   * A leap second is one second. A row claiming a larger jump makes the TAI
   * timeline run *backwards* across that boundary, and then two UTC instants
   * share a TAI second - which breaks the one property the type exists to
   * provide.
   */
  static const char kJump[] =
      "#@\t4023129600\n"
      "2272060800\t10\n"
      "2287785600\t800010\n";
  GCHRON_LeapTable * table = nullptr;
  GCHRON_Error err{};
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_leap_table_parse(kJump, sizeof(kJump) - 1, nullptr, &table, &err));
  EXPECT_EQ(GCHRON_DIAG_LEAP_TABLE_STEP, err.diag);

  // A row that changes nothing is refused by the same rule: the file records
  // changes, and a no-op row is not one.
  static const char kSame[] =
      "#@\t4023129600\n"
      "2272060800\t10\n"
      "2287785600\t10\n";
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_leap_table_parse(kSame, sizeof(kSame) - 1, nullptr, &table, &err));
  EXPECT_EQ(GCHRON_DIAG_LEAP_TABLE_STEP, err.diag);

  // The first row is the baseline UTC started from, and may be any value.
  static const char kBaseline[] =
      "#@\t4023129600\n"
      "2272060800\t10\n"
      "2287785600\t11\n";
  EXPECT_EQ(GCHRON_OK,
      gchron_leap_table_parse(kBaseline, sizeof(kBaseline) - 1, nullptr,
          &table, &err));
  gchron_leap_table_destroy(table);
}

TEST(Leap, TheSecondANegativeLeapRemovesIsAGapNotAnInstant) {
  /*
   * A negative leap second takes the clock from 23:59:58 straight to
   * 00:00:00, so the Unix value in between names no instant. Converting it
   * anyway produced the same TAI second as its successor - two UTC values
   * colliding on one TAI value, on the scale whose whole purpose is that
   * nothing collides.
   *
   * GCHRON_ERR_GAP is what this library already says about a civil time
   * inside a daylight-saving gap, and it is the same question.
   *
   * No negative leap second has ever been issued, so this table is
   * synthesised. design.md section 5.1 item 5 is explicit that the library
   * must not be built around assuming one cannot happen.
   */
  static const char kNegative[] =
      "#@\t4023129600\n"
      "2272060800\t10\n"
      "2287785600\t11\n"
      "2303683200\t10\n";
  GCHRON_LeapTable * table = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_leap_table_parse(kNegative, sizeof(kNegative) - 1, nullptr,
          &table, nullptr));

  GCHRON_LeapEntry third{};
  ASSERT_EQ(GCHRON_OK, gchron_leap_table_entry(table, 2, &third));
  ASSERT_TRUE(third.negative);

  // The removed second is the one immediately before the row takes effect.
  GCHRON_Instant removed{ third.at.sec - 1, 0 };
  GCHRON_TaiInstant tai{};
  EXPECT_EQ(GCHRON_ERR_GAP,
      gchron_tai_from_instant(table, removed, false, &tai));

  // Its neighbours on both sides are ordinary instants that round-trip.
  for (int64_t delta : { INT64_C(-1), INT64_C(0) }) {
    GCHRON_Instant utc{ third.at.sec + delta, 250000000 };
    if (delta == -1) {
      continue; /* that is the removed one, checked above */
    }
    ASSERT_EQ(GCHRON_OK, gchron_tai_from_instant(table, utc, false, &tai));
    GCHRON_Instant back{};
    bool leap = true;
    ASSERT_EQ(GCHRON_OK, gchron_tai_to_instant(table, tai, &back, &leap));
    EXPECT_EQ(utc.sec, back.sec);
    EXPECT_FALSE(leap);
  }
  {
    GCHRON_Instant before{ third.at.sec - 2, 250000000 };
    ASSERT_EQ(GCHRON_OK, gchron_tai_from_instant(table, before, false, &tai));
    GCHRON_Instant back{};
    ASSERT_EQ(GCHRON_OK, gchron_tai_to_instant(table, tai, &back, nullptr));
    EXPECT_EQ(before.sec, back.sec);
  }

  gchron_leap_table_destroy(table);
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
