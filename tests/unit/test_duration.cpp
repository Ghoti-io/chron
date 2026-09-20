/**
 * @file
 *
 * The duration type's invariants, and the RFC 3339 appendix A grammar.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdio>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

GCHRON_Result parse(const std::string & text, GCHRON_Duration * out,
    GCHRON_Error * err = nullptr) {
  return gchron_parse_rfc3339_duration(text.data(), text.size(), nullptr, out,
      nullptr, err);
}

std::string write(const GCHRON_Duration & d, GCHRON_Result * result) {
  char buf[GCHRON_RFC3339_DURATION_MAX];
  size_t len = 0;
  *result = gchron_write_rfc3339_duration(&d, buf, sizeof(buf), &len);
  if (*result != GCHRON_OK) {
    return std::string();
  }
  return std::string(buf, len);
}

} // namespace

// design.md section 4.1: "one month minus one day" is not a duration; it is
// two operations, and writing it as one is how `Jan 31 + P1M-1D` came to mean
// four different things in four libraries.
TEST(Duration, FieldsOfDisagreeingSignAreNotADuration) {
  GCHRON_Duration d{};
  d.months = 1;
  d.days = -1;
  EXPECT_FALSE(gchron_duration_is_valid(&d));

  d = GCHRON_Duration{};
  d.months = -1;
  d.days = -1;
  EXPECT_TRUE(gchron_duration_is_valid(&d));
  EXPECT_EQ(-1, gchron_duration_sign(&d));

  d = GCHRON_Duration{};
  EXPECT_TRUE(gchron_duration_is_valid(&d));
  EXPECT_EQ(0, gchron_duration_sign(&d));
  EXPECT_FALSE(gchron_duration_is_valid(nullptr));
}

// The sub-second part carries no sign of its own: -0.5s is
// {seconds: -1, nsec: 500000000}, the same normalisation the instant uses.
TEST(Duration, NanosecondsAreAlwaysNonNegative) {
  GCHRON_Duration d{};
  d.seconds = -1;
  d.nsec = 500000000;
  EXPECT_TRUE(gchron_duration_is_valid(&d));
  EXPECT_EQ(-1, gchron_duration_sign(&d));

  // A positive fraction with a negative calendar unit and no seconds field to
  // carry the sign is two durations pretending to be one.
  d = GCHRON_Duration{};
  d.years = -1;
  d.nsec = 1;
  EXPECT_FALSE(gchron_duration_is_valid(&d));

  d = GCHRON_Duration{};
  d.nsec = 1000000000;
  EXPECT_FALSE(gchron_duration_is_valid(&d));
}

TEST(Duration, NegateRoundTripsAndReportsTheOneValueThatCannot) {
  GCHRON_Duration d{};
  GCHRON_Duration negated;
  GCHRON_Duration back;

  d.seconds = 3;
  d.nsec = 500000000;
  ASSERT_EQ(GCHRON_OK, gchron_duration_negate(&d, &negated));
  EXPECT_EQ(-4, negated.seconds);
  EXPECT_EQ(500000000, negated.nsec);
  ASSERT_EQ(GCHRON_OK, gchron_duration_negate(&negated, &back));
  EXPECT_TRUE(gchron_duration_identical(&d, &back));

  d = GCHRON_Duration{};
  d.years = INT64_MIN;
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_duration_negate(&d, &negated));
}

// PT90M and PT1H30M are the same length and are not identical, and the
// library never rewrites one as the other unless asked (section 4.2).
TEST(Duration, IdenticalIsNotTheSameQuestionAsTheSameLength) {
  GCHRON_Duration ninety{};
  GCHRON_Duration hour_and_half{};
  ninety.minutes = 90;
  hour_and_half.hours = 1;
  hour_and_half.minutes = 30;

  EXPECT_FALSE(gchron_duration_identical(&ninety, &hour_and_half));

  int64_t a = 0;
  int64_t b = 0;
  ASSERT_EQ(GCHRON_OK,
      gchron_duration_to_exact_seconds(&ninety, &a, nullptr));
  ASSERT_EQ(GCHRON_OK,
      gchron_duration_to_exact_seconds(&hour_and_half, &b, nullptr));
  EXPECT_EQ(a, b);
}

TEST(Duration, ExactSecondsRefusesACalendarUnitAndReportsOverflow) {
  GCHRON_Duration d{};
  int64_t seconds = 0;

  d.days = 1;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_duration_to_exact_seconds(&d, &seconds, nullptr));

  d = GCHRON_Duration{};
  d.hours = INT64_MAX;
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_duration_to_exact_seconds(&d, &seconds, nullptr));
}

/*
 * RFC 3339 appendix A's productions nest, so a duration is not just "numbers
 * and unit letters until they run out". The cases below are the ones that
 * distinguish the two readings; the JSON Schema conformance runner checks
 * the rest of the suite.
 */
TEST(Duration, AppendixANestingRefusesUnitsThatSkipALevel) {
  GCHRON_Duration d;

  // dur-year = 1*DIGIT "Y" [dur-month]: days cannot follow years directly.
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("P1Y2D", &d));
  EXPECT_EQ(GCHRON_OK, parse("P1Y0M2D", &d));
  EXPECT_EQ(GCHRON_OK, parse("P1Y2M", &d));
  EXPECT_EQ(GCHRON_OK, parse("P1M2D", &d));

  // dur-hour = 1*DIGIT "H" [dur-minute]: seconds cannot follow hours.
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("PT1H2S", &d));
  EXPECT_EQ(GCHRON_OK, parse("PT1H0M2S", &d));
  EXPECT_EQ(GCHRON_OK, parse("PT1M2S", &d));
  EXPECT_EQ(GCHRON_OK, parse("PT1H2M", &d));
}

TEST(Duration, WeeksStandAlone) {
  GCHRON_Duration d;
  GCHRON_Error err;

  ASSERT_EQ(GCHRON_OK, parse("P2W", &d));
  EXPECT_EQ(2, d.weeks);

  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("P1Y2W", &d, &err));
  EXPECT_EQ(GCHRON_DIAG_DURATION_WEEK_COMBINED, err.diag);
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("P0Y1W", &d, &err));
  EXPECT_EQ(GCHRON_DIAG_DURATION_WEEK_COMBINED, err.diag);
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("P1WT1H", &d, &err));
  EXPECT_EQ(GCHRON_DIAG_DURATION_WEEK_COMBINED, err.diag);
}

TEST(Duration, AppendixAHasNoSignAndNoFraction) {
  GCHRON_Duration d;
  GCHRON_Error err;

  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("-P1D", &d, &err));
  EXPECT_EQ(GCHRON_DIAG_DURATION_SIGN, err.diag);
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("P-1D", &d, &err));
  EXPECT_EQ(GCHRON_DIAG_DURATION_SIGN, err.diag);
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("PT0.5S", &d, &err));
  EXPECT_EQ(GCHRON_DIAG_DURATION_FRACTION, err.diag);
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("PT0,5S", &d, &err));
  EXPECT_EQ(GCHRON_DIAG_DURATION_FRACTION, err.diag);
}

// The text *is* a duration; no integer can hold it. GCHRON_ERR_RANGE and not
// GCHRON_ERR_FORMAT, so that a `format` check agrees with every other
// implementation while a caller wanting the value does not.
TEST(Duration, AComponentTooLargeForAnIntegerIsRangeNotFormat) {
  GCHRON_Duration d;
  EXPECT_EQ(GCHRON_ERR_RANGE,
      parse("P999999999999999999999999999999D", &d));
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("P999999999999999999999999999999", &d));
}

TEST(Duration, TheShortestZeroIsPTZeroS) {
  GCHRON_Duration d{};
  GCHRON_Result result;
  EXPECT_EQ("PT0S", write(d, &result));
  EXPECT_EQ(GCHRON_OK, result);

  GCHRON_Duration back;
  ASSERT_EQ(GCHRON_OK, parse("PT0S", &back));
  EXPECT_TRUE(gchron_duration_identical(&d, &back));
}

// `dur-year = 1*DIGIT "Y" [dur-month]` means a duration of one year and two
// days has no spelling that skips the months, so the writer does not try.
TEST(Duration, TheWriterEmitsTheIntermediateZerosTheGrammarNeeds) {
  GCHRON_Duration d{};
  GCHRON_Result result;

  d.years = 1;
  d.days = 2;
  EXPECT_EQ("P1Y0M2D", write(d, &result));

  d = GCHRON_Duration{};
  d.hours = 1;
  d.seconds = 2;
  EXPECT_EQ("PT1H0M2S", write(d, &result));

  d = GCHRON_Duration{};
  d.years = 1;
  d.months = 2;
  d.days = 3;
  d.hours = 4;
  d.minutes = 5;
  d.seconds = 6;
  EXPECT_EQ("P1Y2M3DT4H5M6S", write(d, &result));
}

TEST(Duration, TheWriterRefusesWhatAppendixACannotSpell) {
  GCHRON_Duration d{};
  GCHRON_Result result;

  d.days = -1;
  write(d, &result);
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, result);

  d = GCHRON_Duration{};
  d.seconds = 1;
  d.nsec = 500000000;
  write(d, &result);
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, result);

  d = GCHRON_Duration{};
  d.weeks = 1;
  d.days = 1;
  write(d, &result);
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, result);
}

TEST(Duration, TheWriterReportsTheLengthWhenTheBufferIsTooSmall) {
  GCHRON_Duration d{};
  d.days = 4;
  d.hours = 12;
  d.minutes = 30;
  d.seconds = 5;
  size_t len = 0;
  char small[4];
  EXPECT_EQ(GCHRON_ERR_LIMIT,
      gchron_write_rfc3339_duration(&d, small, sizeof(small), &len));
  EXPECT_EQ(std::strlen("P4DT12H30M5S"), len);

  // A zero-length buffer is how a caller asks for the length alone.
  len = 0;
  EXPECT_EQ(GCHRON_ERR_LIMIT,
      gchron_write_rfc3339_duration(&d, nullptr, 0, &len));
  EXPECT_EQ(std::strlen("P4DT12H30M5S"), len);

  char big[GCHRON_RFC3339_DURATION_MAX];
  ASSERT_EQ(GCHRON_OK,
      gchron_write_rfc3339_duration(&d, big, sizeof(big), &len));
  EXPECT_STREQ("P4DT12H30M5S", big);
}

// Property 3 of design.md section 12.1, for this grammar.
TEST(Duration, ParseOfWriteIsTheIdentityForEverythingAppendixACanSpell) {
  const char * texts[] = {
    "PT0S", "P4Y", "P1M", "PT1M", "PT36H", "P1DT12H", "P2W",
    "P1Y2M3DT4H5M6S", "P1Y2M3D", "PT1H2M3S", "P1M2D", "PT1H30M",
    "P10Y10M10DT10H10M10S", "P1Y2M", "PT1H2M", "PT1M2S", "P1Y0M2D",
    "PT1H0M2S",
  };
  for (const char * text : texts) {
    GCHRON_Duration d;
    ASSERT_EQ(GCHRON_OK, parse(text, &d)) << text;
    GCHRON_Result result;
    EXPECT_EQ(std::string(text), write(d, &result)) << text;
    EXPECT_EQ(GCHRON_OK, result) << text;
  }
}

// The one place the round trip is lossy, stated rather than hidden: a
// duration of zero days *is* the zero duration - the value type has one zero,
// however the text spelled it - and the writer's spelling of that is `PT0S`.
TEST(Duration, EveryZeroSpellingParsesToOneZeroAndWritesAsPTZeroS) {
  const char * zeroes[] = { "PT0S", "P0D", "P0Y", "P0W", "PT0M", "PT0H" };
  GCHRON_Duration zero{};
  for (const char * text : zeroes) {
    GCHRON_Duration d;
    ASSERT_EQ(GCHRON_OK, parse(text, &d)) << text;
    EXPECT_TRUE(gchron_duration_identical(&zero, &d)) << text;
    GCHRON_Result result;
    EXPECT_EQ("PT0S", write(d, &result)) << text;
  }
}

TEST(Duration, DumpWritesSomethingForEveryInput) {
  GCHRON_Duration d{};
  d.days = 1;
  FILE * sink = std::tmpfile();
  ASSERT_NE(nullptr, sink);
  gchron_duration_dump(&d, sink);
  gchron_duration_dump(nullptr, sink);
  gchron_duration_dump(&d, nullptr);
  EXPECT_GT(std::ftell(sink), 0);
  std::fclose(sink);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
