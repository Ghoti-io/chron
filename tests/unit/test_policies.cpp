/**
 * @file
 *
 * The rule of design.md section 3.7: every policy enum's zero value refuses.
 *
 * An options struct a caller zero-initialised and forgot a field of therefore
 * *refuses* the ambiguous case instead of guessing at it, and the convenient
 * behaviour is always the one a caller has to ask for by name. This is the
 * single rule that most distinguishes a correctness-first time library from a
 * convenient one, so it gets a test file of its own rather than a line inside
 * somebody else's.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

TEST(Policies, EveryRejectConstantIsZero) {
  EXPECT_EQ(0, GCHRON_FRACTION_REJECT);
  EXPECT_EQ(0, GCHRON_LEAP_REJECT);
  EXPECT_EQ(0, GCHRON_OVERFLOW_REJECT);
  // GCHRON_RESOLVE_REJECT and GCHRON_ZONECONFLICT_REJECT arrive with zone.h
  // in phase 1 and join this list then.
}

TEST(Policies, ZeroInitialisedParseOptionsAreTheStrictDefaults) {
  GCHRON_ParseOptions zeroed;
  GCHRON_ParseOptions named;
  std::memset(&zeroed, 0, sizeof(zeroed));
  gchron_parse_options_default(&named);
  // Field by field, not memcmp: the struct has padding, and what the padding
  // holds is not part of the value. That is the same reason the header says
  // memcmp is the wrong way to compare any type in this library.
  EXPECT_EQ(zeroed.limits, named.limits);
  EXPECT_EQ(zeroed.fraction, named.fraction);
  EXPECT_EQ(zeroed.leap, named.leap);
  EXPECT_EQ(zeroed.allow_space_separator, named.allow_space_separator);
  EXPECT_EQ(zeroed.allow_trailing, named.allow_trailing);
}

// A caller who zero-initialised the options and forgot the fraction field
// must be refused rather than have nine digits chosen for them.
TEST(Policies, ZeroInitialisedOptionsRefuseALongFraction) {
  GCHRON_ParseOptions opts;
  GCHRON_OffsetTime out;
  std::memset(&opts, 0, sizeof(opts));

  const char * text = "00:59:59.999999999999999Z";
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parse_rfc3339_full_time(text, std::strlen(text), &opts, &out,
          nullptr, nullptr));
}

// The same for a leap second.
TEST(Policies, ZeroInitialisedOptionsRefuseALeapSecond) {
  GCHRON_ParseOptions opts;
  GCHRON_OffsetDateTime out;
  std::memset(&opts, 0, sizeof(opts));

  const char * text = "1998-12-31T23:59:60Z";
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parse_rfc3339_date_time(text, std::strlen(text), &opts, &out,
          nullptr, nullptr));
}

// GCHRON_LEAP_TABLE needs leap.h, which is phase 4. It must refuse rather
// than quietly behave as GCHRON_LEAP_MINUTE: a caller who asked for the
// strict reading and silently got the loose one has a check that passes for
// the wrong reason.
TEST(Policies, LeapTableRefusesUntilTheTableExists) {
  GCHRON_ParseOptions opts;
  GCHRON_OffsetDateTime out;
  gchron_parse_options_default(&opts);
  opts.leap = GCHRON_LEAP_TABLE;

  const char * text = "1998-12-31T23:59:60Z";
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_parse_rfc3339_date_time(text, std::strlen(text), &opts, &out,
          nullptr, nullptr));
}

TEST(Policies, JsonSchemaPresetIsTruncateAndMinute) {
  GCHRON_ParseOptions opts;
  gchron_parse_options_json_schema(&opts);
  EXPECT_EQ(GCHRON_FRACTION_TRUNCATE, opts.fraction);
  EXPECT_EQ(GCHRON_LEAP_MINUTE, opts.leap);
  EXPECT_FALSE(opts.allow_space_separator);
  EXPECT_FALSE(opts.allow_trailing);
}

TEST(Policies, TomlPresetTruncatesAndAllowsASpace) {
  GCHRON_ParseOptions opts;
  gchron_parse_options_toml(&opts);
  EXPECT_EQ(GCHRON_FRACTION_TRUNCATE, opts.fraction);
  EXPECT_TRUE(opts.allow_space_separator);
  // TOML 1.0.0 says nothing about leap seconds, so the strict default stands.
  EXPECT_EQ(GCHRON_LEAP_REJECT, opts.leap);
}

TEST(Policies, ZeroInitialisedWriteOptionsLoseNothing) {
  GCHRON_WriteOptions zeroed;
  GCHRON_WriteOptions named;
  std::memset(&zeroed, 0, sizeof(zeroed));
  gchron_write_options_default(&named);
  EXPECT_EQ(zeroed.fraction_digits, named.fraction_digits);
  EXPECT_EQ(zeroed.lowercase, named.lowercase);
  EXPECT_EQ(zeroed.space_separator, named.space_separator);
  EXPECT_EQ(zeroed.zero_offset_as_numeric, named.zero_offset_as_numeric);
  EXPECT_EQ(GCHRON_FRACTION_DIGITS_AUTO, named.fraction_digits);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
