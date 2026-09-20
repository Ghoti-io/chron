/**
 * @file
 *
 * TOML v1.0.0's four date-time types.
 *
 * Reference: TOML v1.0.0, *Offset Date-Time*, *Local Date-Time*, *Local
 * Date*, *Local Time*. The examples in the tests below are the
 * specification's own.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

GCHRON_Result parse(const std::string & text, GCHRON_TomlValue * out,
    const GCHRON_ParseOptions * opts = nullptr,
    GCHRON_ParseInfo * info = nullptr, GCHRON_Error * err = nullptr) {
  return gchron_parse_toml(text.data(), text.size(), opts, out, info, err);
}

std::string write(const GCHRON_TomlValue & v, GCHRON_Result * result,
    const GCHRON_WriteOptions * opts = nullptr) {
  char buf[GCHRON_RFC3339_DATE_TIME_MAX];
  size_t len = 0;
  *result = gchron_write_toml(&v, opts, buf, sizeof(buf), &len);
  if (*result != GCHRON_OK) {
    return std::string();
  }
  return std::string(buf, len);
}

} // namespace

// TOML distinguishes its four types by which fields are present rather than
// by a tag, so a parser that reads any of them has to report which it read.
TEST(Toml, TheKindComesFromWhichFieldsArePresent) {
  GCHRON_TomlValue v;

  ASSERT_EQ(GCHRON_OK, parse("1979-05-27T07:32:00Z", &v));
  EXPECT_EQ(GCHRON_TOML_OFFSET_DATE_TIME, v.kind);

  ASSERT_EQ(GCHRON_OK, parse("1979-05-27T00:32:00-07:00", &v));
  EXPECT_EQ(GCHRON_TOML_OFFSET_DATE_TIME, v.kind);
  EXPECT_EQ(-7 * 3600, v.offset_sec);

  ASSERT_EQ(GCHRON_OK, parse("1979-05-27T07:32:00", &v));
  EXPECT_EQ(GCHRON_TOML_LOCAL_DATE_TIME, v.kind);
  EXPECT_EQ(0, v.offset_sec);

  ASSERT_EQ(GCHRON_OK, parse("1979-05-27", &v));
  EXPECT_EQ(GCHRON_TOML_LOCAL_DATE, v.kind);
  EXPECT_EQ(1979, v.civil.date.year);

  ASSERT_EQ(GCHRON_OK, parse("07:32:00", &v));
  EXPECT_EQ(GCHRON_TOML_LOCAL_TIME, v.kind);
  EXPECT_EQ(7, v.civil.time.hour);
}

// TOML's Local Time is the shape a YAML timestamp cannot hold, and the reason
// a library with only "date-time with an offset" cannot read TOML.
TEST(Toml, LocalTimeCarriesAFractionAndNoDate) {
  GCHRON_TomlValue v;
  ASSERT_EQ(GCHRON_OK, parse("00:32:00.999999", &v));
  EXPECT_EQ(GCHRON_TOML_LOCAL_TIME, v.kind);
  EXPECT_EQ(999999000, v.civil.time.nsec);
  EXPECT_FALSE(v.offset_unknown);

  // A trailing offset is trailing text, not an offset: a Local Time has none.
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("07:32:00Z", &v));
}

// TOML permits a space where RFC 3339 wants `T`, and the preset says so.
TEST(Toml, ASpaceSeparatorIsPermitted) {
  GCHRON_TomlValue v;
  ASSERT_EQ(GCHRON_OK, parse("1979-05-27 07:32:00Z", &v));
  EXPECT_EQ(GCHRON_TOML_OFFSET_DATE_TIME, v.kind);
  ASSERT_EQ(GCHRON_OK, parse("1979-05-27 07:32:00", &v));
  EXPECT_EQ(GCHRON_TOML_LOCAL_DATE_TIME, v.kind);

  // Passing the strict defaults explicitly turns it back off, which is how a
  // caller who wants RFC 3339 proper asks for it.
  GCHRON_ParseOptions strict;
  gchron_parse_options_default(&strict);
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("1979-05-27 07:32:00Z", &v, &strict));
}

// TOML 1.0.0: "If the value contains greater precision than the
// implementation can support, the additional precision must be truncated, not
// rounded." So NULL options here mean the TOML preset, not the strict one.
TEST(Toml, NullOptionsMeanTheTomlPresetAndSoTruncate) {
  GCHRON_TomlValue v;
  GCHRON_ParseInfo info;
  ASSERT_EQ(GCHRON_OK, parse("1979-05-27T07:32:00.9999999999Z", &v, nullptr,
      &info));
  EXPECT_TRUE(info.fraction_truncated);
  EXPECT_EQ(999999999, v.civil.time.nsec);

  GCHRON_ParseOptions strict;
  gchron_parse_options_default(&strict);
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      parse("1979-05-27T07:32:00.9999999999Z", &v, &strict));
}

TEST(Toml, SecondsAreRequiredInVersionOnePointZero) {
  GCHRON_TomlValue v;
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("07:32", &v));
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse("1979-05-27T07:32Z", &v));
}

TEST(Toml, TheUnknownOffsetSurvivesTheGrammar) {
  GCHRON_TomlValue v;
  ASSERT_EQ(GCHRON_OK, parse("1979-05-27T07:32:00-00:00", &v));
  EXPECT_TRUE(v.offset_unknown);
  ASSERT_EQ(GCHRON_OK, parse("1979-05-27T07:32:00+00:00", &v));
  EXPECT_FALSE(v.offset_unknown);
}

TEST(Toml, EachKindWritesBackAsItselfAndRoundTrips) {
  const char * texts[] = {
    "1979-05-27T07:32:00Z",
    "1979-05-27T00:32:00-07:00",
    "1979-05-27T07:32:00-00:00",
    "1979-05-27T07:32:00",
    "1979-05-27T07:32:00.999",
    "1979-05-27",
    "07:32:00",
    "00:32:00.999999",
  };
  for (const char * text : texts) {
    GCHRON_TomlValue v;
    ASSERT_EQ(GCHRON_OK, parse(text, &v)) << text;
    GCHRON_Result result;
    EXPECT_EQ(std::string(text), write(v, &result)) << text;
    EXPECT_EQ(GCHRON_OK, result) << text;
  }
}

TEST(Toml, TheWriterRefusesAValueWithNoKind) {
  GCHRON_TomlValue v{};
  GCHRON_Result result;
  write(v, &result);
  EXPECT_EQ(GCHRON_ERR_INVALID, result);
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_write_toml(nullptr, nullptr, nullptr, 0, nullptr));
}

TEST(Toml, NullArgumentsAreRefusedRatherThanDereferenced) {
  GCHRON_TomlValue v;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_parse_toml(nullptr, 0, nullptr, &v, nullptr, nullptr));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_parse_toml("x", 1, nullptr, nullptr, nullptr, nullptr));
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parse_toml("", 0, nullptr, &v, nullptr, nullptr));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
