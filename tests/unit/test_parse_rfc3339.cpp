/**
 * @file
 *
 * RFC 3339 section 5.6, and the policies that govern what it accepts.
 *
 * The JSON Schema conformance runner checks the whole of the suite's optional
 * format vectors; what is here is the behaviour no vector covers - the error
 * positions, the diagnostics, the limits, and the facts a parse records in
 * GCHRON_ParseInfo that the value itself cannot hold.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

GCHRON_Result parse_dt(const std::string & text, GCHRON_OffsetDateTime * out,
    const GCHRON_ParseOptions * opts = nullptr,
    GCHRON_ParseInfo * info = nullptr, GCHRON_Error * err = nullptr) {
  return gchron_parse_rfc3339_date_time(text.data(), text.size(), opts, out,
      info, err);
}

GCHRON_ParseOptions json_schema() {
  GCHRON_ParseOptions opts;
  gchron_parse_options_json_schema(&opts);
  return opts;
}

} // namespace

TEST(Rfc3339, ParsesTheGrammarsOwnExample) {
  GCHRON_OffsetDateTime odt;
  GCHRON_ParseInfo info;
  ASSERT_EQ(GCHRON_OK,
      parse_dt("1985-04-12T23:20:50.52Z", &odt, nullptr, &info));
  EXPECT_EQ(1985, odt.civil.date.year);
  EXPECT_EQ(4, odt.civil.date.month);
  EXPECT_EQ(12, odt.civil.date.day);
  EXPECT_EQ(23, odt.civil.time.hour);
  EXPECT_EQ(20, odt.civil.time.minute);
  EXPECT_EQ(50, odt.civil.time.second);
  EXPECT_EQ(520000000, odt.civil.time.nsec);
  EXPECT_EQ(0, odt.offset_sec);
  EXPECT_FALSE(odt.offset_unknown);
  EXPECT_EQ(2, info.fraction_digits);
  EXPECT_EQ(std::strlen("1985-04-12T23:20:50.52Z"), info.consumed);
}

// RFC 5234 section 2.3 makes ABNF string literals case-insensitive, so `t`
// and `z` are as conformant as `T` and `Z` and need no option. A `format`
// check that refused them would disagree with the suite.
TEST(Rfc3339, LowercaseTAndZNeedNoOption) {
  GCHRON_OffsetDateTime odt;
  ASSERT_EQ(GCHRON_OK, parse_dt("1963-06-19t08:30:06.283185z", &odt));
  EXPECT_EQ(8, odt.civil.time.hour);
  EXPECT_EQ(283185000, odt.civil.time.nsec);
}

// The space is RFC 3339 section 5.6's *note*, not its ABNF, so it is an
// option and is off by default.
TEST(Rfc3339, TheSpaceSeparatorIsAnOptionAndIsOffByDefault) {
  GCHRON_OffsetDateTime odt;
  GCHRON_Error err;
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      parse_dt("1963-06-19 08:30:06Z", &odt, nullptr, nullptr, &err));
  EXPECT_EQ(GCHRON_DIAG_EXPECTED_SEPARATOR, err.diag);
  EXPECT_EQ(10u, err.offset);

  GCHRON_ParseOptions opts;
  gchron_parse_options_default(&opts);
  opts.allow_space_separator = true;
  EXPECT_EQ(GCHRON_OK, parse_dt("1963-06-19 08:30:06Z", &odt, &opts));
}

// Mistake M14: the flag is set by the parser and survives.
TEST(Rfc3339, MinusZeroZeroSetsTheUnknownOffsetFlag) {
  GCHRON_OffsetDateTime odt;
  GCHRON_ParseInfo info;
  ASSERT_EQ(GCHRON_OK,
      parse_dt("2026-09-20T12:00:00-00:00", &odt, nullptr, &info));
  EXPECT_TRUE(odt.offset_unknown);
  EXPECT_TRUE(info.offset_unknown);
  EXPECT_EQ(0, odt.offset_sec);

  ASSERT_EQ(GCHRON_OK,
      parse_dt("2026-09-20T12:00:00+00:00", &odt, nullptr, &info));
  EXPECT_FALSE(odt.offset_unknown);
  ASSERT_EQ(GCHRON_OK, parse_dt("2026-09-20T12:00:00Z", &odt, nullptr, &info));
  EXPECT_FALSE(odt.offset_unknown);
}

// design.md section 5.4: GCHRON_LEAP_MINUTE's question - "is this 23:59 in
// UTC?" - cannot be answered until the offset is known, which is why the
// check happens after the offset is read rather than where `:60` was written.
TEST(Rfc3339, LeapMinuteJudgesTheSecondAfterApplyingTheOffset) {
  GCHRON_ParseOptions opts = json_schema();
  GCHRON_OffsetDateTime odt;
  GCHRON_ParseInfo info;

  ASSERT_EQ(GCHRON_OK,
      parse_dt("1998-12-31T23:59:60Z", &odt, &opts, &info));
  EXPECT_TRUE(info.leap_second);
  // The value holds :59 of the same minute; the flag is the evidence that the
  // text said otherwise.
  EXPECT_EQ(59, odt.civil.time.second);

  ASSERT_EQ(GCHRON_OK,
      parse_dt("1998-12-31T15:59:60.123-08:00", &odt, &opts, &info));
  EXPECT_TRUE(info.leap_second);

  // The wrong minute, the wrong hour, and a second past the leap.
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse_dt("1998-12-31T23:58:60Z", &odt, &opts));
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse_dt("1998-12-31T22:59:60Z", &odt, &opts));
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse_dt("1998-12-31T23:59:61Z", &odt, &opts));
  // Hour 24 is invalid even beside a leap second.
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      parse_dt("2016-12-31T24:59:60+01:00", &odt, &opts));
}

TEST(Rfc3339, LeapClampAcceptsSixtyAnywhereAndStillFlagsIt) {
  GCHRON_ParseOptions opts;
  GCHRON_OffsetDateTime odt;
  GCHRON_ParseInfo info;
  gchron_parse_options_default(&opts);
  opts.leap = GCHRON_LEAP_CLAMP;

  ASSERT_EQ(GCHRON_OK, parse_dt("1998-12-31T22:59:60Z", &odt, &opts, &info));
  EXPECT_TRUE(info.leap_second);
  EXPECT_EQ(59, odt.civil.time.second);
  // 61 is out of the grammar's range whatever the policy.
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse_dt("1998-12-31T23:59:61Z", &odt, &opts));
}

TEST(Rfc3339, TruncationIsRecordedAndNeverRounds) {
  GCHRON_ParseOptions opts = json_schema();
  GCHRON_OffsetDateTime odt;
  GCHRON_ParseInfo info;

  ASSERT_EQ(GCHRON_OK,
      parse_dt("1985-04-12T00:59:59.999999999999999Z", &odt, &opts, &info));
  EXPECT_TRUE(info.fraction_truncated);
  EXPECT_EQ(15, info.fraction_digits);
  // Truncated, not rounded: rounding would have carried into the next day.
  EXPECT_EQ(999999999, odt.civil.time.nsec);
  EXPECT_EQ(59, odt.civil.time.second);

  ASSERT_EQ(GCHRON_OK,
      parse_dt("1985-04-12T00:00:00.000000000999Z", &odt, &opts, &info));
  EXPECT_TRUE(info.fraction_truncated);
  EXPECT_EQ(0, odt.civil.time.nsec);
}

TEST(Rfc3339, TheYearIsExactlyFourDigitsWithNoSign) {
  GCHRON_OffsetDateTime odt;
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      parse_dt("+11963-06-19T08:30:06.283185Z", &odt));
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse_dt("963-06-19T08:30:06Z", &odt));
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse_dt("11963-06-19T08:30:06Z", &odt));
  EXPECT_EQ(GCHRON_OK, parse_dt("0001-01-01T00:00:00Z", &odt));
}

TEST(Rfc3339, TheDiagnosticNamesTheFieldAndThePositionUnderlinesIt) {
  GCHRON_OffsetDateTime odt;
  GCHRON_Error err;

  EXPECT_EQ(GCHRON_ERR_FORMAT,
      parse_dt("1990-02-31T15:59:59Z", &odt, nullptr, nullptr, &err));
  EXPECT_EQ(GCHRON_DIAG_DAY_OUT_OF_RANGE, err.diag);
  EXPECT_EQ(8u, err.offset);
  EXPECT_EQ(2u, err.length);
  EXPECT_NE(nullptr, err.message);

  EXPECT_EQ(GCHRON_ERR_FORMAT,
      parse_dt("1990-13-01T15:59:59Z", &odt, nullptr, nullptr, &err));
  EXPECT_EQ(GCHRON_DIAG_MONTH_OUT_OF_RANGE, err.diag);
  EXPECT_EQ(5u, err.offset);

  EXPECT_EQ(GCHRON_ERR_FORMAT,
      parse_dt("1990-12-31T24:00:00Z", &odt, nullptr, nullptr, &err));
  EXPECT_EQ(GCHRON_DIAG_HOUR_OUT_OF_RANGE, err.diag);
  EXPECT_EQ(11u, err.offset);

  EXPECT_EQ(GCHRON_ERR_FORMAT,
      parse_dt("1990-12-31T15:60:00Z", &odt, nullptr, nullptr, &err));
  EXPECT_EQ(GCHRON_DIAG_MINUTE_OUT_OF_RANGE, err.diag);

  EXPECT_EQ(GCHRON_ERR_FORMAT,
      parse_dt("1990-12-31T15:59:59-24:00", &odt, nullptr, nullptr, &err));
  EXPECT_EQ(GCHRON_DIAG_OFFSET_OUT_OF_RANGE, err.diag);

  EXPECT_EQ(GCHRON_ERR_FORMAT,
      parse_dt("1990-12-31T10:00:00+10:60", &odt, nullptr, nullptr, &err));
  EXPECT_EQ(GCHRON_DIAG_OFFSET_OUT_OF_RANGE, err.diag);
}

// A digit outside U+0030..U+0039 is a digit in another script, and saying so
// is more use than "expected a digit", which reads as though the field were
// missing.
TEST(Rfc3339, ANonAsciiDigitIsNamedAsOne) {
  GCHRON_OffsetDateTime odt;
  GCHRON_Error err;
  // 1963-06-1<BENGALI FOUR>T00:00:00Z
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      parse_dt("1963-06-1\xE0\xA7\xAAT00:00:00Z", &odt, nullptr, nullptr,
          &err));
  EXPECT_EQ(GCHRON_DIAG_NON_ASCII_DIGIT, err.diag);
  EXPECT_EQ(9u, err.offset);
}

TEST(Rfc3339, LeadingWhitespaceIsItsOwnDiagnostic) {
  GCHRON_OffsetDateTime odt;
  GCHRON_Error err;
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      parse_dt(" 1985-04-12T23:20:50Z", &odt, nullptr, nullptr, &err));
  EXPECT_EQ(GCHRON_DIAG_LEADING_WHITESPACE, err.diag);
  EXPECT_EQ(0u, err.offset);
}

TEST(Rfc3339, TrailingCharactersAreRefusedUnlessAskedFor) {
  GCHRON_OffsetDateTime odt;
  GCHRON_ParseInfo info;
  GCHRON_Error err;

  EXPECT_EQ(GCHRON_ERR_FORMAT,
      parse_dt("1985-04-12T23:20:50Ztail", &odt, nullptr, nullptr, &err));
  EXPECT_EQ(GCHRON_DIAG_TRAILING_CHARACTERS, err.diag);
  EXPECT_EQ(20u, err.offset);
  EXPECT_EQ(4u, err.length);

  // A trailing newline is trailing text like any other.
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse_dt("1985-04-12T23:20:50Z\n", &odt));

  GCHRON_ParseOptions opts;
  gchron_parse_options_default(&opts);
  opts.allow_trailing = true;
  ASSERT_EQ(GCHRON_OK,
      parse_dt("1985-04-12T23:20:50Ztail", &odt, &opts, &info));
  EXPECT_EQ(20u, info.consumed);
}

// The text is never assumed to be NUL-terminated: a parser that trusts a
// terminator reads past the end of the one document that has none.
TEST(Rfc3339, TheLengthBoundsTheParseRatherThanATerminator) {
  const char embedded[] = "1985-04-12T23:20:50Z1985-04-12T23:20:51Z";
  GCHRON_OffsetDateTime odt;
  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc3339_date_time(embedded, 20, nullptr, &odt, nullptr,
          nullptr));
  EXPECT_EQ(50, odt.civil.time.second);
}

TEST(Rfc3339, InputLongerThanTheLimitIsRefusedWithoutBeingParsed) {
  GCHRON_Limits limits;
  GCHRON_ParseOptions opts;
  GCHRON_OffsetDateTime odt;
  GCHRON_Error err;

  gchron_limits_default(&limits);
  limits.max_parse_length = 10;
  gchron_parse_options_default(&opts);
  opts.limits = &limits;

  EXPECT_EQ(GCHRON_ERR_LIMIT,
      parse_dt("1985-04-12T23:20:50Z", &odt, &opts, nullptr, &err));
  EXPECT_EQ(GCHRON_DIAG_INPUT_TOO_LONG, err.diag);

  // Zero means no limit.
  limits.max_parse_length = 0;
  EXPECT_EQ(GCHRON_OK, parse_dt("1985-04-12T23:20:50Z", &odt, &opts));
}

TEST(Rfc3339, FullDateAndFullTimeAreTheirOwnGrammars) {
  GCHRON_Date date;
  GCHRON_OffsetTime ot;
  const char * d = "1963-06-19";
  const char * t = "08:30:06-08:00";

  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc3339_full_date(d, std::strlen(d), nullptr, &date,
          nullptr, nullptr));
  EXPECT_EQ(1963, date.year);

  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc3339_full_time(t, std::strlen(t), nullptr, &ot, nullptr,
          nullptr));
  EXPECT_EQ(8, ot.time.hour);
  EXPECT_EQ(-8 * 3600, ot.offset_sec);

  // A full-date does not accept a time, and a full-time requires an offset.
  const char * dt = "2020-11-28T23:55:45Z";
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parse_rfc3339_full_date(dt, std::strlen(dt), nullptr, &date,
          nullptr, nullptr));
  const char * bare = "12:00:00";
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parse_rfc3339_full_time(bare, std::strlen(bare), nullptr, &ot,
          nullptr, nullptr));
}

TEST(Rfc3339, NullArgumentsAreRefusedRatherThanDereferenced) {
  GCHRON_OffsetDateTime odt;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_parse_rfc3339_date_time(nullptr, 0, nullptr, &odt, nullptr,
          nullptr));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_parse_rfc3339_date_time("x", 1, nullptr, nullptr, nullptr,
          nullptr));
  // The empty string is a format error, not a crash.
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parse_rfc3339_date_time("", 0, nullptr, &odt, nullptr, nullptr));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
