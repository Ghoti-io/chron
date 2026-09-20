/**
 * @file
 *
 * Civil time with a fixed offset, and the two comparisons it needs.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdio>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

GCHRON_OffsetDateTime offset_dt(int32_t year, int month, int day, int hour,
    int minute, int second, int32_t offset_sec, bool unknown = false) {
  GCHRON_DateTime civil =
      gchrontest::datetime(year, month, day, hour, minute, second);
  GCHRON_OffsetDateTime odt{};
  EXPECT_EQ(GCHRON_OK,
      gchron_offset_create(&civil, offset_sec, unknown, &odt));
  return odt;
}

} // namespace

TEST(Offset, TheCivilReadingAndTheOffsetTogetherNameAnInstant) {
  GCHRON_OffsetDateTime odt = offset_dt(2026, 3, 8, 1, 30, 0, -5 * 3600);
  GCHRON_Instant i;
  GCHRON_DateTime utc;
  ASSERT_EQ(GCHRON_OK, gchron_offset_to_instant(&odt, &i));
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&i, &utc));
  EXPECT_EQ(6, utc.time.hour);
  EXPECT_EQ(30, utc.time.minute);
  EXPECT_EQ(8, utc.date.day);
}

// Mistake M16: `01:30-05:00` and `06:30Z` are the same moment, and the two
// questions about them have two names.
TEST(Offset, CompareOrdersByInstantAndIdenticalComparesTheFields) {
  GCHRON_OffsetDateTime local = offset_dt(2026, 3, 8, 1, 30, 0, -5 * 3600);
  GCHRON_OffsetDateTime utc = offset_dt(2026, 3, 8, 6, 30, 0, 0);

  EXPECT_EQ(0, gchron_offset_compare(&local, &utc));
  EXPECT_FALSE(gchron_offset_identical(&local, &utc));
  EXPECT_TRUE(gchron_offset_identical(&local, &local));

  GCHRON_OffsetDateTime later = offset_dt(2026, 3, 8, 6, 30, 1, 0);
  EXPECT_LT(gchron_offset_compare(&utc, &later), 0);
  EXPECT_GT(gchron_offset_compare(&later, &utc), 0);

  EXPECT_LT(gchron_offset_compare(nullptr, &utc), 0);
  EXPECT_GT(gchron_offset_compare(&utc, nullptr), 0);
  EXPECT_EQ(0, gchron_offset_compare(nullptr, nullptr));
  EXPECT_FALSE(gchron_offset_identical(&utc, nullptr));
  EXPECT_TRUE(gchron_offset_identical(nullptr, nullptr));
}

// Mistake M14: RFC 3339 section 4.3 gives `-00:00` a meaning that `Z` and
// `+00:00` do not have, and a value that has lost the distinction cannot say
// which it was.
TEST(Offset, UnknownIsDistinctFromAKnownZeroOffset) {
  GCHRON_OffsetDateTime known = offset_dt(2026, 9, 20, 12, 0, 0, 0, false);
  GCHRON_OffsetDateTime unknown = offset_dt(2026, 9, 20, 12, 0, 0, 0, true);

  // The same moment.
  EXPECT_EQ(0, gchron_offset_compare(&known, &unknown));
  // Not the same statement.
  EXPECT_FALSE(gchron_offset_identical(&known, &unknown));
}

// "Unknown" is a thing only zero can be: no text can write a non-zero offset
// and also say the offset is unknown.
TEST(Offset, UnknownWithANonZeroOffsetIsRefused) {
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 12, 0, 0);
  GCHRON_OffsetDateTime out;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_offset_create(&civil, 3600, true, &out));
  EXPECT_EQ(GCHRON_OK, gchron_offset_create(&civil, 3600, false, &out));
}

TEST(Offset, CreateRefusesAnOffsetOfTwentyFourHoursOrMore) {
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 12, 0, 0);
  GCHRON_OffsetDateTime out;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_offset_create(&civil, 86400, false, &out));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_offset_create(&civil, -86400, false, &out));
  EXPECT_EQ(GCHRON_OK, gchron_offset_create(&civil, 86399, false, &out));
  EXPECT_EQ(GCHRON_OK, gchron_offset_create(&civil, -86399, false, &out));
}

// The tzdb records Europe/Amsterdam at +00:19:32 until 1937. An offset type
// held in minutes cannot round-trip a zone's own history, which is why this
// one is in seconds.
TEST(Offset, SecondsRatherThanMinutesHoldsLocalMeanTime) {
  GCHRON_DateTime civil = gchrontest::datetime(1930, 6, 1, 12, 0, 0);
  GCHRON_OffsetDateTime odt;
  ASSERT_EQ(GCHRON_OK,
      gchron_offset_create(&civil, 19 * 60 + 32, false, &odt));
  GCHRON_Instant i;
  ASSERT_EQ(GCHRON_OK, gchron_offset_to_instant(&odt, &i));
  GCHRON_DateTime utc;
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&i, &utc));
  EXPECT_EQ(11, utc.time.hour);
  EXPECT_EQ(40, utc.time.minute);
  EXPECT_EQ(28, utc.time.second);
}

TEST(Offset, FromInstantAndToInstantAreInverses) {
  GCHRON_Instant i{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1758382200, 123456789, &i));
  GCHRON_OffsetDateTime odt;
  GCHRON_Instant back;
  ASSERT_EQ(GCHRON_OK,
      gchron_offset_from_instant(&i, 2 * 3600, false, &odt));
  ASSERT_EQ(GCHRON_OK, gchron_offset_to_instant(&odt, &back));
  EXPECT_EQ(0, gchron_instant_compare(&i, &back));
  EXPECT_EQ(123456789, odt.civil.time.nsec);
}

// Rewriting the offset is the operation that makes "the offset was unknown"
// no longer true, so the flag does not travel.
TEST(Offset, WithOffsetKeepsTheInstantAndDropsTheUnknownFlag) {
  GCHRON_OffsetDateTime unknown = offset_dt(2026, 9, 20, 12, 0, 0, 0, true);
  GCHRON_OffsetDateTime rewritten;
  ASSERT_EQ(GCHRON_OK,
      gchron_offset_with_offset(&unknown, 2 * 3600, &rewritten));
  EXPECT_EQ(0, gchron_offset_compare(&unknown, &rewritten));
  EXPECT_EQ(14, rewritten.civil.time.hour);
  EXPECT_FALSE(rewritten.offset_unknown);

  GCHRON_OffsetDateTime back;
  ASSERT_EQ(GCHRON_OK, gchron_offset_with_offset(&rewritten, 0, &back));
  EXPECT_FALSE(back.offset_unknown);
}

TEST(Offset, OffsetTimeCarriesTheSameInvariants) {
  GCHRON_OffsetTime a{};
  a.time = gchrontest::timeofday(8, 30, 6);
  a.offset_sec = -8 * 3600;
  a.offset_unknown = false;
  EXPECT_TRUE(gchron_offset_time_is_valid(&a));

  GCHRON_OffsetTime b = a;
  EXPECT_TRUE(gchron_offset_time_identical(&a, &b));
  b.offset_sec = 0;
  EXPECT_FALSE(gchron_offset_time_identical(&a, &b));

  GCHRON_OffsetTime bad = a;
  bad.offset_sec = 86400;
  EXPECT_FALSE(gchron_offset_time_is_valid(&bad));
  bad = a;
  bad.offset_sec = 0;
  bad.offset_unknown = true;
  EXPECT_TRUE(gchron_offset_time_is_valid(&bad));
  bad.offset_sec = 60;
  EXPECT_FALSE(gchron_offset_time_is_valid(&bad));

  EXPECT_FALSE(gchron_offset_time_is_valid(nullptr));
  EXPECT_TRUE(gchron_offset_time_identical(nullptr, nullptr));
  EXPECT_FALSE(gchron_offset_time_identical(&a, nullptr));
}

TEST(Offset, DumpWritesSomethingForEveryInput) {
  GCHRON_OffsetDateTime odt = offset_dt(2026, 9, 20, 12, 0, 0, 0);
  FILE * sink = std::tmpfile();
  ASSERT_NE(nullptr, sink);
  gchron_offset_dump(&odt, sink);
  gchron_offset_dump(nullptr, sink);
  gchron_offset_dump(&odt, nullptr);
  EXPECT_GT(std::ftell(sink), 0);
  std::fclose(sink);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
