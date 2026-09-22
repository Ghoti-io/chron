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

// Mistake M14 again, on the way out through a pattern.
TEST(Format, AnUnknownOffsetPrintsAsMinusZeroZero) {
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 15, 30, 0);
  GCHRON_OffsetDateTime unknown{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, true, &unknown));
  EXPECT_EQ("-00:00", with_pattern("XXX", unknown));
  EXPECT_EQ("-0000", with_pattern("Z", unknown));

  GCHRON_OffsetDateTime known{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, false, &known));
  EXPECT_EQ("Z", with_pattern("XXX", known));
  EXPECT_EQ("+0000", with_pattern("Z", known));
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

  // Out of the enum is NULL rather than a wild read.
  EXPECT_EQ(nullptr, gchron_format_named(GCHRON_NAMED_COUNT));
  EXPECT_EQ(nullptr,
      gchron_format_named(static_cast<GCHRON_NamedFormat>(-1)));
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
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_system(nullptr, nullptr, &db));
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
