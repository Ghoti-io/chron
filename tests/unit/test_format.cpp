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
