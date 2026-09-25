/**
 * @file
 *
 * The pattern compiler and the named formats.
 *
 * The ICU differential (`make check-oracle-ldml`) checks what the letters
 * *mean*; what is here is the behaviour no differential covers - the errors,
 * the limits, the named formats, and the round trip that says every named
 * format has a parser.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <string>
#include <vector>

#include <cctype>
#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>
#include <ghoti.io/chron/format.h>

#include "test_helpers.h"

namespace {

std::string format_offset(const GCHRON_Format * format,
    const GCHRON_OffsetDateTime & odt, GCHRON_Result * result,
    const GCHRON_FormatContext * context = nullptr) {
  char buffer[256];
  size_t length = 0;
  *result = gchron_format_offset(format, &odt, context, buffer,
      sizeof(buffer), &length);
  return (*result == GCHRON_OK) ? std::string(buffer, length)
                                : std::string();
}

std::string with_pattern(const char * pattern, const GCHRON_OffsetDateTime & odt,
    GCHRON_FormatSyntax syntax = GCHRON_FORMAT_LDML) {
  GCHRON_Format * format = nullptr;
  EXPECT_EQ(GCHRON_OK,
      gchron_format_compile(pattern, std::strlen(pattern), syntax, nullptr,
          nullptr, &format, nullptr))
      << pattern;
  GCHRON_Result result;
  std::string text = format_offset(format, odt, &result);
  EXPECT_EQ(GCHRON_OK, result) << pattern;
  gchron_format_destroy(format);
  return text;
}

/**
 * `with_pattern` writes into 256 bytes, which the growth tests exceed. This
 * one asks the compiled format how much room it needs, so it also checks
 * that the bound is still right for a pattern whose pools were reallocated -
 * a bound computed from stale capacities would show up here as a refusal to
 * format rather than as wrong text.
 */
std::string with_long_pattern(const char * pattern,
    const GCHRON_OffsetDateTime & odt) {
  GCHRON_Format * format = nullptr;
  EXPECT_EQ(GCHRON_OK,
      gchron_format_compile(pattern, std::strlen(pattern), GCHRON_FORMAT_LDML,
          nullptr, nullptr, &format, nullptr));
  if (format == nullptr) {
    return std::string();
  }
  size_t bound = 0;
  EXPECT_EQ(GCHRON_OK, gchron_format_max_length(format, &bound));
  std::vector<char> buffer(bound);
  size_t length = 0;
  GCHRON_Result result = gchron_format_offset(format, &odt, nullptr,
      buffer.data(), buffer.size(), &length);
  EXPECT_EQ(GCHRON_OK, result);
  gchron_format_destroy(format);
  return (result == GCHRON_OK) ? std::string(buffer.data(), length)
                               : std::string();
}

GCHRON_OffsetDateTime sample() {
  GCHRON_DateTime civil =
      gchrontest::datetime(2026, 9, 20, 15, 30, 45, 123456789);
  GCHRON_OffsetDateTime odt{};
  EXPECT_EQ(GCHRON_OK, gchron_offset_create(&civil, 2 * 3600, false, &odt));
  return odt;
}

} // namespace

TEST(Format, QuotingFollowsTr35) {
  GCHRON_OffsetDateTime odt = sample();
  EXPECT_EQ("2026-09-20", with_pattern("uuuu-MM-dd", odt));
  EXPECT_EQ("at 15:30", with_pattern("'at' HH:mm", odt));
  // Two single quotes are one literal quote, inside a quoted run and out.
  EXPECT_EQ("it's 15", with_pattern("'it''s' HH", odt));
  EXPECT_EQ("'", with_pattern("''", odt));
  EXPECT_EQ("'2026", with_pattern("''uuuu", odt));
  // Four quotes are two literal quotes, not an empty quoted run - which is
  // what ICU does with it, and ICU is the definition (design.md section 8.3).
  EXPECT_EQ("''2026", with_pattern("''''uuuu", odt));
}

TEST(Format, AnUnterminatedQuoteIsAnErrorWithAPosition) {
  GCHRON_Format * format = nullptr;
  GCHRON_Error err{};
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_format_compile("HH:mm 'unterminated", 19, GCHRON_FORMAT_LDML,
          nullptr, nullptr, &format, &err));
  EXPECT_EQ(GCHRON_DIAG_UNTERMINATED_QUOTE, err.diag);
  EXPECT_EQ(6u, err.offset);
  EXPECT_NE(nullptr, err.message);
}

/*
 * TR35 reserves every ASCII letter as a pattern character, so an
 * unrecognised one is a pattern this library does not implement rather than a
 * literal. Treating it as a literal is how `b` in a pattern silently becomes
 * the letter b instead of an error a caller can act on.
 */
TEST(Format, AnUnknownPatternLetterIsRefusedRatherThanTakenAsText) {
  GCHRON_Format * format = nullptr;
  GCHRON_Error err{};
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_format_compile("HH:mm b", 7, GCHRON_FORMAT_LDML, nullptr,
          nullptr, &format, &err));
  EXPECT_EQ(GCHRON_DIAG_PATTERN_LETTER_UNKNOWN, err.diag);
  EXPECT_EQ(6u, err.offset);

  // Quoted, it is text and is fine.
  GCHRON_OffsetDateTime odt = sample();
  EXPECT_EQ("15:30 b", with_pattern("HH:mm 'b'", odt));
}

TEST(Format, TheCompilerIsBounded) {
  GCHRON_Format * format = nullptr;
  GCHRON_Limits limits;
  GCHRON_Error err{};
  std::string long_pattern(200, 'H');

  gchron_limits_default(&limits);
  limits.max_format_length = 8;
  EXPECT_EQ(GCHRON_ERR_LIMIT,
      gchron_format_compile("uuuu-MM-dd", 10, GCHRON_FORMAT_LDML, &limits,
          nullptr, &format, &err));
  EXPECT_EQ(GCHRON_DIAG_INPUT_TOO_LONG, err.diag);

  gchron_limits_default(&limits);
  limits.max_format_items = 2;
  EXPECT_EQ(GCHRON_ERR_LIMIT,
      gchron_format_compile("uuuu-MM-dd", 10, GCHRON_FORMAT_LDML, &limits,
          nullptr, &format, &err));

  // A run of one letter longer than any field uses is refused rather than
  // silently truncated - in ctang a pattern comes from a template and a
  // template may come from a user.
  gchron_limits_default(&limits);
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_format_compile(long_pattern.data(), long_pattern.size(),
          GCHRON_FORMAT_LDML, &limits, nullptr, &format, &err));
  EXPECT_EQ(GCHRON_DIAG_PATTERN_LETTER_RUN, err.diag);
}

TEST(Format, TheLengthBoundHoldsAndAZeroBufferAsksForTheLength) {
  GCHRON_Format * format = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_format_compile("EEEE, d MMMM uuuu 'at' HH:mm:ss XXX", 35,
          GCHRON_FORMAT_LDML, nullptr, nullptr, &format, nullptr));

  size_t bound = 0;
  ASSERT_EQ(GCHRON_OK, gchron_format_max_length(format, &bound));

  GCHRON_OffsetDateTime odt = sample();
  size_t length = 0;
  EXPECT_EQ(GCHRON_ERR_LIMIT,
      gchron_format_offset(format, &odt, nullptr, nullptr, 0, &length));
  EXPECT_GT(length, 0u);
  EXPECT_LT(length + 1, bound) << "the bound must actually bound";

  std::vector<char> buffer(bound);
  size_t written = 0;
  ASSERT_EQ(GCHRON_OK,
      gchron_format_offset(format, &odt, nullptr, buffer.data(), bound,
          &written));
  EXPECT_EQ(length, written);
  EXPECT_EQ(written, std::strlen(buffer.data()));

  // One byte short of what it needs is GCHRON_ERR_LIMIT, with the length
  // still reported.
  size_t again = 0;
  EXPECT_EQ(GCHRON_ERR_LIMIT,
      gchron_format_offset(format, &odt, nullptr, buffer.data(), written,
          &again));
  EXPECT_EQ(length, again);

  gchron_format_destroy(format);
}

/*
 * Mistake M14 again, on the way out through a pattern - and swept rather than
 * sampled, which is the whole point of this test.
 *
 * **No oracle here can reach this axis.** `make check-oracle-ldml` formats
 * zoned instants against ICU, and a zoned instant always has an offset, so
 * 3,780 agreeing comparisons say nothing at all about `offset_unknown`. What
 * stood in for the differential was this test asserting two spellings, `XXX`
 * and `Z`. Both of those two were right; three of the other fifteen were not:
 *
 *   - `XXXX` wrote `-00:00`, the *extended* form, while writing the basic form
 *     for every offset it did know. The old code said
 *     `count >= 3 ? "-00:00" : "-0000"`, and only counts 3 and 5 are extended.
 *   - `x` through `xxxxx` ignored the flag entirely and wrote `+00:00`, which
 *     is the opposite claim: `x` differs from `X` only in never writing `Z`,
 *     so it can spell a negative zero perfectly well.
 *   - `ZZZZ` wrote `-0000`, an ISO string from a localised-GMT pattern, and
 *     only for the unknown case - so one pattern could emit `GMT+00:00` and
 *     `-0000` into the same log.
 *
 * What replaced them is one rule: **an unknown offset is a negative zero,
 * written in the shape the letter uses for an offset it knows.** The sign is
 * the portable half of RFC 3339 section 4.3's convention - `X`, `XX`, `XXXX`
 * and `Z`..`ZZZ` are the ISO *basic* format and have no colon to write, so
 * `-00:00` exactly is not available to them - and it is the half the reader
 * keys on: scan.c treats a negative sign with a zero magnitude as unknown
 * whatever the spelling.
 *
 * Both columns are asserted. A fix to the unknown column that moved the known
 * one would otherwise pass.
 */
TEST(Format, EveryOffsetLetterSpellsAnUnknownOffsetInItsOwnShape) {
  struct Row {
    const char * pattern;
    const char * known;
    const char * unknown;
  };
  // `Z` for a known zero where the letter has it; the negative zero otherwise.
  static const Row rows[] = {
    // ISO 8601 with the UTC indicator. Basic at 1, 2 and 4; extended at 3
    // and 5. Count 1 omits minutes that are zero, which is why it is `-00`.
    { "X", "Z", "-00" },
    { "XX", "Z", "-0000" },
    { "XXX", "Z", "-00:00" },
    { "XXXX", "Z", "-0000" },
    { "XXXXX", "Z", "-00:00" },
    // The same shapes, and never `Z` - which is the only thing that makes `x`
    // a separate letter, and the reason it can still spell the negative zero.
    { "x", "+00", "-00" },
    { "xx", "+0000", "-0000" },
    { "xxx", "+00:00", "-00:00" },
    { "xxxx", "+0000", "-0000" },
    { "xxxxx", "+00:00", "-00:00" },
    // RFC 822. `ZZZZ` is the long localised GMT format and `ZZZZZ` the ISO
    // extended one, which is the pair TR35 splits off from the first three.
    { "Z", "+0000", "-0000" },
    { "ZZ", "+0000", "-0000" },
    { "ZZZ", "+0000", "-0000" },
    { "ZZZZ", "GMT+00:00", "GMT-00:00" },
    { "ZZZZZ", "Z", "-00:00" },
    // Localised GMT, and no exception to the rule. A bare `GMT` is what CLDR's
    // `gmtZeroFormat` means and what every ICU up to 76.1 wrote for a *known*
    // zero, so writing it for an unknown offset would collide with text this
    // library has to be able to read from other writers. `GMT-0` carries the
    // same convention `-00:00` carries for the ISO letters, and unlike a bare
    // `GMT` it round-trips.
    { "O", "GMT+0", "GMT-0" },
    { "OOOO", "GMT+00:00", "GMT-00:00" },
  };

  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 15, 30, 0);
  GCHRON_OffsetDateTime known{};
  GCHRON_OffsetDateTime unknown{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, false, &known));
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, true, &unknown));

  for (const Row & row : rows) {
    EXPECT_EQ(row.known, with_pattern(row.pattern, known))
        << "known zero offset through " << row.pattern;
    EXPECT_EQ(row.unknown, with_pattern(row.pattern, unknown))
        << "unknown offset through " << row.pattern;
  }
}

/*
 * And every spelling of it reads back as what it was, which is the property
 * the sign carries and the reason the shape is free to vary.
 *
 * Not a round trip of the value - a bare offset pattern names no date - but of
 * the *flag*, which is the thing a hard-coded `-00:00` was there to protect
 * and the thing nothing checked.
 *
 * This is also the test that makes the negative-zero rule exceptionless. While
 * `O`, `OOOO` and `ZZZZ` wrote a bare `GMT` for an unknown offset they could
 * not appear here, because a bare `GMT` is a known zero on the way back in.
 */
TEST(Format, AnUnknownOffsetReadsBackAsUnknownInEveryShapeThatCanBeRead) {
  // All seventeen. The localised GMT letters joined when their reader stopped
  // being unreachable; before that they were the only offset spellings this
  // library could write and not read.
  static const char * const readable[] = {
    "X", "XX", "XXX", "XXXX", "XXXXX",
    "x", "xx", "xxx", "xxxx", "xxxxx",
    "Z", "ZZ", "ZZZ", "ZZZZ", "ZZZZZ",
    "O", "OOOO",
  };
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 15, 30, 0);
  GCHRON_OffsetDateTime unknown{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, true, &unknown));

  for (const char * pattern : readable) {
    GCHRON_Format * format = nullptr;
    ASSERT_EQ(GCHRON_OK,
        gchron_format_compile(pattern, std::strlen(pattern),
            GCHRON_FORMAT_LDML, nullptr, nullptr, &format, nullptr));
    std::string text = with_pattern(pattern, unknown);
    GCHRON_ParsedFields fields{};
    EXPECT_EQ(GCHRON_OK,
        gchron_format_parse(format, text.c_str(), text.size(), nullptr,
            &fields, nullptr)) << pattern << " wrote " << text;
    EXPECT_TRUE(fields.offset_unknown) << pattern << " wrote " << text;
    EXPECT_EQ(0, fields.offset_sec) << pattern << " wrote " << text;
    gchron_format_destroy(format);
  }
}

/*
 * `gchron_format_is_invertible()` has to refuse exactly what the scanner
 * refuses, and it is now the *only* thing standing between a caller and the
 * scanner's own answer - so this asserts both, for each pattern, and that they
 * agree.
 *
 * Both halves were wrong at once and in opposite directions. It accepted `ZZZZ`
 * while `gchron_format_parse()` refused it, a promise of a round trip the
 * scanner could not deliver - hidden because `ZZZZ` wrote a readable `-0000`
 * for exactly one value of one flag. And it refused `O` and `OOOO` while
 * take_offset() had a complete, documented reader for them that no call site
 * reached.
 */
TEST(Format, IsInvertibleAgreesWithWhatTheScannerActuallyDoes) {
  struct Row { const char * pattern; const char * text; bool invertible; };
  static const Row rows[] = {
    { "X", "Z", true }, { "XXXXX", "-08:00", true },
    { "x", "+00", true }, { "xxxxx", "-08:00", true },
    { "Z", "+0000", true }, { "ZZ", "+0000", true }, { "ZZZ", "+0000", true },
    { "ZZZZZ", "Z", true },
    // The localised GMT format, by either spelling. Readable in root, which is
    // the only locale the writer has: GCHRON_Names carries no hook for
    // `gmtFormat` or `gmtZeroFormat`, so emit.c writes the literal `GMT`
    // whatever provider it is given.
    { "ZZZZ", "GMT-08:00", true },
    { "O", "GMT-8", true },
    { "OOOO", "GMT-08:00", true },
    // An abbreviation is not a bijection: `EST` is several zones, and choosing
    // between them needs CLDR data section 14 declines to ship.
    { "z", "EST", false }, { "zzzz", "Eastern Standard Time", false },
    // A compound pattern inherits the refusal from the one letter in it.
    { "uuuu-MM-dd'T'HH:mm:ss zzzz", "2026-09-20T15:30:00 X", false },
    { "uuuu-MM-dd'T'HH:mm:ssXXX", "2026-09-20T15:30:00Z", true },
  };

  for (const Row & row : rows) {
    GCHRON_Format * format = nullptr;
    ASSERT_EQ(GCHRON_OK,
        gchron_format_compile(row.pattern, std::strlen(row.pattern),
            GCHRON_FORMAT_LDML, nullptr, nullptr, &format, nullptr));
    GCHRON_Result said = gchron_format_is_invertible(format, nullptr);
    EXPECT_EQ(row.invertible ? GCHRON_OK : GCHRON_ERR_UNSUPPORTED, said)
        << row.pattern;

    /*
     * And the scanner agrees. A pattern it can read must not answer
     * GCHRON_ERR_UNSUPPORTED on text of the right shape, and a pattern it
     * cannot read must answer exactly that - not GCHRON_ERR_FORMAT, which
     * would mean "this text does not match" rather than "this letter never
     * can".
     */
    GCHRON_ParsedFields fields{};
    GCHRON_Result read = gchron_format_parse(format, row.text,
        std::strlen(row.text), nullptr, &fields, nullptr);
    if (row.invertible) {
      EXPECT_EQ(GCHRON_OK, read) << row.pattern << " on " << row.text;
    }
    else {
      EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, read) << row.pattern;
    }
    gchron_format_destroy(format);
  }
}

/*
 * The localised GMT reader accepts what other writers produce, not only what
 * this one does.
 *
 * A bare `GMT` is CLDR's `gmtZeroFormat` and is what every ICU up to 76.1 wrote
 * for a known zero offset, so it has to read as a known zero - which is also
 * the reason the unknown case cannot be spelled that way.
 */
TEST(Format, TheLocalisedGmtReaderTakesEverySpellingOfTheFormat) {
  struct Row {
    const char * pattern;
    const char * text;
    int32_t offset_sec;
    bool unknown;
    bool accepted;
  };
  static const Row rows[] = {
    { "O", "GMT", 0, false, true },
    { "OOOO", "GMT", 0, false, true },
    { "ZZZZ", "GMT", 0, false, true },
    { "O", "GMT+0", 0, false, true },
    { "OOOO", "GMT+00:00", 0, false, true },
    { "O", "GMT-0", 0, true, true },
    { "OOOO", "GMT-00:00", 0, true, true },
    { "O", "GMT-8", -8 * 3600, false, true },
    { "OOOO", "GMT-08:00", -8 * 3600, false, true },
    // The short form keeps minutes when they are not zero.
    { "O", "GMT+5:45", 5 * 3600 + 45 * 60, false, true },
    // And the seconds the tzdb records for local mean time.
    { "OOOO", "GMT+00:19:32", 19 * 60 + 32, false, true },
    // Refused: out of range, and a `gmtFormat` this locale does not have.
    { "O", "GMT+24", 0, false, false },
    { "O", "UTC+0", 0, false, false },
  };

  for (const Row & row : rows) {
    GCHRON_Format * format = nullptr;
    ASSERT_EQ(GCHRON_OK,
        gchron_format_compile(row.pattern, std::strlen(row.pattern),
            GCHRON_FORMAT_LDML, nullptr, nullptr, &format, nullptr));
    GCHRON_ParsedFields fields{};
    GCHRON_Result read = gchron_format_parse(format, row.text,
        std::strlen(row.text), nullptr, &fields, nullptr);
    if (!row.accepted) {
      EXPECT_NE(GCHRON_OK, read) << row.pattern << " on " << row.text;
    }
    else {
      ASSERT_EQ(GCHRON_OK, read) << row.pattern << " on " << row.text;
      EXPECT_EQ(row.offset_sec, fields.offset_sec) << row.text;
      EXPECT_EQ(row.unknown, fields.offset_unknown) << row.text;
    }
    gchron_format_destroy(format);
  }
}

/*
 * CLDR 48 changed what a zero offset looks like in the localised GMT format,
 * and this library followed it.
 *
 * Until ICU 76.1 a zero offset wrote CLDR's `gmtZeroFormat`, which root spells
 * `GMT`. TR35 revision 76 - CLDR 48 - added `"GMT+00:00" (long)` and
 * `"UTC+0" (short)` to the localized-GMT examples, where revision 75 and every
 * revision back to 68 have none, and ICU 78.3 writes the explicit form.
 * design.md section 8.3 makes ICU the definition of what a pattern means, so
 * this library writes it too. `make check-oracle-ldml` is what found the change
 * and is what keeps this agreeing; this test is what says it on a machine with
 * no ICU.
 *
 * The non-zero cases are here as the control. A change to the zero case that
 * reached the others would pass a test that only checked zero.
 */
TEST(Format, AZeroOffsetWritesAnExplicitZeroInTheLocalisedGmtFormat) {
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 15, 30, 0);
  GCHRON_OffsetDateTime zero{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, false, &zero));
  EXPECT_EQ("GMT+0", with_pattern("O", zero));
  EXPECT_EQ("GMT+00:00", with_pattern("OOOO", zero));
  // TR35 makes `ZZZZ` the long localised GMT format, the same as `OOOO`.
  EXPECT_EQ("GMT+00:00", with_pattern("ZZZZ", zero));

  GCHRON_OffsetDateTime west{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, -4 * 3600, false, &west));
  EXPECT_EQ("GMT-4", with_pattern("O", west));
  EXPECT_EQ("GMT-04:00", with_pattern("OOOO", west));

  // The short form keeps minutes when they are not zero, and both forms keep
  // seconds - the tzdb records pre-standard local mean time to the second.
  GCHRON_OffsetDateTime quarter{};
  ASSERT_EQ(GCHRON_OK,
      gchron_offset_create(&civil, 5 * 3600 + 45 * 60, false, &quarter));
  EXPECT_EQ("GMT+5:45", with_pattern("O", quarter));
  GCHRON_OffsetDateTime mean{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 19 * 60 + 32, false,
      &mean));
  EXPECT_EQ("GMT+00:19:32", with_pattern("OOOO", mean));

  // The unknown offset is EveryOffsetLetterSpellsAnUnknownOffsetInItsOwnShape's
  // to assert, for all seventeen spellings rather than these two.
}

TEST(Format, ALetterThatNeedsAZoneWithoutOneIsRefused) {
  GCHRON_Format * format = nullptr;
  GCHRON_OffsetDateTime odt = sample();
  GCHRON_Result result;

  ASSERT_EQ(GCHRON_OK,
      gchron_format_compile("VV", 2, GCHRON_FORMAT_LDML, nullptr, nullptr,
          &format, nullptr));
  format_offset(format, odt, &result);
  // An offset date-time has no zone name, and inventing one would be mistake
  // M13 from the other direction.
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, result);
  gchron_format_destroy(format);

  ASSERT_EQ(GCHRON_OK,
      gchron_format_compile("z", 1, GCHRON_FORMAT_LDML, nullptr, nullptr,
          &format, nullptr));
  format_offset(format, odt, &result);
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, result);
  gchron_format_destroy(format);
}

TEST(Format, TheFractionTruncatesAndNeverRounds) {
  GCHRON_OffsetDateTime odt = sample(); // .123456789
  EXPECT_EQ("1", with_pattern("S", odt));
  EXPECT_EQ("123", with_pattern("SSS", odt));
  EXPECT_EQ("123456", with_pattern("SSSSSS", odt));
  EXPECT_EQ("123456789", with_pattern("SSSSSSSSS", odt));
  // More digits than a nanosecond has are zeros, not noise.
  EXPECT_EQ("1234567890", with_pattern("SSSSSSSSSS", odt));

  GCHRON_DateTime nearly =
      gchrontest::datetime(2026, 9, 20, 23, 59, 59, 999999999);
  GCHRON_OffsetDateTime edge{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&nearly, 0, false, &edge));
  // Rounding .999999999 to three digits would carry into the next day.
  EXPECT_EQ("2026-09-20 23:59:59.999", with_pattern("uuuu-MM-dd HH:mm:ss.SSS",
      edge));
}

// Mistake M8: `y` is the year of the era and `u` is the astronomical year,
// and year 0 is where they part company.
TEST(Format, TheYearLettersDifferAtTheEraBoundary) {
  GCHRON_DateTime year_zero = gchrontest::datetime(0, 6, 15, 12, 0, 0);
  GCHRON_OffsetDateTime odt{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&year_zero, 0, false, &odt));

  // Year 0 is 1 BCE, so `y` prints 1 and `u` prints 0.
  EXPECT_EQ("0001", with_pattern("yyyy", odt));
  EXPECT_EQ("0000", with_pattern("uuuu", odt));
  EXPECT_EQ("BCE", with_pattern("G", odt));

  GCHRON_DateTime year_one = gchrontest::datetime(1, 6, 15, 12, 0, 0);
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&year_one, 0, false, &odt));
  EXPECT_EQ("0001", with_pattern("yyyy", odt));
  EXPECT_EQ("0001", with_pattern("uuuu", odt));
  EXPECT_EQ("CE", with_pattern("G", odt));
}

TEST(Format, EveryNamedFormatCompilesAndProducesItsDocumentedShape) {
  struct Case { GCHRON_NamedFormat named; const char * expected; };
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 15, 30, 0);
  GCHRON_OffsetDateTime odt{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, false, &odt));

  const Case cases[] = {
    { GCHRON_NAMED_RFC3339, "2026-09-20T15:30:00Z" },
    { GCHRON_NAMED_RFC3339_NANOS, "2026-09-20T15:30:00.000000000Z" },
    { GCHRON_NAMED_ISO8601_BASIC, "20260920T153000Z" },
    { GCHRON_NAMED_ISO_WEEK, "2026-W38-7" },
    { GCHRON_NAMED_ISO_ORDINAL, "2026-263" },
    { GCHRON_NAMED_HTTP, "Sun, 20 Sep 2026 15:30:00 GMT" },
    { GCHRON_NAMED_RFC5322, "Sun, 20 Sep 2026 15:30:00 +0000" },
  };
  for (const Case & c : cases) {
    const GCHRON_Format * format = gchron_format_named(c.named);
    ASSERT_NE(nullptr, format) << c.expected;
    GCHRON_Result result;
    EXPECT_EQ(std::string(c.expected), format_offset(format, odt, &result))
        << c.expected;
    EXPECT_EQ(GCHRON_OK, result);
  }

  // GCHRON_NAMED_RFC9557 is the one that cannot be written from an offset
  // date-time, because `VV` needs a zone rather than an offset. It was left
  // out of the list above for years on that account, which is how the two
  // ways of writing RFC 9557 came to disagree without anybody noticing.
  gchrontest::ZoneDb db;
  const GCHRON_Zone * paris = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_zone(db.get(), "Europe/Paris", &paris));
  GCHRON_Instant when = { 1789918200, 0 };  // 2026-09-20T15:30:00Z
  GCHRON_ZonedDateTime zoned{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_from_instant(when, paris, &zoned));

  char buffer[128];
  size_t length = 0;
  ASSERT_EQ(GCHRON_OK,
      gchron_format_zoned(gchron_format_named(GCHRON_NAMED_RFC9557), &zoned,
          nullptr, buffer, sizeof(buffer), &length));
  EXPECT_EQ("2026-09-20T17:30:00+02:00[Europe/Paris]",
      std::string(buffer, length));

  // Out of the enum is NULL rather than a wild read.
  EXPECT_EQ(nullptr, gchron_format_named(GCHRON_NAMED_COUNT));
  EXPECT_EQ(nullptr,
      gchron_format_named(static_cast<GCHRON_NamedFormat>(-1)));
}

/*
 * `parse(write(x))` for every named format, which format.h claims and nothing
 * checked. The claim was attributed to tests/unit/test_roundtrip.cpp, which
 * is entirely about gchron_write_rfc3339_date_time() and has never called
 * gchron_format_named().
 *
 * It is not one property, because two of the eight write a date and no time
 * of day. design.md section 12.1 property 3 is "identical for every value the
 * format can represent losslessly, and where the format is lossy the loss is
 * exactly the documented one" - so each format says which it is, and the loss
 * is written down here rather than discovered.
 */
TEST(Format, ParseOfWriteIsIdenticalForEveryNamedFormat) {
  gchrontest::ZoneDb db;
  const GCHRON_Zone * paris = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_zone(db.get(), "Europe/Paris", &paris));
  GCHRON_Instant when = { 1789918200, 0 };  // 2026-09-20T15:30:00Z
  GCHRON_ZonedDateTime zoned{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_from_instant(when, paris, &zoned));

  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 17, 30, 0);
  GCHRON_OffsetDateTime odt{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 2 * 3600, false, &odt));

  enum Loss {
    NOTHING,      // the whole offset date-time comes back
    TIME_OF_DAY,  // a date-only format; the date comes back
    THE_OFFSET,   // an HTTP-date is GMT, so the local reading is not in it
  };
  struct Case {
    GCHRON_NamedFormat named;
    const char * written;
    Loss loss;
  };
  const Case cases[] = {
    { GCHRON_NAMED_RFC3339, "2026-09-20T17:30:00+02:00", NOTHING },
    { GCHRON_NAMED_RFC3339_NANOS, "2026-09-20T17:30:00.000000000+02:00",
      NOTHING },
    { GCHRON_NAMED_RFC9557, "2026-09-20T17:30:00+02:00[Europe/Paris]",
      NOTHING },
    { GCHRON_NAMED_ISO8601_BASIC, "20260920T173000+0200", NOTHING },
    { GCHRON_NAMED_ISO_WEEK, "2026-W38-7", TIME_OF_DAY },
    { GCHRON_NAMED_ISO_ORDINAL, "2026-263", TIME_OF_DAY },
    { GCHRON_NAMED_HTTP, "Sun, 20 Sep 2026 15:30:00 GMT", THE_OFFSET },
    { GCHRON_NAMED_RFC5322, "Sun, 20 Sep 2026 17:30:00 +0200", NOTHING },
  };
  ASSERT_EQ(static_cast<size_t>(GCHRON_NAMED_COUNT),
      sizeof(cases) / sizeof(cases[0]))
      << "a named format was added without saying what it loses";

  for (const Case & c : cases) {
    const GCHRON_Format * format = gchron_format_named(c.named);
    ASSERT_NE(nullptr, format) << c.written;

    // Every one of them claims to be readable; that is what the claim rests
    // on, so ask before relying on it.
    EXPECT_EQ(GCHRON_OK, gchron_format_is_invertible(format, nullptr))
        << c.written;

    char buffer[128];
    size_t length = 0;
    ASSERT_EQ(GCHRON_OK,
        gchron_format_zoned(format, &zoned, nullptr, buffer, sizeof(buffer),
            &length)) << c.written;
    EXPECT_EQ(std::string(c.written), std::string(buffer, length));

    GCHRON_ParsedFields fields{};
    ASSERT_EQ(GCHRON_OK,
        gchron_format_parse(format, buffer, length, nullptr, &fields, nullptr))
        << c.written;

    if (c.loss == TIME_OF_DAY) {
      GCHRON_Date back{};
      ASSERT_EQ(GCHRON_OK,
          gchron_parsed_to_date(&fields, nullptr, &back, nullptr))
          << c.written;
      EXPECT_EQ(2026, back.year) << c.written;
      EXPECT_EQ(9, back.month) << c.written;
      EXPECT_EQ(20, back.day) << c.written;
      // And there is no time of day in it to ask for.
      GCHRON_DateTime whole{};
      EXPECT_NE(GCHRON_OK,
          gchron_parsed_to_datetime(&fields, nullptr, &whole, nullptr))
          << c.written;
      continue;
    }

    GCHRON_OffsetDateTime back{};
    ASSERT_EQ(GCHRON_OK,
        gchron_parsed_to_offset(&fields, nullptr, &back, nullptr))
        << c.written;
    // The instant survives every one of them; that is the property that
    // matters, and it is the one an HTTP-date keeps while losing the local
    // reading.
    GCHRON_Instant again{};
    ASSERT_EQ(GCHRON_OK, gchron_offset_to_instant(&back, &again));
    EXPECT_EQ(when.sec, again.sec) << c.written;
    EXPECT_EQ(when.nsec, again.nsec) << c.written;

    if (c.loss == NOTHING) {
      EXPECT_EQ(2 * 3600, back.offset_sec) << c.written;
      EXPECT_EQ(17, back.civil.time.hour) << c.written;
    }
    else {
      // The documented loss: an HTTP-date is GMT and says nothing about
      // where the sender was, so it reads back as the UTC civil time.
      EXPECT_EQ(0, back.offset_sec) << c.written;
      EXPECT_EQ(15, back.civil.time.hour) << c.written;
    }
  }
}

/*
 * Two ways to write RFC 9557, and they answer differently for a zone with no
 * name - which is every fixed-offset zone and every zone built from a `TZ`
 * rule or a plain-file `/etc/localtime`.
 *
 * Both answers are right for their layer, and the pairing is the thing worth
 * pinning. A pattern containing `VV` is a caller asking for the identifier,
 * and GCHRON_FormatContext::zone already promises that a zone without one is
 * GCHRON_ERR_UNSUPPORTED "rather than inventing a name". gchron_write_rfc9557
 * is not asking for the identifier, it is writing the format, and RFC 3339 is
 * valid RFC 9557 - so it degrades, which loses nothing, because there was no
 * name to lose.
 *
 * What was wrong before is that neither said so, and nothing tested it.
 */
TEST(Format, TheTwoWaysToWriteRfc9557PartCompanyOverANamelessZone) {
  gchrontest::ZoneDb db;
  const GCHRON_Zone * fixed = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_fixed(db.get(), 5 * 3600 + 1800,
      &fixed));
  ASSERT_EQ(nullptr, gchron_zone_id(fixed));

  GCHRON_Instant when = { 1789918200, 0 };  // 2026-09-20T15:30:00Z
  GCHRON_ZonedDateTime zoned{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_from_instant(when, fixed, &zoned));

  char buffer[128];
  size_t length = 0;
  ASSERT_EQ(GCHRON_OK,
      gchron_write_rfc9557(&zoned, nullptr, buffer, sizeof(buffer), &length));
  EXPECT_EQ("2026-09-20T21:00:00+05:30", std::string(buffer, length));

  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_format_zoned(gchron_format_named(GCHRON_NAMED_RFC9557), &zoned,
          nullptr, buffer, sizeof(buffer), &length));

  // They agree wherever there is a name to agree about.
  const GCHRON_Zone * paris = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_zone(db.get(), "Europe/Paris", &paris));
  ASSERT_EQ(GCHRON_OK, gchron_zoned_from_instant(when, paris, &zoned));
  char direct[128];
  size_t direct_length = 0;
  ASSERT_EQ(GCHRON_OK, gchron_write_rfc9557(&zoned, nullptr, direct,
      sizeof(direct), &direct_length));
  ASSERT_EQ(GCHRON_OK,
      gchron_format_zoned(gchron_format_named(GCHRON_NAMED_RFC9557), &zoned,
          nullptr, buffer, sizeof(buffer), &length));
  EXPECT_EQ(std::string(direct, direct_length), std::string(buffer, length));
}

/*
 * RFC 9110 section 5.6.7: an HTTP-date "represents time as an instance of
 * Coordinated Universal Time", and the grammar spells that as the literal
 * `GMT`. A pattern cannot say it - it prints whatever civil reading it is
 * handed - so this format used to write a value's *local* time beside that
 * literal, and `2026-09-20T17:30:00+02:00` came out as
 * `Sun, 20 Sep 2026 17:30:00 GMT`: two hours wrong, and labelled as though
 * it were not. gchron_write_http_date() has always converted first and says
 * so in its documentation; the two now give the same answer, which is the
 * point of having the named one at all.
 */
TEST(Format, TheNamedHttpFormatMovesTheValueIntoGmtRatherThanLabellingIt) {
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 17, 30, 0);
  GCHRON_OffsetDateTime odt{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 2 * 3600, false, &odt));

  const GCHRON_Format * format = gchron_format_named(GCHRON_NAMED_HTTP);
  ASSERT_NE(nullptr, format);
  GCHRON_Result result;
  EXPECT_EQ("Sun, 20 Sep 2026 15:30:00 GMT", format_offset(format, odt,
      &result));
  EXPECT_EQ(GCHRON_OK, result);

  char direct[GCHRON_HTTP_DATE_MAX];
  size_t length = 0;
  ASSERT_EQ(GCHRON_OK,
      gchron_write_http_date(&odt, direct, sizeof(direct), &length));
  EXPECT_EQ(std::string(direct), format_offset(format, odt, &result));

  // A western offset moves the other way, and past midnight - which is the
  // case a conversion that only adjusted the clock would get wrong.
  GCHRON_DateTime late = gchrontest::datetime(2026, 9, 20, 23, 30, 0);
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&late, -5 * 3600, false, &odt));
  EXPECT_EQ("Mon, 21 Sep 2026 04:30:00 GMT", format_offset(format, odt,
      &result));

  // Every other named format carries its own offset, so none of them moves.
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 2 * 3600, false, &odt));
  EXPECT_EQ("2026-09-20T17:30:00+02:00",
      format_offset(gchron_format_named(GCHRON_NAMED_RFC3339), odt, &result));
  EXPECT_EQ("Sun, 20 Sep 2026 17:30:00 +0200",
      format_offset(gchron_format_named(GCHRON_NAMED_RFC5322), odt, &result));
}

// A named format is static, and a caller holding "whichever format was
// chosen" should not have to remember which kind it is.
TEST(Format, DestroyingANamedFormatIsIgnored) {
  const GCHRON_Format * named = gchron_format_named(GCHRON_NAMED_RFC3339);
  ASSERT_NE(nullptr, named);
  gchron_format_destroy(const_cast<GCHRON_Format *>(named));
  gchron_format_destroy(nullptr);
  // Still usable afterwards.
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 15, 30, 0);
  GCHRON_OffsetDateTime odt{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, false, &odt));
  GCHRON_Result result;
  EXPECT_EQ("2026-09-20T15:30:00Z", format_offset(named, odt, &result));
}

TEST(Format, TheZoneLettersWorkOnAZonedValue) {
  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK, gchrontest::open_zonedb(&db));
  const GCHRON_Zone * zone = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_zone(db, "America/New_York", &zone));

  GCHRON_Instant instant{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1781539200, 0, &instant));
  GCHRON_ZonedDateTime zoned{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_from_instant(instant, zone, &zoned));

  GCHRON_Format * format = nullptr;
  char buffer[128];
  size_t length = 0;

  ASSERT_EQ(GCHRON_OK,
      gchron_format_compile("uuuu-MM-dd HH:mm:ss z XXX '['VV']'", 34,
          GCHRON_FORMAT_LDML, nullptr, nullptr, &format, nullptr));
  ASSERT_EQ(GCHRON_OK,
      gchron_format_zoned(format, &zoned, nullptr, buffer, sizeof(buffer),
          &length));
  EXPECT_EQ("2026-06-15 12:00:00 EDT -04:00 [America/New_York]",
      std::string(buffer, length));

  gchron_format_destroy(format);
  gchron_zonedb_destroy(db);
}

TEST(Format, TheNamesProviderIsWhereNamesComeFrom) {
  // A provider that speaks nothing but nonsense still drives the formatter,
  // which is the whole point of the seam.
  struct Silly {
    static const char * month(const GCHRON_Names *, int m, GCHRON_NameWidth) {
      static const char * const NAMES[13] = {
        nullptr, "one", "two", "three", "four", "five", "six", "seven",
        "eight", "nine", "ten", "eleven", "twelve"
      };
      return (m < 1 || m > 12) ? nullptr : NAMES[m];
    }
    static const char * weekday(const GCHRON_Names *, int, GCHRON_NameWidth) {
      return "day";
    }
    static const char * era(const GCHRON_Names *, int, GCHRON_NameWidth) {
      return "era";
    }
    static const char * period(const GCHRON_Names *, int, GCHRON_NameWidth) {
      return "half";
    }
  };
  GCHRON_Names silly{};
  silly.id = "silly";
  silly.month = Silly::month;
  silly.weekday = Silly::weekday;
  silly.era = Silly::era;
  silly.day_period = Silly::period;
  silly.first_day_of_week = 1;
  silly.minimum_days_in_first_week = 4;

  GCHRON_FormatContext context{};
  context.names = &silly;

  GCHRON_Format * format = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_format_compile("EEEE d MMMM G a", 15, GCHRON_FORMAT_LDML,
          nullptr, nullptr, &format, nullptr));
  GCHRON_OffsetDateTime odt = sample();
  GCHRON_Result result;
  EXPECT_EQ("day 20 nine era half", format_offset(format, odt, &result,
      &context));
  EXPECT_EQ(GCHRON_OK, result);
  gchron_format_destroy(format);

  // And the one this library ships never reads a locale setting.
  EXPECT_STREQ("root", gchron_names_english()->id);
  EXPECT_STREQ("September", gchron_names_english()->month(
      gchron_names_english(), 9, GCHRON_NAME_WIDE));
}

/*
 * `gchron_format_max_length` exists so a caller can size a buffer once and
 * then format into it without checking. That promise is only worth having if
 * it holds for every pattern, so this asserts it over every letter at every
 * count the compiler accepts, against the values whose fields are widest.
 * `fuzz_format` found the case that motivated it: `u` and `Y` on a negative
 * year write the sign outside the zero padding, so a count past nine digits
 * needed one byte more than it had been promised.
 */
/*
 * Whatever this library writes, it reads - or says up front that it cannot.
 *
 * `gchron_format_is_invertible()` exists so that a caller can ask once, when a
 * pattern arrives from a template, instead of on the first line of a log. That
 * makes it a promise about `gchron_format_parse()`, and a promise nothing
 * checked: both halves were wrong at once, in opposite directions, and for a
 * long time.
 *
 *   - It accepted `ZZZZ` while the scan refused it, because `ZZZZ` is the long
 *     localised GMT format and not the RFC 822 offset its shorter spellings
 *     are. Hidden by a second defect: `ZZZZ` wrote a readable `-0000` for an
 *     unknown offset, so the promise held for one value of one flag.
 *   - It refused `O` and `OOOO` while take_offset() carried a complete,
 *     documented reader for `GMT`, `GMT+8` and `GMT+08:00` that no call site
 *     reached - twenty lines that could not run, under a refusal whose stated
 *     reason (a locale that localises the word) describes a library this is
 *     not, since GCHRON_Names has no hook for `gmtFormat`.
 *
 * So this sweeps the same generated population TheDeclaredBoundReallyBounds-
 * EveryPattern does - every letter, every count that compiles - rather than a
 * list, because a list is what both of those hid behind. The value is zoned so
 * that every letter has what it needs.
 *
 * A pattern that inverts has to read **its own output**, and the only exemption
 * is named rather than a class: `GCHRON_ERR_INVALID` with
 * ::GCHRON_DIAG_PATTERN_NEEDS_CLOCK, which is `yy` and `YY` declining to guess
 * a century because this library never calls the system clock on its own
 * (mistake M18).
 *
 * The first draft of this only forbade GCHRON_ERR_UNSUPPORTED, and that was too
 * weak by exactly the margin that matters: breaking the dispatch so that the
 * localised GMT letters were read with the wrong style made them fail with
 * GCHRON_ERR_FORMAT, which the loose version accepted. A reader that is present
 * and wrong is the case this test exists for, so "did not refuse the letter" is
 * not enough - it has to succeed.
 *
 * **Only one direction can break, and this says which.**
 * `gchron_format_parse()` calls `gchron_format_is_invertible()` first, so a
 * pattern this function refuses can never parse: that half of the loop below is
 * held by construction and cannot fail, and is asserted for its diagnostic
 * rather than as a control. The half with teeth is the other one, and it is
 * where the `ZZZZ` defect lived - the gate said yes and then the *dispatch*
 * refused, because "what can be read" was written down in two places. Reviving
 * the localised GMT reader collapsed them into one.
 */
TEST(Format, EveryPatternThatSaysItInvertsIsOneTheScannerWillTry) {
  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK, gchrontest::open_zonedb(&db));
  const GCHRON_Zone * zone = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_zone(db, "America/New_York", &zone));
  GCHRON_Instant instant{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1781539200, 0, &instant));
  GCHRON_ZonedDateTime zoned{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_from_instant(instant, zone, &zoned));

  int inverted = 0;
  int refused = 0;
  for (char ch = 'A'; ch <= 'z'; ++ch) {
    if (!std::isalpha(static_cast<unsigned char>(ch))) {
      continue;
    }
    for (int count = 1; count <= 20; ++count) {
      std::string pattern(static_cast<size_t>(count), ch);
      GCHRON_Format * format = nullptr;
      if (gchron_format_compile(pattern.c_str(), pattern.size(),
              GCHRON_FORMAT_LDML, nullptr, nullptr, &format, nullptr)
          != GCHRON_OK) {
        continue; /* a count this letter does not accept */
      }
      char buffer[256];
      size_t length = 0;
      if (gchron_format_zoned(format, &zoned, nullptr, buffer, sizeof(buffer),
              &length) != GCHRON_OK) {
        gchron_format_destroy(format);
        continue; /* a letter this value cannot feed */
      }
      GCHRON_Result says = gchron_format_is_invertible(format, nullptr);
      GCHRON_ParsedFields fields{};
      GCHRON_Error err{};
      GCHRON_Result read = gchron_format_parse(format, buffer, length, nullptr,
          &fields, &err);
      if (says == GCHRON_OK) {
        ++inverted;
        if (read != GCHRON_OK) {
          EXPECT_EQ(GCHRON_ERR_INVALID, read)
              << pattern << " wrote " << std::string(buffer, length)
              << " and is_invertible said it could be read back";
          EXPECT_EQ(GCHRON_DIAG_PATTERN_NEEDS_CLOCK, err.diag)
              << pattern << " wrote " << std::string(buffer, length);
        }
      }
      else {
        ++refused;
        EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, says) << pattern;
        // Structural, per the note above: parse() gates on is_invertible(), so
        // this cannot fail. Asserted for the *diagnostic*, which can - a
        // refusal that reached the caller as GCHRON_DIAG_NONE would tell a
        // template author nothing about which letter to change.
        EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, read) << pattern;
        EXPECT_EQ(GCHRON_DIAG_PATTERN_NOT_INVERTIBLE, err.diag) << pattern;
        EXPECT_LT(err.offset, pattern.size()) << pattern;
      }
      gchron_format_destroy(format);
    }
  }
  // A sweep that swept nothing passes every assertion in it.
  EXPECT_GT(inverted, 50);
  EXPECT_GT(refused, 0);
  gchron_zonedb_destroy(db);
}

TEST(Format, TheDeclaredBoundReallyBoundsEveryPattern) {
  static const struct {
    int32_t year;
    int month, day, hour, minute, second;
    int32_t nsec, offset;
  } kWidest[] = {
    { 2026, 9, 20, 15, 30, 45, 123456789, 2 * 3600 },
    { -999999999, 1, 1, 0, 0, 0, 0, -86399 },
    { 999999999, 12, 31, 23, 59, 59, 999999999, 86399 },
    { -1, 12, 31, 23, 59, 59, 999999999, -1 },
    { 0, 2, 29, 12, 0, 0, 1, 1171 },
  };

  int checked = 0;
  for (char ch = 'A'; ch <= 'z'; ++ch) {
    if (!std::isalpha(static_cast<unsigned char>(ch))) {
      continue;
    }
    for (int count = 1; count <= 20; ++count) {
      std::string pattern(static_cast<size_t>(count), ch);
      GCHRON_Format * format = nullptr;
      if (gchron_format_compile(pattern.c_str(), pattern.size(),
              GCHRON_FORMAT_LDML, nullptr, nullptr, &format, nullptr)
          != GCHRON_OK) {
        continue; /* a count this letter does not accept */
      }
      size_t bound = 0;
      ASSERT_EQ(GCHRON_OK, gchron_format_max_length(format, &bound));

      for (const auto & v : kWidest) {
        GCHRON_DateTime civil{};
        GCHRON_OffsetDateTime odt{};
        if (gchron_date_create(v.year, v.month, v.day, &civil.date) != GCHRON_OK
            || gchron_time_create(v.hour, v.minute, v.second, v.nsec,
                   &civil.time) != GCHRON_OK
            || gchron_offset_create(&civil, v.offset, false, &odt)
                != GCHRON_OK) {
          continue;
        }

        // A buffer of exactly the declared size must be enough - that is the
        // entire promise. Sized from the bound, not from what we expect.
        std::vector<char> buffer(bound, '\xAB');
        size_t written = 0;
        GCHRON_Result result = gchron_format_offset(format, &odt, nullptr,
            buffer.data(), buffer.size(), &written);
        if (result == GCHRON_ERR_UNSUPPORTED || result == GCHRON_ERR_RANGE) {
          continue; /* a letter this value cannot supply */
        }
        ASSERT_EQ(GCHRON_OK, result)
            << "pattern '" << pattern << "' year " << v.year
            << " did not fit its own declared bound of " << bound;
        EXPECT_LT(written, bound) << "pattern '" << pattern << "'";
        ++checked;
      }
      gchron_format_destroy(format);
    }
  }
  EXPECT_GT(checked, 2000) << "the sweep stopped covering patterns";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/*
 * The literal pool and the item list each take a first allocation of a fixed
 * size and double from there. Until these tests the doubling loop in
 * push_literal had never run: every pattern in the suite held 32 bytes of
 * literal text or fewer, so the pool was allocated once at its initial size
 * and never grown, and `make coverage` reported those two lines as the only
 * growth path in the library no test reached.
 *
 * Reaching them is not the point. A reallocation that loses bytes, or that
 * leaves an earlier item naming the wrong place in the moved pool, is the
 * kind of defect that produces corrupted output rather than a crash - so
 * these check the bytes that come out, at and either side of every size
 * where the capacity changes.
 */
TEST(Format, TheLiteralPoolGrowsWithoutLosingWhatItHeld) {
  GCHRON_OffsetDateTime odt = sample();

  for (size_t run : {31u, 32u, 33u, 63u, 64u, 65u, 127u, 128u, 129u, 255u,
           256u, 257u, 511u, 512u, 513u}) {
    std::string text(run, 'a');
    std::string pattern = "'" + text + "'uuuu";
    EXPECT_EQ(text + "2026", with_long_pattern(pattern.c_str(), odt))
        << "literal run of " << run << " bytes";
  }
}

/*
 * The one a growth defect would actually break. Each run is stored as an
 * offset and a length into a pool that realloc is free to move, so the runs
 * written before the move have to still name the right bytes after it. The
 * long run at the end is what forces the pool past its first allocation, and
 * the three before it were written while it was somewhere else.
 */
TEST(Format, LiteralsWrittenBeforeAGrowthStillReadCorrectlyAfterIt) {
  GCHRON_OffsetDateTime odt = sample();
  std::string tail(200, 'z');
  std::string pattern = "'alpha'uuuu'beta'MM'" + tail + "'dd";

  EXPECT_EQ("alpha2026beta09" + tail + "20",
      with_long_pattern(pattern.c_str(), odt));
}

/*
 * The item list doubles from 16. `y'x'` compiles to two items, so the
 * boundaries fall at 8, 16 and 32 repetitions.
 */
TEST(Format, TheItemListGrowsAcrossItsDoublingBoundaries) {
  GCHRON_OffsetDateTime odt = sample();

  for (size_t pairs : {7u, 8u, 9u, 15u, 16u, 17u, 31u, 32u, 40u}) {
    std::string pattern;
    std::string expected;
    for (size_t i = 0; i < pairs; ++i) {
      pattern += "y'x'";
      expected += "2026x";
    }
    EXPECT_EQ(expected, with_long_pattern(pattern.c_str(), odt))
        << pairs << " item pairs";
  }
}

/*
 * The other half of a growth path is the growth that fails, and it is
 * unreachable from any input: it is taken when the allocator says no, and
 * the default allocator does not. Granting a fixed number of allocations and
 * refusing the rest walks the refusal one allocation further in each time.
 *
 * `'x'` needs exactly three, in this order: the format struct, the literal
 * pool, the item list. Each refusal has to be reported as GCHRON_ERR_OOM
 * rather than returning a half-built format, and the caller's pointer has to
 * be left alone - a caller who checks the result and not the pointer is the
 * common case, but one who checks the pointer should not find a dangling one.
 */
TEST(Format, ARefusedAllocationIsReportedRatherThanHalfCompiled) {
  for (int grants = 0; grants < 3; ++grants) {
    gchrontest::FailingAllocator allocator(grants);
    GCHRON_Format * format = nullptr;

    EXPECT_EQ(GCHRON_ERR_OOM,
        gchron_format_compile("'x'", 3, GCHRON_FORMAT_LDML, nullptr,
            allocator.get(), &format, nullptr))
        << grants << " allocations granted";
    EXPECT_EQ(nullptr, format) << grants << " allocations granted";
    EXPECT_EQ(grants, allocator.granted());
  }

  // And the third grant is enough for it to succeed, which is what says the
  // count above is the real one rather than an accident of the loop bound.
  {
    gchrontest::FailingAllocator allocator(3);
    GCHRON_Format * format = nullptr;
    EXPECT_EQ(GCHRON_OK,
        gchron_format_compile("'x'", 3, GCHRON_FORMAT_LDML, nullptr,
            allocator.get(), &format, nullptr));
    ASSERT_NE(nullptr, format);
    gchron_format_destroy(format);
  }
}

/*
 * The same sweep over a pattern that reallocates both pools several times, so
 * that the refusal lands in the middle of a structure that already holds
 * something. Whatever was built by then has to be released: the suite runs
 * under Valgrind, which is what actually checks that claim, and these were
 * the only paths through the compiler's cleanup that nothing had run.
 */
TEST(Format, ARefusalPartWayThroughAGrowingCompileLeavesNothingBehind) {
  GCHRON_OffsetDateTime odt = sample();
  std::string pattern;
  std::string expected;
  for (int i = 0; i < 40; ++i) {
    pattern += "'aaaa'y";
    expected += "aaaa2026";
  }

  int succeeded = 0;
  for (int grants = 0; grants < 16; ++grants) {
    gchrontest::FailingAllocator allocator(grants);
    GCHRON_Format * format = nullptr;
    GCHRON_Result result = gchron_format_compile(pattern.c_str(),
        pattern.size(), GCHRON_FORMAT_LDML, nullptr, allocator.get(),
        &format, nullptr);

    if (result == GCHRON_OK) {
      succeeded += 1;
      gchron_format_destroy(format);
    }
    else {
      EXPECT_EQ(GCHRON_ERR_OOM, result) << grants << " allocations granted";
      EXPECT_EQ(nullptr, format) << grants << " allocations granted";
    }
  }
  // Somewhere in that range the allocator stopped being the limit, or the
  // sweep never reached the growth it was written for.
  EXPECT_GT(succeeded, 0);

  EXPECT_EQ(expected, with_long_pattern(pattern.c_str(), odt));
}
