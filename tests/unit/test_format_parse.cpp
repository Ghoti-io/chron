/**
 * @file
 *
 * Reading text back through a compiled pattern: design.md section 8.7.
 *
 * The centre of this file is one property - `parse(format(x))` gives back
 * `x` - swept across every pattern letter that inverts and a spread of
 * values. Everything else here is a decision the design document made, tested
 * so that changing it has to be deliberate.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/chron.h>
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace {

/** Compile, or fail the test with the pattern named. */
GCHRON_Format * compiled(const std::string & pattern,
    GCHRON_FormatSyntax syntax = GCHRON_FORMAT_LDML) {
  GCHRON_Format * format = nullptr;
  GCHRON_Error err{};
  GCHRON_Result result = gchron_format_compile(pattern.c_str(),
      pattern.size(), syntax, nullptr, nullptr, &format, &err);
  EXPECT_EQ(GCHRON_OK, result) << pattern << ": " << gchron_diag_string(err.diag);
  return result == GCHRON_OK ? format : nullptr;
}

struct Trip {
  GCHRON_Result format_result;
  GCHRON_Result parse_result;
  std::string text;
  GCHRON_ParsedFields fields;
  GCHRON_Error err;
};

/** Format a value, then read the text back. */
Trip round_trip(GCHRON_Format * format, const GCHRON_DateTime & dt,
    const GCHRON_PatternContext * context = nullptr) {
  Trip trip{};
  char buffer[256];
  size_t length = 0;
  trip.format_result = gchron_format_datetime(format, &dt, nullptr, buffer,
      sizeof(buffer), &length);
  if (trip.format_result != GCHRON_OK) {
    return trip;
  }
  trip.text.assign(buffer, length);
  trip.parse_result = gchron_format_parse(format, buffer, length, context,
      &trip.fields, &trip.err);
  return trip;
}

/*--------------------------------------------------------------------------*
 * The property
 *--------------------------------------------------------------------------*/

TEST(FormatParse, EveryInvertiblePatternReadsBackWhatItWrote) {
  /*
   * The whole claim of section 8.7, as a sweep rather than as examples: for
   * every pattern below and every value below, formatting and then parsing
   * gives back a date-time equal to the one that went in.
   *
   * The patterns are chosen so that each names a whole date and time by
   * itself - a partial pattern cannot round-trip through
   * gchron_parsed_to_datetime() and is tested separately - and between them
   * they use every letter that section 8.7 says inverts.
   */
  static const struct {
    const char * pattern;
    GCHRON_FormatSyntax syntax;
  } kPatterns[] = {
    { "yyyy-MM-dd'T'HH:mm:ss",          GCHRON_FORMAT_LDML },
    { "uuu-MM-dd HH:mm:ss",             GCHRON_FORMAT_LDML },
    { "uuuuu-MM-dd HH:mm:ss",           GCHRON_FORMAT_LDML },
    { "y-M-d H:m:s",                    GCHRON_FORMAT_LDML },
    { "yyyyMMddHHmmss",                 GCHRON_FORMAT_LDML },
    { "yyyy-MM-dd HH:mm:ss.SSS",        GCHRON_FORMAT_LDML },
    { "yyyy-MM-dd HH:mm:ss.SSSSSSSSS",  GCHRON_FORMAT_LDML },
    { "dd MMM yyyy HH:mm:ss",           GCHRON_FORMAT_LDML },
    { "dd MMMM yyyy HH:mm:ss",          GCHRON_FORMAT_LDML },
    { "EEE, dd MMM yyyy HH:mm:ss",      GCHRON_FORMAT_LDML },
    { "EEEE d MMMM yyyy HH:mm:ss",      GCHRON_FORMAT_LDML },
    { "yyyy-DDD HH:mm:ss",              GCHRON_FORMAT_LDML },
    { "yyyy-MM-dd hh:mm:ss a",          GCHRON_FORMAT_LDML },
    { "yyyy-MM-dd KK:mm:ss a",          GCHRON_FORMAT_LDML },
    { "yyyy-MM-dd kk:mm:ss",            GCHRON_FORMAT_LDML },
    { "uuuu-MM-dd HH:mm:ss",            GCHRON_FORMAT_LDML },
    { "QQQ yyyy-MM-dd HH:mm:ss",        GCHRON_FORMAT_LDML },
    { "Q yyyy-MM-dd HH:mm:ss",          GCHRON_FORMAT_LDML },
    { "g HH:mm:ss",                     GCHRON_FORMAT_LDML },
    { "%Y-%m-%d %H:%M:%S",              GCHRON_FORMAT_STRFTIME },
    { "%d/%b/%Y:%H:%M:%S",              GCHRON_FORMAT_STRFTIME },
    { "%Y-%m-%d %I:%M:%S %p",           GCHRON_FORMAT_STRFTIME },
    { "%Y-%j %H:%M:%S",                 GCHRON_FORMAT_STRFTIME },
    { "%a %b %d %H:%M:%S %Y",           GCHRON_FORMAT_STRFTIME },
    { "%A %B %d %H:%M:%S %Y",           GCHRON_FORMAT_STRFTIME },
  };

  static const GCHRON_DateTime kValues[] = {
    gchrontest::datetime(2026, 9, 21, 15, 30, 45, 123456789),
    gchrontest::datetime(2026, 1, 1, 0, 0, 0, 0),
    gchrontest::datetime(2026, 12, 31, 23, 59, 59, 999999999),
    gchrontest::datetime(2024, 2, 29, 12, 0, 0, 0),   // a leap day
    gchrontest::datetime(2000, 2, 29, 11, 59, 59, 0), // the century leap day
    gchrontest::datetime(1970, 1, 1, 0, 0, 0, 0),     // the epoch
    gchrontest::datetime(1900, 3, 1, 13, 14, 15, 0),
    gchrontest::datetime(2026, 7, 4, 12, 0, 0, 0),    // noon, the `hh` trap
    gchrontest::datetime(2026, 7, 4, 0, 30, 0, 0),    // half past midnight
  };

  size_t checked = 0;
  for (const auto & row : kPatterns) {
    GCHRON_Format * format = compiled(row.pattern, row.syntax);
    ASSERT_NE(nullptr, format) << row.pattern;
    for (const GCHRON_DateTime & value : kValues) {
      Trip trip = round_trip(format, value);
      ASSERT_EQ(GCHRON_OK, trip.format_result) << row.pattern;
      ASSERT_EQ(GCHRON_OK, trip.parse_result)
          << row.pattern << " wrote \"" << trip.text
          << "\" and could not read it: "
          << gchron_diag_string(trip.err.diag);

      GCHRON_DateTime back{};
      GCHRON_Error err{};
      ASSERT_EQ(GCHRON_OK,
          gchron_parsed_to_datetime(&trip.fields, nullptr, &back, &err))
          << row.pattern << " \"" << trip.text << "\": "
          << gchron_diag_string(err.diag);

      EXPECT_EQ(value.date.year, back.date.year) << row.pattern << " " << trip.text;
      EXPECT_EQ(value.date.month, back.date.month) << row.pattern << " " << trip.text;
      EXPECT_EQ(value.date.day, back.date.day) << row.pattern << " " << trip.text;
      EXPECT_EQ(value.time.hour, back.time.hour) << row.pattern << " " << trip.text;
      EXPECT_EQ(value.time.minute, back.time.minute) << row.pattern << " " << trip.text;
      EXPECT_EQ(value.time.second, back.time.second) << row.pattern << " " << trip.text;
      ++checked;
    }
    gchron_format_destroy(format);
  }
  EXPECT_EQ(sizeof(kPatterns) / sizeof(kPatterns[0])
      * sizeof(kValues) / sizeof(kValues[0]), checked);
}

TEST(FormatParse, TheFractionSurvivesAtEveryWidthItWasWrittenAt) {
  // `SSS` writes three digits of a nanosecond field and must read back as
  // those three digits' worth - 123000000, not 123.
  static const struct {
    const char * pattern;
    int32_t nsec;
    int32_t expected;
  } kRows[] = {
    { "HH:mm:ss.S",         123456789, 100000000 },
    { "HH:mm:ss.SSS",       123456789, 123000000 },
    { "HH:mm:ss.SSSSSS",    123456789, 123456000 },
    { "HH:mm:ss.SSSSSSSSS", 123456789, 123456789 },
    { "HH:mm:ss.SSS",         1000000,   1000000 },
    { "HH:mm:ss.SSSSSSSSS",         1,         1 },
    /*
     * TR35 allows a count past the nine digits a nanosecond field holds, and
     * the emitter writes zeros there. Accumulating those extra digits into
     * the same integer overflowed it - `fuzz_scan` found `SSSSSSSSSS`
     * producing a ten-digit nanosecond count - so they are consumed and
     * dropped, cut rather than rounded, as section 8.2 says everywhere else.
     */
    { "HH:mm:ss.SSSSSSSSSS",  123456789, 123456789 },
    { "HH:mm:ss.SSSSSSSSSSSSSS", 123456789, 123456789 },
  };
  for (const auto & row : kRows) {
    GCHRON_Format * format = compiled(row.pattern);
    ASSERT_NE(nullptr, format) << row.pattern;
    Trip trip = round_trip(format,
        gchrontest::datetime(2026, 9, 21, 15, 30, 45, row.nsec));
    ASSERT_EQ(GCHRON_OK, trip.parse_result) << row.pattern;
    EXPECT_EQ(row.expected, trip.fields.nsec)
        << row.pattern << " wrote \"" << trip.text << "\"";
    gchron_format_destroy(format);
  }
}

/*--------------------------------------------------------------------------*
 * Offsets
 *--------------------------------------------------------------------------*/

TEST(FormatParse, EveryOffsetSpellingReadsBackWhatItWrote) {
  // Whole-minute offsets, which every spelling can carry.
  static const char * const kPatterns[] = {
    "X", "XX", "XXX", "XXXX", "XXXXX",
    "x", "xx", "xxx", "xxxx", "xxxxx",
    "Z", "ZZ", "ZZZ", "ZZZZZ",
  };
  static const int32_t kOffsets[] = {
    0, 3600, -3600, 19800, -19800, 50400, -39600, 60, -60,
  };

  size_t checked = 0;
  for (const char * pattern : kPatterns) {
    GCHRON_Format * format = compiled(pattern);
    ASSERT_NE(nullptr, format) << pattern;
    for (int32_t offset : kOffsets) {
      GCHRON_OffsetDateTime odt{};
      odt.civil = gchrontest::datetime(2026, 9, 21, 15, 30, 45, 0);
      odt.offset_sec = offset;

      char buffer[64];
      size_t length = 0;
      ASSERT_EQ(GCHRON_OK, gchron_format_offset(format, &odt, nullptr,
          buffer, sizeof(buffer), &length)) << pattern;

      GCHRON_ParsedFields fields{};
      GCHRON_Error err{};
      ASSERT_EQ(GCHRON_OK,
          gchron_format_parse(format, buffer, length, nullptr, &fields, &err))
          << pattern << " wrote \"" << std::string(buffer, length)
          << "\": " << gchron_diag_string(err.diag);
      EXPECT_EQ(offset, fields.offset_sec)
          << pattern << " wrote \"" << std::string(buffer, length) << "\"";
      ++checked;
    }
    gchron_format_destroy(format);
  }
  EXPECT_EQ(sizeof(kPatterns) / sizeof(kPatterns[0])
      * sizeof(kOffsets) / sizeof(kOffsets[0]), checked);
}

TEST(FormatParse, ASubMinuteOffsetSurvivesOnlyTheSpellingsThatWriteSeconds) {
  /*
   * Europe/Amsterdam kept +00:19:32 until 1937 and Europe/Paris +00:09:21
   * until 1911, so offsets that are not a whole number of minutes are real
   * history rather than a contrived input.
   *
   * TR35 gives `X`, `XX` and `XXX` hours and minutes and nothing else, so
   * those spellings *cannot* carry the 32 seconds and the round trip is not
   * an identity through them - the loss happens on the way out, in an
   * emitter that is doing exactly what the standard says. Asserting the
   * truncation rather than skipping it is what keeps this a test: a parser
   * that invented the seconds back would fail here, and so would an emitter
   * that quietly started writing them.
   */
  static const struct {
    const char * pattern;
    const char * written;
    int32_t reads_back;
  } kRows[] = {
    { "X",     "+0019",    1140 },  // truncated to the minute
    { "XX",    "+0019",    1140 },
    { "XXX",   "+00:19",   1140 },
    { "XXXX",  "+001932",  1172 },  // and these carry it
    { "XXXXX", "+00:19:32", 1172 },
    { "x",     "+0019",    1140 },
    { "xxxx",  "+001932",  1172 },
    { "xxxxx", "+00:19:32", 1172 },
    { "Z",     "+001932",  1172 },  // `Z`..`ZZZ` append optional seconds
    { "ZZZZZ", "+00:19:32", 1172 },
  };

  for (const auto & row : kRows) {
    GCHRON_Format * format = compiled(row.pattern);
    ASSERT_NE(nullptr, format) << row.pattern;

    GCHRON_OffsetDateTime odt{};
    odt.civil = gchrontest::datetime(1930, 6, 1, 12, 0, 0, 0);
    odt.offset_sec = 1172;  // +00:19:32, Amsterdam time

    char buffer[64];
    size_t length = 0;
    ASSERT_EQ(GCHRON_OK, gchron_format_offset(format, &odt, nullptr, buffer,
        sizeof(buffer), &length)) << row.pattern;
    EXPECT_EQ(row.written, std::string(buffer, length)) << row.pattern;

    GCHRON_ParsedFields fields{};
    GCHRON_Error err{};
    ASSERT_EQ(GCHRON_OK,
        gchron_format_parse(format, buffer, length, nullptr, &fields, &err))
        << row.pattern << " wrote \"" << std::string(buffer, length)
        << "\": " << gchron_diag_string(err.diag);
    EXPECT_EQ(row.reads_back, fields.offset_sec) << row.pattern;
    gchron_format_destroy(format);
  }
}

TEST(FormatParse, MinusZeroZeroIsRememberedAsAnUnknownOffset) {
  // RFC 3339 section 4.3 gives `-00:00` a meaning `Z` and `+00:00` do not
  // have, and mistake M14 is destroying that evidence in passing.
  GCHRON_Format * format = compiled("XXX");
  ASSERT_NE(nullptr, format);

  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};
  ASSERT_EQ(GCHRON_OK,
      gchron_format_parse(format, "-00:00", 6, nullptr, &fields, &err));
  EXPECT_EQ(0, fields.offset_sec);
  EXPECT_TRUE(fields.offset_unknown);

  ASSERT_EQ(GCHRON_OK,
      gchron_format_parse(format, "+00:00", 6, nullptr, &fields, &err));
  EXPECT_FALSE(fields.offset_unknown);

  ASSERT_EQ(GCHRON_OK, gchron_format_parse(format, "Z", 1, nullptr, &fields, &err));
  EXPECT_FALSE(fields.offset_unknown);
  gchron_format_destroy(format);
}

/*--------------------------------------------------------------------------*
 * What the document says it refuses
 *--------------------------------------------------------------------------*/

TEST(FormatParse, AZoneAbbreviationCannotBeReadBack) {
  // `EST` is US Eastern, Australian Eastern and several others; choosing
  // between them needs CLDR, which section 14 declines to ship. The refusal
  // arrives when the pattern is checked, not on the first line of a log.
  for (const char * pattern : { "z", "zzzz", "O", "OOOO", "HH:mm z" }) {
    GCHRON_Format * format = compiled(pattern);
    ASSERT_NE(nullptr, format) << pattern;

    GCHRON_Error err{};
    EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
        gchron_format_is_invertible(format, &err)) << pattern;
    EXPECT_EQ(GCHRON_DIAG_PATTERN_NOT_INVERTIBLE, err.diag) << pattern;
    // Into the pattern, so a template author is told where to look. This
    // reported the item index until `fuzz_scan` noticed the number was not
    // an offset into anything.
    EXPECT_LT(err.offset, strlen(pattern)) << pattern;
    EXPECT_EQ(std::string(pattern).find_first_of("zO"), err.offset) << pattern;

    GCHRON_ParsedFields fields{};
    EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
        gchron_format_parse(format, "EST", 3, nullptr, &fields, &err))
        << pattern;
    gchron_format_destroy(format);
  }
}

TEST(FormatParse, TheZoneIdentifierDoesReadBack) {
  // `VV` is exact where `z` is not, so it is supported and says so.
  GCHRON_Format * format = compiled("VV");
  ASSERT_NE(nullptr, format);
  EXPECT_EQ(GCHRON_OK, gchron_format_is_invertible(format, nullptr));

  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};
  ASSERT_EQ(GCHRON_OK, gchron_format_parse(format, "America/Argentina/Salta",
      23, nullptr, &fields, &err));
  EXPECT_STREQ("America/Argentina/Salta", fields.zone_id);
  EXPECT_TRUE((fields.present & GCHRON_FIELD_ZONE_ID) != 0);
  gchron_format_destroy(format);
}

TEST(FormatParse, ATwoDigitYearWithoutAClockIsRefused) {
  /*
   * `yy` reads `26` and cannot say whether that is 2026 or 1926 without
   * knowing what year it is. Section 3.8 refuses the static sliding window
   * ICU and Java keep, so the clock is asked for - and a caller who supplies
   * none gets a refusal naming the reason rather than a plausible wrong year.
   */
  GCHRON_Format * format = compiled("yy-MM-dd");
  ASSERT_NE(nullptr, format);

  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_format_parse(format, "26-09-21", 8, nullptr, &fields, &err));
  EXPECT_EQ(GCHRON_DIAG_PATTERN_NEEDS_CLOCK, err.diag);

  // With one, the window puts it in the nearest century.
  GCHRON_FixedClock fixed{};
  GCHRON_Instant when{ 1789000000, 0 };  // 2026-09-08
  ASSERT_EQ(GCHRON_OK, gchron_clock_fixed(when, &fixed));
  GCHRON_PatternContext context{};
  context.clock = (const GCHRON_Clock *)&fixed;

  ASSERT_EQ(GCHRON_OK,
      gchron_format_parse(format, "26-09-21", 8, &context, &fields, &err))
      << gchron_diag_string(err.diag);
  EXPECT_EQ(2026, fields.year);

  ASSERT_EQ(GCHRON_OK,
      gchron_format_parse(format, "99-09-21", 8, &context, &fields, &err));
  EXPECT_EQ(1999, fields.year) << "the window did not reach back a century";
  gchron_format_destroy(format);
}

TEST(FormatParse, TextLeftOverIsAFailure) {
  // A parser that stopped early would read `2026-09-20xyz` as a valid date.
  GCHRON_Format * format = compiled("yyyy-MM-dd");
  ASSERT_NE(nullptr, format);

  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_format_parse(format, "2026-09-20xyz", 13, nullptr, &fields, &err));
  EXPECT_EQ(GCHRON_DIAG_PATTERN_TRAILING, err.diag);
  EXPECT_EQ(10u, err.offset) << "the error does not point at the junk";
  EXPECT_EQ(10u, fields.consumed);
  gchron_format_destroy(format);
}

TEST(FormatParse, StrictWidthMeansExactlyThatManyDigits) {
  /*
   * `MM` reads two digits and not one. This is what makes `yyyyMMdd`
   * readable, and it is deliberately stricter than ICU's default, which
   * takes `2026-9-8` for `yyyy-MM-dd`.
   */
  GCHRON_Format * format = compiled("yyyy-MM-dd");
  ASSERT_NE(nullptr, format);
  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_format_parse(format, "2026-9-08", 9, nullptr, &fields, &err));
  gchron_format_destroy(format);

  // A single letter is the variable-width form and does take one digit.
  format = compiled("yyyy-M-d");
  ASSERT_NE(nullptr, format);
  EXPECT_EQ(GCHRON_OK,
      gchron_format_parse(format, "2026-9-8", 8, nullptr, &fields, &err))
      << gchron_diag_string(err.diag);
  EXPECT_EQ(9, fields.month);
  EXPECT_EQ(8, fields.day);
  gchron_format_destroy(format);
}

TEST(FormatParse, TheLetterCountIsAMinimumUntilAnotherNumberFollows) {
  /*
   * TR35's adjacent numeric value parsing, which is the rule the emitter and
   * the reader have to share. The emitter pads to the count as a *minimum*,
   * so `uuu` on year 2026 writes four digits - and a reader demanding exactly
   * three took `202` and then failed on the `6`. `fuzz_scan` found it through
   * `uuAg`; `uuu-MM-dd` is the same defect in a pattern somebody might type.
   *
   * The count becomes an exact width only where nothing else could find the
   * boundary: a number immediately followed by another number.
   */
  static const struct {
    const char * pattern;
    const char * text;
    int64_t year;
    int month;
    int day;
  } kRows[] = {
    // Followed by a literal: the count is a minimum and the read is greedy.
    { "uuu-MM-dd",   "2026-09-21",  2026, 9, 21 },
    { "uuuuu-MM-dd", "02026-09-21", 2026, 9, 21 },
    { "y-MM-dd",     "2026-09-21",  2026, 9, 21 },
    { "uuuu-M-d",    "2026-9-21",   2026, 9, 21 },
    { "uuuu-MM-dd",  "2026-09-21",  2026, 9, 21 },
    // Followed by another number: the count is the only boundary there is.
    { "uuuuMMdd",    "20260921",    2026, 9, 21 },
    { "uuMMdd",      "260921",        26, 9, 21 },
  };

  for (const auto & row : kRows) {
    GCHRON_Format * format = compiled(row.pattern);
    ASSERT_NE(nullptr, format) << row.pattern;
    GCHRON_ParsedFields fields{};
    GCHRON_Error err{};
    ASSERT_EQ(GCHRON_OK, gchron_format_parse(format, row.text,
        strlen(row.text), nullptr, &fields, &err))
        << row.pattern << " <- " << row.text << ": "
        << gchron_diag_string(err.diag);
    EXPECT_EQ(row.year, fields.year) << row.pattern << " <- " << row.text;
    EXPECT_EQ(row.month, fields.month) << row.pattern << " <- " << row.text;
    EXPECT_EQ(row.day, fields.day) << row.pattern << " <- " << row.text;
    gchron_format_destroy(format);
  }
}

TEST(FormatParse, AFieldNeverRecordsAValueItCannotHold) {
  /*
   * Range-checked where it is read, not only where it is resolved.
   * `fuzz_scan` found the reason: `uuuu-MM-d.'T'hH:mm` writes
   * `2026-09-21.T315:30`, the `h` read `31` - not an hour on any clock - and
   * the `H` after it took the leftover `5`. The second hour overwrote the
   * first, the impossible value vanished, and what came out was an ordinary
   * looking time that was simply wrong.
   */
  static const struct { const char * pattern; const char * text; } kRows[] = {
    { "hh:mm",     "13:00" },   // no thirteenth hour on a twelve-hour clock
    { "hh:mm",     "00:00" },   // nor a zeroth
    { "KK:mm",     "12:00" },   // `K` counts 0..11
    { "kk:mm",     "00:00" },   // `k` counts 1..24
    { "HH:mm",     "24:00" },
    { "HH:mm",     "15:60" },
    { "HH:mm:ss",  "15:30:61" },
    { "MM/dd",     "14/01" },
    { "MM/dd",     "00/01" },
    { "MM/dd",     "12/32" },
    { "DDD",       "367" },
    { "ww",        "54" },
  };
  for (const auto & row : kRows) {
    GCHRON_Format * format = compiled(row.pattern);
    ASSERT_NE(nullptr, format) << row.pattern;
    GCHRON_ParsedFields fields{};
    GCHRON_Error err{};
    EXPECT_EQ(GCHRON_ERR_FORMAT, gchron_format_parse(format, row.text,
        strlen(row.text), nullptr, &fields, &err))
        << row.pattern << " accepted \"" << row.text << "\"";
    gchron_format_destroy(format);
  }

  /*
   * The pattern that found this now reads correctly rather than being
   * refused, because the adjacency rule arrived with the range check: `h` is
   * followed by `H`, so it takes exactly one digit and the `H` takes the two
   * that follow. Kept as a case because it is the input that exposed both
   * defects, and because a reader that lost either rule would land back on
   * 05:30 - a plausible wrong answer, which is the dangerous kind.
   */
  {
    GCHRON_Format * ambiguous = compiled("uuuu-MM-d.'T'hH:mm");
    ASSERT_NE(nullptr, ambiguous);
    GCHRON_ParsedFields got{};
    GCHRON_Error e{};
    ASSERT_EQ(GCHRON_OK, gchron_format_parse(ambiguous,
        "2026-09-21.T315:30", 18, nullptr, &got, &e))
        << gchron_diag_string(e.diag);
    EXPECT_EQ(15, got.hour);
    EXPECT_EQ(GCHRON_HOUR_0_23, got.hour_cycle);
    EXPECT_EQ(30, got.minute);
    gchron_format_destroy(ambiguous);
  }

  // A leap second is not out of range here: whether one was issued that day
  // is the leap table's question, not the scanner's.
  GCHRON_Format * format = compiled("HH:mm:ss");
  ASSERT_NE(nullptr, format);
  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};
  EXPECT_EQ(GCHRON_OK,
      gchron_format_parse(format, "23:59:60", 8, nullptr, &fields, &err));
  EXPECT_EQ(60, fields.second);
  gchron_format_destroy(format);
}

TEST(FormatParse, ALiteralMustBeThere) {
  GCHRON_Format * format = compiled("yyyy-MM-dd");
  ASSERT_NE(nullptr, format);
  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_format_parse(format, "2026/09/20", 10, nullptr, &fields, &err));
  EXPECT_EQ(GCHRON_DIAG_PATTERN_LITERAL, err.diag);
  EXPECT_EQ(4u, err.offset);
  gchron_format_destroy(format);
}

/*--------------------------------------------------------------------------*
 * Resolution
 *--------------------------------------------------------------------------*/

TEST(FormatParse, NothingIsDefaultedThatWasNotRead) {
  // The whole reason a field set exists rather than a GCHRON_DateTime.
  GCHRON_Format * format = compiled("HH:mm");
  ASSERT_NE(nullptr, format);
  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};
  ASSERT_EQ(GCHRON_OK,
      gchron_format_parse(format, "15:30", 5, nullptr, &fields, &err));

  EXPECT_TRUE((fields.present & GCHRON_FIELD_HOUR) != 0);
  EXPECT_TRUE((fields.present & GCHRON_FIELD_MINUTE) != 0);
  EXPECT_FALSE((fields.present & GCHRON_FIELD_YEAR) != 0);
  EXPECT_FALSE((fields.present & GCHRON_FIELD_SECOND) != 0);

  // A time resolves - a second the text did not carry is zero, because
  // `15:30` means `15:30:00` in every notation this library reads.
  GCHRON_Time time{};
  ASSERT_EQ(GCHRON_OK, gchron_parsed_to_time(&fields, nullptr, &time, &err));
  EXPECT_EQ(15, time.hour);
  EXPECT_EQ(30, time.minute);
  EXPECT_EQ(0, time.second);

  // A date does not, and says which half was missing rather than inventing
  // one.
  GCHRON_Date date{};
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parsed_to_date(&fields, nullptr, &date, &err));
  EXPECT_EQ(GCHRON_DIAG_PATTERN_FIELD_MISSING, err.diag);

  GCHRON_DateTime dt{};
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parsed_to_datetime(&fields, nullptr, &dt, &err));
  gchron_format_destroy(format);
}

TEST(FormatParse, ADateWithNoTimeDoesNotBecomeMidnight) {
  GCHRON_Format * format = compiled("yyyy-MM-dd");
  ASSERT_NE(nullptr, format);
  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};
  ASSERT_EQ(GCHRON_OK,
      gchron_format_parse(format, "2026-09-21", 10, nullptr, &fields, &err));

  GCHRON_Date date{};
  ASSERT_EQ(GCHRON_OK, gchron_parsed_to_date(&fields, nullptr, &date, &err));
  EXPECT_EQ(2026, date.year);

  GCHRON_DateTime dt{};
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parsed_to_datetime(&fields, nullptr, &dt, &err))
      << "a date with no time silently became midnight";
  EXPECT_EQ(GCHRON_DIAG_PATTERN_FIELD_MISSING, err.diag);
  gchron_format_destroy(format);
}

TEST(FormatParse, TwelveHourTimeWithoutAMeridiemIsHalfAnAnswer) {
  GCHRON_Format * format = compiled("hh:mm");
  ASSERT_NE(nullptr, format);
  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};
  ASSERT_EQ(GCHRON_OK,
      gchron_format_parse(format, "03:30", 5, nullptr, &fields, &err));
  EXPECT_EQ(GCHRON_HOUR_1_12, fields.hour_cycle);

  GCHRON_Time time{};
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parsed_to_time(&fields, nullptr, &time, &err));
  EXPECT_EQ(GCHRON_DIAG_PATTERN_FIELD_MISSING, err.diag);
  gchron_format_destroy(format);
}

TEST(FormatParse, TheTwelveHourTrapsAtNoonAndMidnight) {
  // 12 AM is 00:00 and 12 PM is 12:00, which is the one pair every
  // hand-written twelve-hour reader gets backwards.
  static const struct { const char * text; int expected; } kRows[] = {
    { "12:00 AM", 0 }, { "12:00 PM", 12 },
    { "01:00 AM", 1 }, { "01:00 PM", 13 },
    { "11:59 AM", 11 }, { "11:59 PM", 23 },
  };
  GCHRON_Format * format = compiled("hh:mm a");
  ASSERT_NE(nullptr, format);
  for (const auto & row : kRows) {
    GCHRON_ParsedFields fields{};
    GCHRON_Error err{};
    ASSERT_EQ(GCHRON_OK, gchron_format_parse(format, row.text,
        strlen(row.text), nullptr, &fields, &err)) << row.text;
    GCHRON_Time time{};
    ASSERT_EQ(GCHRON_OK, gchron_parsed_to_time(&fields, nullptr, &time, &err))
        << row.text;
    EXPECT_EQ(row.expected, time.hour) << row.text;
  }
  gchron_format_destroy(format);
}

TEST(FormatParse, TwoFieldsThatDescribeTheSameDayMustAgree) {
  /*
   * A pattern carrying both `EEE` and `dd` gives two answers about one day.
   * Preferring either silently accepts a timestamp that contradicts itself,
   * which is more likely a bug in whatever wrote it than a field to discard.
   * Deliberately stricter than gchron_parse_http_date(), which ignores the
   * weekday because RFC 9110 tells it to.
   */
  GCHRON_Format * format = compiled("EEE yyyy-MM-dd HH:mm:ss");
  ASSERT_NE(nullptr, format);
  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};

  // 2026-09-21 really is a Monday.
  ASSERT_EQ(GCHRON_OK, gchron_format_parse(format,
      "Mon 2026-09-21 00:00:00", 23, nullptr, &fields, &err));
  GCHRON_DateTime dt{};
  EXPECT_EQ(GCHRON_OK, gchron_parsed_to_datetime(&fields, nullptr, &dt, &err));

  // It is not a Tuesday, and saying so is an error rather than a preference.
  ASSERT_EQ(GCHRON_OK, gchron_format_parse(format,
      "Tue 2026-09-21 00:00:00", 23, nullptr, &fields, &err));
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parsed_to_datetime(&fields, nullptr, &dt, &err));
  EXPECT_EQ(GCHRON_DIAG_PATTERN_FIELD_CONFLICT, err.diag);
  gchron_format_destroy(format);
}

TEST(FormatParse, AnOffsetIsNotInventedWhenTheTextHadNone) {
  // Calling a zoneless timestamp UTC is how a log line from Adelaide becomes
  // wrong by nine and a half hours.
  GCHRON_Format * format = compiled("yyyy-MM-dd HH:mm:ss");
  ASSERT_NE(nullptr, format);
  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};
  ASSERT_EQ(GCHRON_OK, gchron_format_parse(format,
      "2026-09-21 15:30:45", 19, nullptr, &fields, &err));

  GCHRON_OffsetDateTime odt{};
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_parsed_to_offset(&fields, nullptr, &odt, &err));
  EXPECT_EQ(GCHRON_DIAG_PATTERN_FIELD_MISSING, err.diag);
  gchron_format_destroy(format);
}

TEST(FormatParse, AWeekDateResolvesThroughItsOwnRoute) {
  GCHRON_Format * format = compiled("YYYY-'W'ww-e");
  ASSERT_NE(nullptr, format);
  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};
  ASSERT_EQ(GCHRON_OK,
      gchron_format_parse(format, "2026-W39-1", 10, nullptr, &fields, &err))
      << gchron_diag_string(err.diag);

  GCHRON_Date date{};
  ASSERT_EQ(GCHRON_OK, gchron_parsed_to_date(&fields, nullptr, &date, &err))
      << gchron_diag_string(err.diag);
  EXPECT_EQ(2026, date.year);
  EXPECT_EQ(9, date.month);
  EXPECT_EQ(21, date.day);
  gchron_format_destroy(format);
}

TEST(FormatParse, TheFieldsAreClearedBeforeEveryParse) {
  // A caller reusing one struct must not read a field out of it that this
  // parse never wrote - the same defect gchron_parse_options_default()
  // memsets against, and exactly what `strptime` does not do.
  GCHRON_Format * full = compiled("yyyy-MM-dd HH:mm:ss");
  GCHRON_Format * partial = compiled("HH:mm");
  ASSERT_NE(nullptr, full);
  ASSERT_NE(nullptr, partial);

  GCHRON_ParsedFields fields{};
  GCHRON_Error err{};
  ASSERT_EQ(GCHRON_OK, gchron_format_parse(full, "2026-09-21 15:30:45", 19,
      nullptr, &fields, &err));
  ASSERT_EQ(2026, fields.year);

  ASSERT_EQ(GCHRON_OK,
      gchron_format_parse(partial, "08:15", 5, nullptr, &fields, &err));
  EXPECT_EQ(0, fields.year) << "a field survived a parse that never wrote it";
  EXPECT_FALSE((fields.present & GCHRON_FIELD_YEAR) != 0);
  EXPECT_EQ(0, fields.day);

  gchron_format_destroy(full);
  gchron_format_destroy(partial);
}

TEST(FormatParse, NullsAndEmptyInputsAreRefusedRatherThanCrashing) {
  GCHRON_Format * format = compiled("yyyy");
  ASSERT_NE(nullptr, format);
  GCHRON_ParsedFields fields{};
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_format_parse(nullptr, "2026", 4, nullptr, &fields, nullptr));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_format_parse(format, "2026", 4, nullptr, nullptr, nullptr));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_format_parse(format, nullptr, 4, nullptr, &fields, nullptr));
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_format_parse(format, "", 0, nullptr, &fields, nullptr));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_parsed_to_date(nullptr, nullptr, nullptr, nullptr));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_format_is_invertible(nullptr, nullptr));
  gchron_format_destroy(format);
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
