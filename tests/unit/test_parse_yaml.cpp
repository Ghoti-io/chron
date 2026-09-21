/**
 * @file
 *
 * The YAML 1.1 `!!timestamp` parser and writer, on the questions the PyYAML
 * differential cannot ask.
 *
 * `tests/conformance/test_yaml_timestamp.cpp` settles what the grammar
 * accepts, against the reference implementation. What it cannot settle is
 * anything PyYAML has no opinion about: which diagnostic a refusal carries,
 * what the non-default policies do, how the writer spells a value, and what
 * happens at the edges of a buffer. Those are this library's decisions, and
 * they are stated here as requirements rather than observed from the code.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

/** Parse with the grammar's own preset, returning the result code. */
GCHRON_Result parse(const std::string & text, GCHRON_YamlValue * out,
    GCHRON_ParseInfo * info = nullptr, GCHRON_Error * err = nullptr) {
  return gchron_parse_yaml_timestamp(text.data(), text.size(), nullptr, out,
      info, err);
}

/** Write with the grammar's own default options. */
std::string write(const GCHRON_YamlValue & value) {
  char buf[GCHRON_YAML_TIMESTAMP_MAX];
  size_t length = 0;
  EXPECT_EQ(gchron_write_yaml_timestamp(&value, nullptr, buf, sizeof(buf),
          &length), GCHRON_OK);
  EXPECT_EQ(std::strlen(buf), length);
  return std::string(buf, length);
}

/** Parse then write, which is what a document normaliser does. */
std::string normalise(const std::string & text) {
  GCHRON_YamlValue value;
  EXPECT_EQ(parse(text, &value), GCHRON_OK) << text;
  return write(value);
}

/*--------------------------------------------------------------------------*
 * Which shape came out
 *--------------------------------------------------------------------------*/

TEST(ParseYaml, ReportsTheShape) {
  GCHRON_YamlValue value;

  ASSERT_EQ(parse("2001-12-14", &value), GCHRON_OK);
  EXPECT_EQ(value.kind, GCHRON_YAML_DATE);

  ASSERT_EQ(parse("2001-12-14T21:59:43", &value), GCHRON_OK);
  EXPECT_EQ(value.kind, GCHRON_YAML_DATE_TIME);

  ASSERT_EQ(parse("2001-12-14T21:59:43Z", &value), GCHRON_OK);
  EXPECT_EQ(value.kind, GCHRON_YAML_OFFSET_DATE_TIME);

  ASSERT_EQ(parse("2001-12-14T21:59:43-05:00", &value), GCHRON_OK);
  EXPECT_EQ(value.kind, GCHRON_YAML_OFFSET_DATE_TIME);
}

/**
 * A zoneless reading is not UTC.
 *
 * The type repository's canonical form is UTC, and the temptation is to fold
 * a zoneless timestamp into it on the way in. That would assert a zone the
 * document did not write (design.md, mistake M1), and the two readings below
 * would become the same value when they are not.
 */
TEST(ParseYaml, ZonelessIsNotUtc) {
  GCHRON_YamlValue bare;
  GCHRON_YamlValue utc;

  ASSERT_EQ(parse("2001-12-14T21:59:43", &bare), GCHRON_OK);
  ASSERT_EQ(parse("2001-12-14T21:59:43Z", &utc), GCHRON_OK);

  EXPECT_NE(bare.kind, utc.kind);
  EXPECT_FALSE(gchron_yaml_timestamp_identical(&bare, &utc));
  EXPECT_EQ(write(bare), "2001-12-14T21:59:43");
  EXPECT_EQ(write(utc), "2001-12-14T21:59:43Z");
}

/*--------------------------------------------------------------------------*
 * Diagnostics
 *--------------------------------------------------------------------------*/

/** The diagnostic a refusal carries, for the refusals this grammar owns. */
TEST(ParseYaml, NamesWhatWasWrong) {
  struct Case {
    const char * text;
    GCHRON_Diag diag;
  };
  const Case cases[] = {
    { "2001-12-4", GCHRON_DIAG_YAML_DATE_WIDTH },
    { "2001-1-14", GCHRON_DIAG_YAML_DATE_WIDTH },
    { "2001-12-14T21:59:43z", GCHRON_DIAG_YAML_LOWERCASE_Z },
    { "2001-13-14", GCHRON_DIAG_MONTH_OUT_OF_RANGE },
    { "2001-02-30", GCHRON_DIAG_DAY_OUT_OF_RANGE },
    { "2001-12-14T24:00:00Z", GCHRON_DIAG_HOUR_OUT_OF_RANGE },
    { "2001-12-14T21:60:43Z", GCHRON_DIAG_MINUTE_OUT_OF_RANGE },
    { "2001-12-14T21:59:61Z", GCHRON_DIAG_SECOND_OUT_OF_RANGE },
    { "2001-12-14T21:59:43+24:00", GCHRON_DIAG_OFFSET_OUT_OF_RANGE },
    { "2001-12-14T21:59:43+05:60", GCHRON_DIAG_OFFSET_OUT_OF_RANGE },
    { "2001-12-14T21:59:43Ztail", GCHRON_DIAG_TRAILING_CHARACTERS },
    { " 2001-12-14", GCHRON_DIAG_LEADING_WHITESPACE },
    { "2001-12-14T21:5:43Z", GCHRON_DIAG_EXPECTED_DIGIT },
  };

  for (const Case & c : cases) {
    GCHRON_YamlValue value;
    GCHRON_Error err;
    EXPECT_NE(parse(c.text, &value, nullptr, &err), GCHRON_OK) << c.text;
    EXPECT_EQ(err.diag, c.diag)
        << c.text << " said \"" << gchron_diag_string(err.diag) << "\"";
  }
}

/**
 * A `~` in the offset is refused as a digit, not as a missing field.
 *
 * The Bengali digit the JSON-Schema-Test-Suite carries reaches this grammar
 * too, and "expected a digit" reads as though the field were absent.
 */
TEST(ParseYaml, NonAsciiDigitSaysSo) {
  GCHRON_YamlValue value;
  GCHRON_Error err;
  const std::string text = "২001-12-14";
  EXPECT_NE(parse(text, &value, nullptr, &err), GCHRON_OK);
  EXPECT_EQ(err.diag, GCHRON_DIAG_NON_ASCII_DIGIT);
}

/*--------------------------------------------------------------------------*
 * Policies
 *--------------------------------------------------------------------------*/

/** The preset is truncate-and-clamp, and each can be overridden. */
TEST(ParseYaml, PresetIsTruncateAndClamp) {
  GCHRON_ParseOptions opts;
  gchron_parse_options_yaml(&opts);
  EXPECT_EQ(opts.fraction, GCHRON_FRACTION_TRUNCATE);
  EXPECT_EQ(opts.leap, GCHRON_LEAP_CLAMP);
  EXPECT_FALSE(opts.allow_trailing);
  EXPECT_EQ(opts.limits, nullptr);
  EXPECT_EQ(opts.leap_table, nullptr);
}

TEST(ParseYaml, LeapSecondFollowsThePolicy) {
  const std::string text = "1998-12-31T23:59:60Z";
  GCHRON_YamlValue value;
  GCHRON_ParseInfo info;
  GCHRON_Error err;

  /* The preset accepts it, holding :59 with the flag as the evidence. */
  ASSERT_EQ(parse(text, &value, &info), GCHRON_OK);
  EXPECT_EQ(static_cast<int>(value.civil.time.second), 59);
  EXPECT_TRUE(info.leap_second);

  /* REJECT refuses it. */
  GCHRON_ParseOptions opts;
  gchron_parse_options_yaml(&opts);
  opts.leap = GCHRON_LEAP_REJECT;
  EXPECT_EQ(gchron_parse_yaml_timestamp(text.data(), text.size(), &opts,
          &value, nullptr, &err), GCHRON_ERR_FORMAT);
  EXPECT_EQ(err.diag, GCHRON_DIAG_LEAP_SECOND_REJECTED);

  /* MINUTE accepts the real one and refuses one in the wrong minute. */
  opts.leap = GCHRON_LEAP_MINUTE;
  EXPECT_EQ(gchron_parse_yaml_timestamp(text.data(), text.size(), &opts,
          &value, nullptr, nullptr), GCHRON_OK);
  const std::string wrong = "1998-12-31T22:59:60Z";
  EXPECT_EQ(gchron_parse_yaml_timestamp(wrong.data(), wrong.size(), &opts,
          &value, nullptr, &err), GCHRON_ERR_FORMAT);
  EXPECT_EQ(err.diag, GCHRON_DIAG_LEAP_SECOND_WRONG_MINUTE);
}

/**
 * A `:60` with no zone cannot satisfy GCHRON_LEAP_MINUTE.
 *
 * YAML is the one grammar here whose timestamp may carry no offset, so it is
 * the one where the question "is this 23:59 in UTC?" can have no answer. The
 * requirement is that it refuse rather than assume the document meant UTC.
 */
TEST(ParseYaml, LeapMinuteNeedsAnOffset) {
  GCHRON_ParseOptions opts;
  gchron_parse_options_yaml(&opts);
  opts.leap = GCHRON_LEAP_MINUTE;

  const std::string text = "1998-12-31T23:59:60";
  GCHRON_YamlValue value;
  GCHRON_Error err;
  EXPECT_EQ(gchron_parse_yaml_timestamp(text.data(), text.size(), &opts,
          &value, nullptr, &err), GCHRON_ERR_FORMAT);
  EXPECT_EQ(err.diag, GCHRON_DIAG_LEAP_SECOND_WRONG_MINUTE);
}

TEST(ParseYaml, FractionFollowsThePolicy) {
  const std::string text = "2001-12-14T21:59:43.1234567891Z";
  GCHRON_YamlValue value;
  GCHRON_ParseInfo info;
  GCHRON_Error err;

  ASSERT_EQ(parse(text, &value, &info), GCHRON_OK);
  EXPECT_EQ(value.civil.time.nsec, 123456789);
  EXPECT_TRUE(info.fraction_truncated);
  EXPECT_EQ(static_cast<int>(info.fraction_digits), 10);

  GCHRON_ParseOptions opts;
  gchron_parse_options_yaml(&opts);
  opts.fraction = GCHRON_FRACTION_REJECT;
  EXPECT_EQ(gchron_parse_yaml_timestamp(text.data(), text.size(), &opts,
          &value, nullptr, &err), GCHRON_ERR_FORMAT);
  EXPECT_EQ(err.diag, GCHRON_DIAG_FRACTION_TOO_LONG);
}

/** An empty fraction is a fraction of zero, not a missing one. */
TEST(ParseYaml, EmptyFractionIsZero) {
  GCHRON_YamlValue value;
  GCHRON_ParseInfo info;
  ASSERT_EQ(parse("2001-12-14T21:59:43.Z", &value, &info), GCHRON_OK);
  EXPECT_EQ(value.civil.time.nsec, 0);
  EXPECT_EQ(static_cast<int>(info.fraction_digits), 0);
  EXPECT_FALSE(info.fraction_truncated);
}

/*--------------------------------------------------------------------------*
 * The offset's spelling
 *--------------------------------------------------------------------------*/

/** `-00:00` is not `+00:00`, and survives a round trip as itself. */
TEST(ParseYaml, UnknownOffsetSurvives) {
  GCHRON_YamlValue unknown;
  GCHRON_YamlValue zero;
  ASSERT_EQ(parse("2001-12-14T21:59:43-00:00", &unknown), GCHRON_OK);
  ASSERT_EQ(parse("2001-12-14T21:59:43+00:00", &zero), GCHRON_OK);

  EXPECT_TRUE(unknown.offset_unknown);
  EXPECT_FALSE(zero.offset_unknown);
  EXPECT_FALSE(gchron_yaml_timestamp_identical(&unknown, &zero));
  EXPECT_EQ(write(unknown), "2001-12-14T21:59:43-00:00");
  EXPECT_EQ(write(zero), "2001-12-14T21:59:43+00:00");
}

/** `Z` and `+00:00` are the same instant and different statements. */
TEST(ParseYaml, ZeroOffsetKeepsItsSpelling) {
  GCHRON_YamlValue z;
  GCHRON_YamlValue numeric;
  ASSERT_EQ(parse("2001-12-14T21:59:43Z", &z), GCHRON_OK);
  ASSERT_EQ(parse("2001-12-14T21:59:43+00:00", &numeric), GCHRON_OK);

  EXPECT_EQ(z.offset_sec, numeric.offset_sec);
  EXPECT_TRUE(z.offset_is_z);
  EXPECT_FALSE(numeric.offset_is_z);
  EXPECT_FALSE(gchron_yaml_timestamp_identical(&z, &numeric));
  EXPECT_EQ(write(z), "2001-12-14T21:59:43Z");
  EXPECT_EQ(write(numeric), "2001-12-14T21:59:43+00:00");

  /* Options of the caller's own override the remembered spelling. */
  GCHRON_WriteOptions opts;
  gchron_write_options_default(&opts);
  char buf[GCHRON_YAML_TIMESTAMP_MAX];
  size_t length = 0;
  ASSERT_EQ(gchron_write_yaml_timestamp(&numeric, &opts, buf, sizeof(buf),
          &length), GCHRON_OK);
  EXPECT_EQ(std::string(buf, length), "2001-12-14T21:59:43Z");
}

/*--------------------------------------------------------------------------*
 * Normalisation
 *--------------------------------------------------------------------------*/

/**
 * The writer emits the canonical spelling, whatever came in.
 *
 * Every left-hand side below is a conformant YAML 1.1 timestamp; none of the
 * relaxed spellings survives, because a reader following the published
 * expression to the letter is entitled to read some of them as strings.
 */
TEST(ParseYaml, NormalisesToTheCanonicalSpelling) {
  EXPECT_EQ(normalise("2001-12-4T21:59:43Z"), "2001-12-04T21:59:43Z");
  EXPECT_EQ(normalise("2001-1-4T2:59:43Z"), "2001-01-04T02:59:43Z");
  EXPECT_EQ(normalise("2001-12-14   21:59:43Z"), "2001-12-14T21:59:43Z");
  EXPECT_EQ(normalise("2001-12-14\t21:59:43Z"), "2001-12-14T21:59:43Z");
  EXPECT_EQ(normalise("2001-12-14t21:59:43Z"), "2001-12-14T21:59:43Z");
  EXPECT_EQ(normalise("2001-12-14T21:59:43 Z"), "2001-12-14T21:59:43Z");
  EXPECT_EQ(normalise("2001-12-14T21:59:43 -05:00"),
      "2001-12-14T21:59:43-05:00");
  EXPECT_EQ(normalise("2001-12-14T21:59:43-5"), "2001-12-14T21:59:43-05:00");
  EXPECT_EQ(normalise("2001-12-14T21:59:43+05"), "2001-12-14T21:59:43+05:00");
  EXPECT_EQ(normalise("2001-12-14T21:59:43."), "2001-12-14T21:59:43");
}

/** The fraction is the shortest that loses nothing, at any width. */
TEST(ParseYaml, WritesTheShortestFraction) {
  EXPECT_EQ(normalise("2001-12-14T21:59:43.10Z"), "2001-12-14T21:59:43.1Z");
  EXPECT_EQ(normalise("2001-12-14T21:59:43.100000000Z"),
      "2001-12-14T21:59:43.1Z");
  EXPECT_EQ(normalise("2001-12-14T21:59:43.5Z"), "2001-12-14T21:59:43.5Z");
  EXPECT_EQ(normalise("2001-12-14T21:59:43.123456789Z"),
      "2001-12-14T21:59:43.123456789Z");
  EXPECT_EQ(normalise("2001-12-14T21:59:43.000000001Z"),
      "2001-12-14T21:59:43.000000001Z");
  EXPECT_EQ(normalise("2001-12-14T21:59:43.000Z"), "2001-12-14T21:59:43Z");

  /* GCHRON_FRACTION_DIGITS_AUTO is the other rule, and still available. */
  GCHRON_YamlValue value;
  ASSERT_EQ(parse("2001-12-14T21:59:43.1Z", &value), GCHRON_OK);
  GCHRON_WriteOptions opts;
  gchron_write_options_default(&opts);
  opts.fraction_digits = GCHRON_FRACTION_DIGITS_AUTO;
  char buf[GCHRON_YAML_TIMESTAMP_MAX];
  size_t length = 0;
  ASSERT_EQ(gchron_write_yaml_timestamp(&value, &opts, buf, sizeof(buf),
          &length), GCHRON_OK);
  EXPECT_EQ(std::string(buf, length), "2001-12-14T21:59:43.100Z");
}

/*--------------------------------------------------------------------------*
 * The output contract
 *--------------------------------------------------------------------------*/

/** A zero-length buffer asks for the length, and does not write. */
TEST(ParseYaml, ZeroBufferReportsTheLength) {
  GCHRON_YamlValue value;
  ASSERT_EQ(parse("2001-12-14T21:59:43Z", &value), GCHRON_OK);

  size_t length = 0;
  EXPECT_EQ(gchron_write_yaml_timestamp(&value, nullptr, nullptr, 0, &length),
      GCHRON_ERR_LIMIT);
  EXPECT_EQ(length, std::strlen("2001-12-14T21:59:43Z"));

  /* And `length + 1` is exactly enough, which is what the contract promises. */
  std::vector<char> buf(length + 1);
  size_t again = 0;
  EXPECT_EQ(gchron_write_yaml_timestamp(&value, nullptr, buf.data(),
          buf.size(), &again), GCHRON_OK);
  EXPECT_EQ(again, length);
  EXPECT_EQ(std::string(buf.data()), "2001-12-14T21:59:43Z");

  /* One byte short is a refusal, with the length still reported. */
  size_t short_len = 0;
  EXPECT_EQ(gchron_write_yaml_timestamp(&value, nullptr, buf.data(),
          buf.size() - 1, &short_len), GCHRON_ERR_LIMIT);
  EXPECT_EQ(short_len, length);
}

/** GCHRON_YAML_TIMESTAMP_MAX holds every value this grammar can produce. */
TEST(ParseYaml, TheMaximumIsEnough) {
  const char * widest[] = {
    "2001-12-14T21:59:43.123456789Z",
    "2001-12-14T21:59:43.123456789-05:00",
    "2001-12-14T21:59:43.123456789-00:00",
    "2001-12-14T21:59:43.123456789",
  };
  for (const char * text : widest) {
    GCHRON_YamlValue value;
    ASSERT_EQ(parse(text, &value), GCHRON_OK) << text;
    char buf[GCHRON_YAML_TIMESTAMP_MAX];
    size_t length = 0;
    EXPECT_EQ(gchron_write_yaml_timestamp(&value, nullptr, buf, sizeof(buf),
            &length), GCHRON_OK) << text;
    EXPECT_LT(length, GCHRON_YAML_TIMESTAMP_MAX) << text;
  }
}

/*--------------------------------------------------------------------------*
 * Edges
 *--------------------------------------------------------------------------*/

/** NULL in, a refusal out; never a crash and never a partial write. */
TEST(ParseYaml, RefusesNull) {
  GCHRON_YamlValue value;
  EXPECT_EQ(gchron_parse_yaml_timestamp(nullptr, 0, nullptr, &value, nullptr,
          nullptr), GCHRON_ERR_INVALID);
  const std::string text = "2001-12-14";
  EXPECT_EQ(gchron_parse_yaml_timestamp(text.data(), text.size(), nullptr,
          nullptr, nullptr, nullptr), GCHRON_ERR_INVALID);

  char buf[GCHRON_YAML_TIMESTAMP_MAX];
  EXPECT_EQ(gchron_write_yaml_timestamp(nullptr, nullptr, buf, sizeof(buf),
          nullptr), GCHRON_ERR_INVALID);

  /* A zero-initialised value names no shape, and is refused rather than
     written as some default. */
  GCHRON_YamlValue unset;
  std::memset(&unset, 0, sizeof(unset));
  EXPECT_EQ(unset.kind, GCHRON_YAML_NONE);
  EXPECT_EQ(gchron_write_yaml_timestamp(&unset, nullptr, buf, sizeof(buf),
          nullptr), GCHRON_ERR_INVALID);

  EXPECT_FALSE(gchron_yaml_timestamp_identical(&value, nullptr));
  EXPECT_FALSE(gchron_yaml_timestamp_identical(nullptr, &value));
  EXPECT_TRUE(gchron_yaml_timestamp_identical(nullptr, nullptr));
}

/** The text is never assumed to be NUL-terminated. */
TEST(ParseYaml, ReadsOnlyTheLengthGiven) {
  const char raw[] = "2001-12-14T21:59:43Zand then some other document";
  GCHRON_YamlValue value;
  EXPECT_EQ(gchron_parse_yaml_timestamp(raw, 20, nullptr, &value, nullptr,
          nullptr), GCHRON_OK);
  EXPECT_EQ(value.kind, GCHRON_YAML_OFFSET_DATE_TIME);

  /* And the whole buffer is trailing text, not a longer timestamp. */
  EXPECT_NE(gchron_parse_yaml_timestamp(raw, std::strlen(raw), nullptr, &value,
          nullptr, nullptr), GCHRON_OK);
}

/**
 * Under `allow_trailing`, a space after a date is trailing text.
 *
 * The separator run commits to a time only when a digit follows it, so a
 * caller reading a timestamp out of a larger document gets the date and the
 * position it stopped at, rather than a failed hour.
 */
TEST(ParseYaml, AllowTrailingStopsAtTheDate) {
  GCHRON_ParseOptions opts;
  gchron_parse_options_yaml(&opts);
  opts.allow_trailing = true;

  const std::string text = "2001-12-14 and then some prose";
  GCHRON_YamlValue value;
  GCHRON_ParseInfo info;
  ASSERT_EQ(gchron_parse_yaml_timestamp(text.data(), text.size(), &opts,
          &value, &info, nullptr), GCHRON_OK);
  EXPECT_EQ(value.kind, GCHRON_YAML_DATE);
  EXPECT_EQ(info.consumed, std::strlen("2001-12-14"));

  const std::string with_time = "2001-12-14 21:59:43Z rest";
  ASSERT_EQ(gchron_parse_yaml_timestamp(with_time.data(), with_time.size(),
          &opts, &value, &info, nullptr), GCHRON_OK);
  EXPECT_EQ(value.kind, GCHRON_YAML_OFFSET_DATE_TIME);
  EXPECT_EQ(info.consumed, std::strlen("2001-12-14 21:59:43Z"));
}

/** The input cap applies here as to every other grammar. */
TEST(ParseYaml, HonoursTheParseLimit) {
  GCHRON_Limits limits;
  gchron_limits_default(&limits);
  limits.max_parse_length = 5;

  GCHRON_ParseOptions opts;
  gchron_parse_options_yaml(&opts);
  opts.limits = &limits;

  const std::string text = "2001-12-14";
  GCHRON_YamlValue value;
  GCHRON_Error err;
  EXPECT_EQ(gchron_parse_yaml_timestamp(text.data(), text.size(), &opts,
          &value, nullptr, &err), GCHRON_ERR_LIMIT);
  EXPECT_EQ(err.diag, GCHRON_DIAG_INPUT_TOO_LONG);
}

/** A year this grammar can read but RFC 3339's writer cannot is a range
    error, not a truncated year. */
TEST(ParseYaml, YearsOutsideFourDigitsAreRefusedByTheWriter) {
  GCHRON_YamlValue value;
  ASSERT_EQ(parse("2001-12-14", &value), GCHRON_OK);
  value.civil.date.year = 12345;

  char buf[GCHRON_YAML_TIMESTAMP_MAX];
  EXPECT_EQ(gchron_write_yaml_timestamp(&value, nullptr, buf, sizeof(buf),
          nullptr), GCHRON_ERR_RANGE);
}

}  // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
