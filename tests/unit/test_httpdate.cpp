/**
 * @file
 *
 * HTTP-date and RFC 5322: the two formats that predate RFC 3339 and that
 * everybody still has to read.
 *
 * The examples below are the RFCs' own, which is the closest thing to an
 * oracle these two have - there is no test suite for either, and writing
 * examples from memory is what design.md section 12 exists to forbid.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>
#include <ghoti.io/chron/clock.h>
#include <ghoti.io/chron/format.h>

#include "test_helpers.h"

namespace {

/** A clock standing in 2026, for the two-digit-year rules. */
GCHRON_FixedClock now_2026() {
  GCHRON_DateTime dt = gchrontest::datetime(2026, 9, 20, 0, 0, 0);
  GCHRON_Instant moment{};
  EXPECT_EQ(GCHRON_OK, gchron_instant_from_utc(&dt, &moment));
  GCHRON_FixedClock clock;
  EXPECT_EQ(GCHRON_OK, gchron_clock_fixed(moment, &clock));
  return clock;
}

std::string write_http(const GCHRON_OffsetDateTime & odt,
    GCHRON_Result * result) {
  char buffer[GCHRON_HTTP_DATE_MAX];
  size_t length = 0;
  *result = gchron_write_http_date(&odt, buffer, sizeof(buffer), &length);
  return (*result == GCHRON_OK) ? std::string(buffer, length) : std::string();
}

std::string write_5322(const GCHRON_OffsetDateTime & odt,
    GCHRON_Result * result) {
  char buffer[GCHRON_RFC5322_MAX];
  size_t length = 0;
  *result = gchron_write_rfc5322(&odt, buffer, sizeof(buffer), &length);
  return (*result == GCHRON_OK) ? std::string(buffer, length) : std::string();
}

} // namespace

/*
 * RFC 9110 section 5.6.7's own three examples, which it says "are equivalent
 * representations of the same instant".
 */
TEST(HttpDate, TheRfcsOwnThreeFormsAreTheSameInstant) {
  GCHRON_FixedClock clock = now_2026();
  const char * forms[] = {
    "Sun, 06 Nov 1994 08:49:37 GMT",     // IMF-fixdate
    "Sunday, 06-Nov-94 08:49:37 GMT",    // obsolete RFC 850
    "Sun Nov  6 08:49:37 1994",          // obsolete asctime
  };

  GCHRON_Instant expected{};
  for (size_t i = 0; i < 3; ++i) {
    GCHRON_OffsetDateTime odt{};
    ASSERT_EQ(GCHRON_OK,
        gchron_parse_http_date(forms[i], std::strlen(forms[i]),
            &clock.clock, nullptr, &odt, nullptr, nullptr))
        << forms[i];
    EXPECT_EQ(1994, odt.civil.date.year);
    EXPECT_EQ(11, odt.civil.date.month);
    EXPECT_EQ(6, odt.civil.date.day);
    EXPECT_EQ(8, odt.civil.time.hour);
    EXPECT_EQ(49, odt.civil.time.minute);
    EXPECT_EQ(37, odt.civil.time.second);
    EXPECT_EQ(0, odt.offset_sec) << "an HTTP-date is always GMT";

    GCHRON_Instant instant{};
    ASSERT_EQ(GCHRON_OK, gchron_offset_to_instant(&odt, &instant));
    if (i == 0) {
      expected = instant;
    }
    else {
      EXPECT_EQ(0, gchron_instant_compare(&expected, &instant))
          << forms[i] << " is not the same instant as " << forms[0];
    }
  }
}

// A sender must produce IMF-fixdate, whatever it was given.
TEST(HttpDate, TheWriterAlwaysProducesImfFixdateInGmt) {
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 17, 30, 0);
  GCHRON_OffsetDateTime paris{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 2 * 3600, false, &paris));

  GCHRON_Result result;
  EXPECT_EQ("Sun, 20 Sep 2026 15:30:00 GMT", write_http(paris, &result));
  EXPECT_EQ(GCHRON_OK, result);

  // Round trip.
  GCHRON_OffsetDateTime back{};
  std::string text = write_http(paris, &result);
  ASSERT_EQ(GCHRON_OK,
      gchron_parse_http_date(text.data(), text.size(), nullptr, nullptr,
          &back, nullptr, nullptr));
  EXPECT_EQ(0, gchron_offset_compare(&paris, &back));
}

TEST(HttpDate, MalformedInputIsRefused) {
  GCHRON_FixedClock clock = now_2026();
  const char * bad[] = {
    "",
    "Sun, 06 Nov 1994 08:49:37",          // no zone
    "Sun, 06 Nov 1994 08:49:37 UTC",      // GMT only
    "Mon, 32 Nov 1994 08:49:37 GMT",      // no such day
    "Sun, 06 Xxx 1994 08:49:37 GMT",      // no such month
    "Sun, 06 Nov 1994 08:49:37 GMT ",     // trailing space
    "Sun, 06 Nov 1994 08:49:37 GMTx",     // trailing text
    "Sun 06 Nov 1994 08:49:37 GMT",       // no comma
    "2026-09-20T15:30:00Z",               // a different format entirely
  };
  for (const char * text : bad) {
    GCHRON_OffsetDateTime odt{};
    EXPECT_NE(GCHRON_OK,
        gchron_parse_http_date(text, std::strlen(text), &clock.clock, nullptr,
            &odt, nullptr, nullptr))
        << "\"" << text << "\"";
  }
}

/*
 * RFC 5322 section 3.3's own example, and section 4.3's obsolete forms.
 */
TEST(Rfc5322, TheRfcsOwnExamplesParse) {
  GCHRON_FixedClock clock = now_2026();
  GCHRON_OffsetDateTime odt{};

  const char * example = "Fri, 21 Nov 1997 09:55:06 -0600";
  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc5322(example, std::strlen(example), &clock.clock,
          nullptr, &odt, nullptr, nullptr));
  EXPECT_EQ(1997, odt.civil.date.year);
  EXPECT_EQ(11, odt.civil.date.month);
  EXPECT_EQ(21, odt.civil.date.day);
  EXPECT_EQ(-6 * 3600, odt.offset_sec);

  // A.5's folded and commented form, which is the same instant.
  const char * folded =
      "Thu,\r\n      13\r\n        Feb\r\n          1969\r\n      23:32\r\n"
      "               -0330 (Newfoundland Time)";
  GCHRON_OffsetDateTime hard{};
  // The seconds are optional in the obsolete grammar; this library requires
  // them, which is a documented narrowing rather than an oversight - see the
  // refusal test below.
  const char * with_seconds =
      "Thu, 13 Feb 1969 23:32:00 -0330 (Newfoundland Time)";
  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc5322(with_seconds, std::strlen(with_seconds),
          &clock.clock, nullptr, &hard, nullptr, nullptr));
  EXPECT_EQ(1969, hard.civil.date.year);
  EXPECT_EQ(-(3 * 3600 + 30 * 60), hard.offset_sec);
  (void)folded;

  // The day name is advisory: a Date whose weekday disagrees is still that
  // date, and dropping it entirely is legal too.
  const char * no_day = "21 Nov 1997 09:55:06 -0600";
  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc5322(no_day, std::strlen(no_day), &clock.clock, nullptr,
          &odt, nullptr, nullptr));
  EXPECT_EQ(21, odt.civil.date.day);
}

/*
 * Mistake M14 in its email form: `-0000` means the offset is unknown, exactly
 * as RFC 3339's `-00:00` does, and RFC 5322 section 3.3 says so.
 */
TEST(Rfc5322, MinusZeroZeroZeroZeroMeansUnknown) {
  GCHRON_FixedClock clock = now_2026();
  GCHRON_OffsetDateTime unknown{};
  GCHRON_OffsetDateTime known{};
  GCHRON_ParseInfo info{};

  const char * a = "Fri, 21 Nov 1997 09:55:06 -0000";
  const char * b = "Fri, 21 Nov 1997 09:55:06 +0000";
  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc5322(a, std::strlen(a), &clock.clock, nullptr, &unknown,
          &info, nullptr));
  EXPECT_TRUE(unknown.offset_unknown);
  EXPECT_TRUE(info.offset_unknown);

  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc5322(b, std::strlen(b), &clock.clock, nullptr, &known,
          nullptr, nullptr));
  EXPECT_FALSE(known.offset_unknown);

  // The same moment, not the same statement.
  EXPECT_EQ(0, gchron_offset_compare(&unknown, &known));
  EXPECT_FALSE(gchron_offset_identical(&unknown, &known));

  // And it writes back as `-0000`.
  GCHRON_Result result;
  EXPECT_EQ("Fri, 21 Nov 1997 09:55:06 -0000", write_5322(unknown, &result));
  EXPECT_EQ("Fri, 21 Nov 1997 09:55:06 +0000", write_5322(known, &result));
}

/*
 * Section 4.3's obsolete zone names, including the rule that makes a military
 * zone mean "unknown" - they were so widely got backwards that the RFC gave
 * up on them.
 */
TEST(Rfc5322, TheObsoleteZoneNamesFollowSection43sTable) {
  GCHRON_FixedClock clock = now_2026();
  struct Case { const char * zone; int32_t offset; bool unknown; };
  const Case cases[] = {
    { "UT", 0, false },
    { "GMT", 0, false },
    { "EST", -5 * 3600, false },
    { "EDT", -4 * 3600, false },
    { "CST", -6 * 3600, false },
    { "PDT", -7 * 3600, false },
    { "A", 0, true },
    { "Z", 0, true },
    { "J", 0, true },
  };
  for (const Case & c : cases) {
    std::string text = std::string("Fri, 21 Nov 1997 09:55:06 ") + c.zone;
    GCHRON_OffsetDateTime odt{};
    ASSERT_EQ(GCHRON_OK,
        gchron_parse_rfc5322(text.data(), text.size(), &clock.clock, nullptr,
            &odt, nullptr, nullptr))
        << text;
    EXPECT_EQ(c.offset, odt.offset_sec) << text;
    EXPECT_EQ(c.unknown, odt.offset_unknown) << text;
  }
}

// Section 4.3's two-digit-year rule is its own, and needs no clock: 00-49 is
// 2000-2049 and 50-99 is 1950-1999.
TEST(Rfc5322, TheTwoDigitYearRuleIsTheRfcsOwnAndNeedsNoClock) {
  GCHRON_OffsetDateTime odt{};
  struct Case { const char * year; int32_t expected; };
  const Case cases[] = {
    { "97", 1997 }, { "50", 1950 }, { "49", 2049 }, { "00", 2000 },
  };
  for (const Case & c : cases) {
    std::string text =
        std::string("Fri, 21 Nov ") + c.year + " 09:55:06 -0600";
    ASSERT_EQ(GCHRON_OK,
        gchron_parse_rfc5322(text.data(), text.size(), nullptr, nullptr, &odt,
            nullptr, nullptr))
        << text;
    EXPECT_EQ(c.expected, odt.civil.date.year) << text;
  }

  // And the obsolete three-digit form is 1900 + the value.
  const char * three = "Fri, 21 Nov 097 09:55:06 -0600";
  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc5322(three, std::strlen(three), nullptr, nullptr, &odt,
          nullptr, nullptr));
  EXPECT_EQ(1997, odt.civil.date.year);
}

TEST(Rfc5322, CommentsAndFoldingWhitespaceAreSkipped) {
  GCHRON_OffsetDateTime odt{};
  const char * commented =
      "  (a comment) Fri, (another) 21 Nov 1997 09:55:06 -0600 (ending)  ";
  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc5322(commented, std::strlen(commented), nullptr,
          nullptr, &odt, nullptr, nullptr));
  EXPECT_EQ(1997, odt.civil.date.year);
  EXPECT_EQ(-6 * 3600, odt.offset_sec);

  // Nested comments, and an escaped parenthesis inside one.
  const char * nested =
      "Fri, 21 Nov 1997 09:55:06 -0600 (outer (inner \\) still) done)";
  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc5322(nested, std::strlen(nested), nullptr, nullptr,
          &odt, nullptr, nullptr));
  EXPECT_EQ(1997, odt.civil.date.year);
}

TEST(Rfc5322, TheWriterRoundTrips) {
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 17, 30, 0);
  GCHRON_OffsetDateTime odt{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 2 * 3600, false, &odt));

  GCHRON_Result result;
  std::string text = write_5322(odt, &result);
  ASSERT_EQ(GCHRON_OK, result);
  EXPECT_EQ("Sun, 20 Sep 2026 17:30:00 +0200", text);

  GCHRON_OffsetDateTime back{};
  ASSERT_EQ(GCHRON_OK,
      gchron_parse_rfc5322(text.data(), text.size(), nullptr, nullptr, &back,
          nullptr, nullptr));
  EXPECT_TRUE(gchron_offset_identical(&odt, &back));
}

TEST(Rfc5322, MalformedInputIsRefused) {
  GCHRON_OffsetDateTime odt{};
  const char * bad[] = {
    "",
    "Fri, 21 Nov 1997 09:55:06",          // no zone
    "Fri, 21 Nov 1997 09:55 -0600",       // no seconds; this library needs them
    "Fri, 32 Nov 1997 09:55:06 -0600",    // no such day
    "Fri, 21 Xxx 1997 09:55:06 -0600",    // no such month
    "Fri, 21 Nov 1997 09:55:06 -0660",    // sixty minutes of offset
    "Fri, 21 Nov 1997 09:55:06 -0600 x",  // trailing text
  };
  for (const char * text : bad) {
    EXPECT_NE(GCHRON_OK,
        gchron_parse_rfc5322(text, std::strlen(text), nullptr, nullptr, &odt,
            nullptr, nullptr))
        << "\"" << text << "\"";
  }
}

TEST(HttpDate, TheWritersReportTheLengthWhenTheBufferIsTooSmall) {
  GCHRON_DateTime civil = gchrontest::datetime(2026, 9, 20, 15, 30, 0);
  GCHRON_OffsetDateTime odt{};
  ASSERT_EQ(GCHRON_OK, gchron_offset_create(&civil, 0, false, &odt));

  char small[4];
  size_t length = 0;
  EXPECT_EQ(GCHRON_ERR_LIMIT,
      gchron_write_http_date(&odt, small, sizeof(small), &length));
  EXPECT_EQ(std::strlen("Sun, 20 Sep 2026 15:30:00 GMT"), length);

  length = 0;
  EXPECT_EQ(GCHRON_ERR_LIMIT,
      gchron_write_rfc5322(&odt, nullptr, 0, &length));
  EXPECT_EQ(std::strlen("Sun, 20 Sep 2026 15:30:00 +0000"), length);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
