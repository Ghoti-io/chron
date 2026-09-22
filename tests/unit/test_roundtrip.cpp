/**
 * @file
 *
 * The RFC 3339 writer, and property 3 of design.md section 12.1:
 * `parse(write(x))` is identical to `x` for every value the format can
 * represent losslessly, and where the format is lossy the loss is exactly the
 * documented one.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <random>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

std::string write_dt(const GCHRON_OffsetDateTime & odt, GCHRON_Result * result,
    const GCHRON_WriteOptions * opts = nullptr) {
  char buf[GCHRON_RFC3339_DATE_TIME_MAX];
  size_t len = 0;
  *result = gchron_write_rfc3339_date_time(&odt, opts, buf, sizeof(buf), &len);
  if (*result != GCHRON_OK) {
    return std::string();
  }
  EXPECT_EQ(len, std::strlen(buf));
  return std::string(buf, len);
}

GCHRON_OffsetDateTime parse_dt(const std::string & text,
    const GCHRON_ParseOptions * opts = nullptr) {
  GCHRON_OffsetDateTime odt{};
  EXPECT_EQ(GCHRON_OK,
      gchron_parse_rfc3339_date_time(text.data(), text.size(), opts, &odt,
          nullptr, nullptr));
  return odt;
}

} // namespace

// The shortest of 0, 3, 6 and 9 digits that loses nothing, which is what
// makes the round trip an identity rather than an approximation.
TEST(Write, TheAutomaticFractionIsTheShortestLosslessOne) {
  GCHRON_Result result;
  struct Case { int32_t nsec; const char * expected; };
  const Case cases[] = {
    { 0,         "2026-09-20T15:30:00Z" },
    { 500000000, "2026-09-20T15:30:00.5Z" },
    { 123000000, "2026-09-20T15:30:00.123Z" },
    { 123456000, "2026-09-20T15:30:00.123456Z" },
    { 123456789, "2026-09-20T15:30:00.123456789Z" },
    { 1,         "2026-09-20T15:30:00.000000001Z" },
  };
  for (const Case & c : cases) {
    GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 15, 30, 0,
        c.nsec);
    GCHRON_OffsetDateTime odt;
    ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, false, &odt));
    // 0.5 seconds is three digits "500", not one: the shortest of 0, 3, 6, 9.
    std::string text = write_dt(odt, &result);
    EXPECT_EQ(GCHRON_OK, result);
    if (c.nsec == 500000000) {
      EXPECT_EQ("2026-09-20T15:30:00.500Z", text);
    }
    else {
      EXPECT_EQ(std::string(c.expected), text);
    }
  }
}

// A fixed count smaller than the value needs truncates, and does so because
// it was asked to.
TEST(Write, AFixedFractionCountTruncatesBecauseItWasAskedTo) {
  GCHRON_DateTime civil =
      gchrontest::datetime(2026, 9, 20, 15, 30, 0, 123456789);
  GCHRON_OffsetDateTime odt;
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, false, &odt));
  GCHRON_WriteOptions opts;
  GCHRON_Result result;

  gchron_write_options_default(&opts);
  opts.fraction_digits = 3;
  EXPECT_EQ("2026-09-20T15:30:00.123Z", write_dt(odt, &result, &opts));

  opts.fraction_digits = GCHRON_FRACTION_DIGITS_NONE;
  EXPECT_EQ("2026-09-20T15:30:00Z", write_dt(odt, &result, &opts));

  opts.fraction_digits = 9;
  EXPECT_EQ("2026-09-20T15:30:00.123456789Z", write_dt(odt, &result, &opts));
}

// Mistake M14, on the way out: the one spelling that carries the meaning is
// written whatever the options say.
TEST(Write, AnUnknownOffsetIsAlwaysWrittenAsMinusZeroZero) {
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 15, 30, 0);
  GCHRON_OffsetDateTime odt;
  GCHRON_WriteOptions opts;
  GCHRON_Result result;
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, true, &odt));

  gchron_write_options_default(&opts);
  EXPECT_EQ("2026-09-20T15:30:00-00:00", write_dt(odt, &result, &opts));
  opts.zero_offset_as_numeric = true;
  EXPECT_EQ("2026-09-20T15:30:00-00:00", write_dt(odt, &result, &opts));
  opts.lowercase = true;
  EXPECT_EQ("2026-09-20t15:30:00-00:00", write_dt(odt, &result, &opts));
}

TEST(Write, AKnownZeroOffsetIsZUnlessTheCallerAsksForNumeric) {
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 15, 30, 0);
  GCHRON_OffsetDateTime odt;
  GCHRON_WriteOptions opts;
  GCHRON_Result result;
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, false, &odt));

  gchron_write_options_default(&opts);
  EXPECT_EQ("2026-09-20T15:30:00Z", write_dt(odt, &result, &opts));
  opts.zero_offset_as_numeric = true;
  EXPECT_EQ("2026-09-20T15:30:00+00:00", write_dt(odt, &result, &opts));
}

TEST(Write, ANegativeOffsetKeepsItsSignAndItsMinutes) {
  GCHRON_DateTime civil = gchrontest::datetime(1937, 1, 1, 12, 0, 27, 870000000);
  GCHRON_OffsetDateTime odt;
  GCHRON_Result result;
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 20 * 60, false, &odt));
  EXPECT_EQ("1937-01-01T12:00:27.870+00:20", write_dt(odt, &result));

  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, -8 * 3600 - 30 * 60,
      false, &odt));
  EXPECT_EQ("1937-01-01T12:00:27.870-08:30", write_dt(odt, &result));
}

// RFC 3339 has no expanded year: GCHRON_ERR_RANGE rather than a truncated or
// signed one, because a reader of `0020-01-01` cannot tell it was 20020.
TEST(Write, AYearRfc3339CannotSpellIsRefused) {
  GCHRON_DateTime civil = gchrontest::datetime(20020, 1, 1, 0, 0, 0);
  GCHRON_OffsetDateTime odt;
  GCHRON_Result result;
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, false, &odt));
  write_dt(odt, &result);
  EXPECT_EQ(GCHRON_ERR_RANGE, result);

  GCHRON_Date negative = gchrontest::date(-1, 1, 1);
  char buf[GCHRON_RFC3339_DATE_MAX];
  size_t len = 0;
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_write_rfc3339_full_date(&negative, buf, sizeof(buf), &len));

  GCHRON_Date year_zero = gchrontest::date(0, 1, 1);
  ASSERT_EQ(GCHRON_OK,
      gchron_write_rfc3339_full_date(&year_zero, buf, sizeof(buf), &len));
  EXPECT_STREQ("0000-01-01", buf);
}

// design.md section 8.6: a buffer too small is GCHRON_ERR_LIMIT with out_len
// set to the length the output would have had, so a zero-length buffer is how
// a caller asks for the length alone.
TEST(Write, ATooSmallBufferReportsTheLengthItNeeded) {
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 15, 30, 0);
  GCHRON_OffsetDateTime odt;
  size_t len = 0;
  char small[8];
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, false, &odt));

  EXPECT_EQ(GCHRON_ERR_LIMIT,
      gchron_write_rfc3339_date_time(&odt, nullptr, small, sizeof(small),
          &len));
  EXPECT_EQ(std::strlen("2026-09-20T15:30:00Z"), len);

  len = 0;
  EXPECT_EQ(GCHRON_ERR_LIMIT,
      gchron_write_rfc3339_date_time(&odt, nullptr, nullptr, 0, &len));
  EXPECT_EQ(std::strlen("2026-09-20T15:30:00Z"), len);

  // Exactly enough, including the terminating NUL.
  char exact[std::char_traits<char>::length("2026-09-20T15:30:00Z") + 1];
  EXPECT_EQ(GCHRON_OK,
      gchron_write_rfc3339_date_time(&odt, nullptr, exact, sizeof(exact),
          &len));
  // One byte short of that is not.
  EXPECT_EQ(GCHRON_ERR_LIMIT,
      gchron_write_rfc3339_date_time(&odt, nullptr, exact, sizeof(exact) - 1,
          &len));
}

TEST(Write, TheDeclaredMaximaHoldForTheLongestOutputOfEachGrammar) {
  GCHRON_DateTime civil =
      gchrontest::datetime(9999, 12, 31, 23, 59, 59, 123456789);
  GCHRON_OffsetDateTime odt;
  GCHRON_OffsetTime ot{};
  size_t len = 0;
  char buf[GCHRON_RFC3339_DATE_TIME_MAX];

  /*
   * -86340 is -23:59:00, the largest offset RFC 3339 can *write*. It used to
   * be -86399, which is -23:59:59 - a value the type holds and the grammar
   * has no seconds field for, and which the writer used to truncate to
   * `-23:59` while reporting success. It is refused now, so the longest
   * writable output is this one.
   */
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, -86340, false, &odt));
  ASSERT_EQ(GCHRON_OK,
      gchron_write_rfc3339_date_time(&odt, nullptr, buf, sizeof(buf), &len));
  EXPECT_LT(len, GCHRON_RFC3339_DATE_TIME_MAX);

  ot.time = civil.time;
  ot.offset_sec = -86340;
  ASSERT_EQ(GCHRON_OK,
      gchron_write_rfc3339_full_time(&ot, nullptr, buf,
          GCHRON_RFC3339_TIME_MAX, &len));
  EXPECT_LT(len, GCHRON_RFC3339_TIME_MAX);

  ASSERT_EQ(GCHRON_OK,
      gchron_write_rfc3339_full_date(&civil.date, buf,
          GCHRON_RFC3339_DATE_MAX, &len));
  EXPECT_LT(len, GCHRON_RFC3339_DATE_MAX);
}

/*
 * An offset the type can hold and the grammar cannot spell.
 *
 * GCHRON_OffsetDateTime keeps the offset in seconds on purpose: design.md
 * section 3.1 and offset.h give the same reason in the same words, that
 * Europe/Amsterdam kept local mean time at +00:19:32 until 1937 and an offset
 * type that cannot hold that cannot round-trip the zone's own history. RFC
 * 3339's `time-numoffset` is hours and minutes with no seconds field, so
 * there are values the type holds that this grammar cannot write.
 *
 * What the writer used to do was write the hours and minutes and drop the
 * seconds, and return GCHRON_OK. That is not a rounding of the text: it moves
 * the instant the text denotes, by up to 59 seconds, silently.
 * `1222-07-08T00:14:07Z[Europe/London]` written back out became
 * `1222-07-08T00:12:52-00:01`, fifteen seconds earlier, because London's LMT
 * was -00:01:15. Found by tests/fuzz/fuzz_textfmt.cpp.
 *
 * Note which of these is the loud one. RFC 9557 refuses to re-read its own
 * writer's output, because section 3.4 calls an offset disagreeing with its
 * zone inconsistent - so the round trip fails and something notices. Plain
 * RFC 3339 has no zone to disagree with, so it hands back a timestamp that is
 * simply fifteen seconds wrong, and nothing notices at all.
 */
TEST(Write, AnOffsetWithSecondsIsRefusedRatherThanTruncated) {
  GCHRON_DateTime civil = gchrontest::datetime(1222, 7, 8, 0, 12, 52, 0);
  GCHRON_OffsetDateTime odt;
  char buf[GCHRON_RFC3339_DATE_TIME_MAX];
  size_t len = 0;

  // London's local mean time, which is what sent this test looking.
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, -75, false, &odt));
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_write_rfc3339_date_time(&odt, nullptr, buf, sizeof(buf), &len));
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_write_rfc5322(&odt, buf, sizeof(buf), &len));

  // Amsterdam's, the one the design document names.
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 1172, false, &odt));
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_write_rfc3339_date_time(&odt, nullptr, buf, sizeof(buf), &len));

  // A whole minute of the same magnitude still writes.
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, -60, false, &odt));
  EXPECT_EQ(GCHRON_OK,
      gchron_write_rfc3339_date_time(&odt, nullptr, buf, sizeof(buf), &len));

  /*
   * `-00:00` is exempt. It is a statement that the offset is not known
   * (RFC 3339 section 4.3), not a magnitude, so there is nothing about it
   * that the grammar cannot express.
   */
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, true, &odt));
  EXPECT_EQ(GCHRON_OK,
      gchron_write_rfc3339_date_time(&odt, nullptr, buf, sizeof(buf), &len));
}

// Property 3 of design.md section 12.1, over random values rather than a
// handful of examples.
TEST(Write, ParseOfWriteIsIdenticalOverRandomValues) {
  std::mt19937_64 rng(20260920);
  std::uniform_int_distribution<int> years(1, 9999);
  std::uniform_int_distribution<int> months(1, 12);
  std::uniform_int_distribution<int> days(1, 28);
  std::uniform_int_distribution<int> hours(0, 23);
  std::uniform_int_distribution<int> minutes(0, 59);
  std::uniform_int_distribution<int32_t> nsecs(0, 999999999);
  std::uniform_int_distribution<int> offsets(-86399 / 60, 86399 / 60);

  for (int i = 0; i < 20000; ++i) {
    GCHRON_DateTime civil = gchrontest::datetime(years(rng), months(rng),
        days(rng), hours(rng), minutes(rng), minutes(rng), nsecs(rng));
    GCHRON_OffsetDateTime odt;
    ASSERT_EQ(GCHRON_OK,
        gchron_offset_create(&civil, offsets(rng) * 60, false, &odt));

    GCHRON_Result result;
    std::string text = write_dt(odt, &result);
    ASSERT_EQ(GCHRON_OK, result);

    // The writer's fraction is automatic, so the text is lossless and the
    // parser needs no truncation policy to read it back.
    GCHRON_OffsetDateTime back = parse_dt(text);
    ASSERT_TRUE(gchron_offset_identical(&odt, &back)) << text;
  }
}

// The one documented loss: a leap second read as `:60` writes back as `:59`,
// because that is what the value holds. GCHRON_ParseInfo::leap_second is the
// evidence, and it is the caller's to keep.
TEST(Write, ALeapSecondIsTheDocumentedLossInTheRoundTrip) {
  GCHRON_ParseOptions opts;
  GCHRON_ParseInfo info;
  GCHRON_OffsetDateTime odt;
  GCHRON_Result result;
  gchron_parse_options_json_schema(&opts);

  const char * text = "1998-12-31T23:59:60Z";
  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc3339_date_time(text, std::strlen(text), &opts, &odt,
          &info, nullptr));
  EXPECT_TRUE(info.leap_second);
  EXPECT_EQ("1998-12-31T23:59:59Z", write_dt(odt, &result));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
