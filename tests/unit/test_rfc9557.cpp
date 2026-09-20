/**
 * @file
 *
 * RFC 9557, the format that keeps the zone name an RFC 3339 timestamp loses.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

class Ixdtf : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(GCHRON_OK, gchron_zonedb_system(nullptr, nullptr, &db_))
        << "no system zoneinfo directory";
  }
  void TearDown() override { gchron_zonedb_destroy(db_); }

  GCHRON_Result parse(const std::string & text, GCHRON_ZonedDateTime * out,
      const GCHRON_ParseOptions * opts = nullptr,
      GCHRON_ParseInfo * info = nullptr, GCHRON_Error * err = nullptr) {
    return gchron_parse_rfc9557(text.data(), text.size(), db_, opts, out,
        info, err);
  }

  std::string write(const GCHRON_ZonedDateTime & zoned,
      GCHRON_Result * result) {
    char buf[GCHRON_RFC9557_MAX];
    size_t len = 0;
    *result = gchron_write_rfc9557(&zoned, nullptr, buf, sizeof(buf), &len);
    return (*result == GCHRON_OK) ? std::string(buf, len) : std::string();
  }

  GCHRON_ZoneDb * db_ = nullptr;
};

} // namespace

TEST_F(Ixdtf, AZoneAnnotationSurvivesTheRoundTripAndAnOffsetDoesNot) {
  const char * text = "2026-09-20T17:30:00+02:00[Europe/Paris]";
  GCHRON_ZonedDateTime zoned{};
  GCHRON_ParseInfo info{};
  ASSERT_EQ(GCHRON_OK, parse(text, &zoned, nullptr, &info));

  EXPECT_TRUE(info.had_zone_annotation);
  EXPECT_STREQ("Europe/Paris", gchron_zone_id(zoned.zone));

  GCHRON_Result result;
  EXPECT_EQ(std::string(text), write(zoned, &result));
  EXPECT_EQ(GCHRON_OK, result);

  // Mistake M13, stated as a contrast: the same moment written as RFC 3339
  // keeps the offset and loses the name, so next summer's offset is no longer
  // derivable from it.
  GCHRON_OffsetDateTime as_offset{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_to_offset(&zoned, &as_offset));
  char rfc3339[GCHRON_RFC3339_DATE_TIME_MAX];
  size_t len = 0;
  ASSERT_EQ(GCHRON_OK,
      gchron_write_rfc3339_date_time(&as_offset, nullptr, rfc3339,
          sizeof(rfc3339), &len));
  EXPECT_STREQ("2026-09-20T17:30:00+02:00", rfc3339);
  EXPECT_EQ(nullptr, std::strstr(rfc3339, "Paris"));
}

TEST_F(Ixdtf, ATimestampWithNoAnnotationIsJustRfc3339) {
  GCHRON_ZonedDateTime zoned{};
  GCHRON_ParseInfo info{};
  ASSERT_EQ(GCHRON_OK, parse("2026-09-20T15:30:00Z", &zoned, nullptr, &info));
  EXPECT_FALSE(info.had_zone_annotation);
  EXPECT_TRUE(gchron_zone_is_fixed(zoned.zone));
  EXPECT_EQ(0, zoned.offset_sec);
}

TEST_F(Ixdtf, AnOffsetAnnotationBecomesAFixedZone) {
  GCHRON_ZonedDateTime zoned{};
  ASSERT_EQ(GCHRON_OK, parse("2026-09-20T10:30:00-05:00[-05:00]", &zoned));
  EXPECT_TRUE(gchron_zone_is_fixed(zoned.zone));
  EXPECT_EQ(-5 * 3600, zoned.offset_sec);
}

/*
 * The offset and the zone contradict each other. Which half to believe is the
 * application's decision, and the zero value refuses so that nobody makes it
 * by accident.
 */
TEST_F(Ixdtf, AnOffsetThatDisagreesWithTheZoneIsAPolicy) {
  // New York is on -05:00 that day, not -08:00.
  const char * text = "2026-03-08T01:30:00-08:00[America/New_York]";
  GCHRON_ZonedDateTime zoned{};
  GCHRON_ParseInfo info{};
  GCHRON_Error err{};
  GCHRON_ParseOptions opts;

  gchron_parse_options_default(&opts);
  EXPECT_EQ(GCHRON_ERR_FORMAT, parse(text, &zoned, &opts, &info, &err));
  EXPECT_EQ(GCHRON_DIAG_OFFSET_ZONE_CONFLICT, err.diag);

  // Believe the offset: the instant is what the offset named.
  opts.zone_conflict = GCHRON_ZONECONFLICT_PREFER_OFFSET;
  ASSERT_EQ(GCHRON_OK, parse(text, &zoned, &opts, &info));
  EXPECT_TRUE(info.offset_disagreed_with_zone)
      << "the evidence that the text contradicted itself is kept";
  GCHRON_OffsetDateTime as_utc{};
  ASSERT_EQ(GCHRON_OK,
      gchron_offset_from_instant(&zoned.instant, 0, false, &as_utc));
  EXPECT_EQ(9, as_utc.civil.time.hour) << "01:30-08:00 is 09:30Z";
  EXPECT_EQ(30, as_utc.civil.time.minute);

  // Believe the zone: the civil reading is re-resolved through it.
  opts.zone_conflict = GCHRON_ZONECONFLICT_PREFER_ZONE;
  ASSERT_EQ(GCHRON_OK, parse(text, &zoned, &opts, &info));
  EXPECT_TRUE(info.offset_disagreed_with_zone);
  ASSERT_EQ(GCHRON_OK,
      gchron_offset_from_instant(&zoned.instant, 0, false, &as_utc));
  EXPECT_EQ(6, as_utc.civil.time.hour) << "01:30-05:00 is 06:30Z";
}

TEST_F(Ixdtf, AnAgreeingOffsetAndZoneNeedNoPolicy) {
  GCHRON_ZonedDateTime zoned{};
  GCHRON_ParseInfo info{};
  // The strict default, which refuses a disagreement, accepts agreement.
  ASSERT_EQ(GCHRON_OK,
      parse("2026-03-08T01:30:00-05:00[America/New_York]", &zoned, nullptr,
          &info));
  EXPECT_FALSE(info.offset_disagreed_with_zone);
}

/*
 * The `!` flag is the whole point of RFC 9557's criticality: the writer is
 * saying that ignoring this annotation would change what the timestamp means,
 * so a reader that does not understand it must refuse rather than proceed.
 */
TEST_F(Ixdtf, ACriticalAnnotationThisLibraryDoesNotUnderstandIsRefused) {
  GCHRON_ZonedDateTime zoned{};
  GCHRON_Error err{};
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      parse("2026-09-20T15:30:00Z[!u-unknown=whatever]", &zoned, nullptr,
          nullptr, &err));
  EXPECT_EQ(GCHRON_DIAG_ANNOTATION_CRITICAL, err.diag);

  // The same annotation without the flag is ignorable, and is ignored.
  EXPECT_EQ(GCHRON_OK,
      parse("2026-09-20T15:30:00Z[u-unknown=whatever]", &zoned));
}

TEST_F(Ixdtf, TheCalendarAnnotationIsReportedRatherThanActedOn) {
  GCHRON_ZonedDateTime zoned{};
  GCHRON_ParseInfo info{};
  ASSERT_EQ(GCHRON_OK,
      parse("2026-09-20T15:30:00Z[u-ca=julian]", &zoned, nullptr, &info));
  // Copied into the info, so it outlives the input it was parsed out of -
  // which the `parse` helper above binds to a temporary, and which an earlier
  // borrowing version of this field dangled on.
  EXPECT_STREQ("julian", info.calendar);
  // The other calendars are phase 2; what phase 1 owes is to carry the
  // annotation rather than silently drop it.
}

TEST_F(Ixdtf, ACalendarIdentifierTooLongToNameACalendarIsRefused) {
  GCHRON_ZonedDateTime zoned{};
  std::string too_long = "2026-09-20T15:30:00Z[u-ca="
      + std::string(GCHRON_CALENDAR_ID_MAX + 1, 'a') + "]";
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, parse(too_long, &zoned));

  std::string just_fits = "2026-09-20T15:30:00Z[u-ca="
      + std::string(GCHRON_CALENDAR_ID_MAX, 'a') + "]";
  GCHRON_ParseInfo info{};
  ASSERT_EQ(GCHRON_OK, parse(just_fits, &zoned, nullptr, &info));
  EXPECT_EQ(GCHRON_CALENDAR_ID_MAX, std::strlen(info.calendar));
}

TEST_F(Ixdtf, TheGrammarsEdgesAreRefusedDeliberately) {
  GCHRON_ZonedDateTime zoned{};
  struct Case { const char * text; const char * why; };
  const Case bad[] = {
    { "2026-09-20T15:30:00Z[", "an unterminated annotation" },
    { "2026-09-20T15:30:00Z[]", "an empty annotation" },
    { "2026-09-20T17:30:00+02:00[Europe/Paris", "no closing bracket" },
    { "2026-09-20T17:30:00+02:00[Europe/Paris][Europe/London]",
      "a second zone annotation" },
    { "2026-09-20T15:30:00Z[Europe/Atlantis]", "a zone that does not exist" },
    { "2026-09-20T15:30:00Z[../../etc/passwd]", "a path, not a zone name" },
    { "2026-09-20T15:30:00Z[U-CA=julian]", "an uppercase suffix key" },
    { "2026-09-20T15:30:00Z[=julian]", "a key that is empty" },
    { "2026-09-20T15:30:00Z[u-ca=]", "a value that is empty" },
    { "2026-09-20T17:30:00+02:00[Europe/Paris]tail", "trailing text" },
    { "2026-09-20T17:30:00+02:00[Europe/Paris]\n", "a trailing newline" },
    { "2026-09-20T15:30:00Z[Europe/Paris]",
      "Paris is +02:00 in September, so the two halves contradict" },
  };
  for (const Case & c : bad) {
    EXPECT_NE(GCHRON_OK, parse(c.text, &zoned)) << c.text << ": " << c.why;
  }

  // Each of these carries the offset the zone was actually on, because the
  // strict default refuses a timestamp whose two halves contradict each
  // other - Paris is +02:00 in September, so `15:30:00Z[Europe/Paris]` is a
  // conflict and belongs in the list above, not this one.
  const char * good[] = {
    "2026-09-20T17:30:00+02:00[Europe/Paris]",
    "2026-09-20T17:30:00+02:00[Europe/Paris][u-ca=iso8601]",
    "2026-09-20T17:30:00+02:00[!Europe/Paris]",
    "2026-01-20T16:30:00+01:00[Europe/Paris]",
    "2026-09-20T15:30:00Z[u-ca=iso8601]",
    "2026-09-20T15:30:00Z[Etc/UTC]",
    "2026-09-20T15:30:00Z[Z]",
  };
  for (const char * text : good) {
    EXPECT_EQ(GCHRON_OK, parse(text, &zoned)) << text;
  }
}

// Without a database there is nothing to resolve a name against, and
// inventing a fixed-offset zone from the timestamp's own offset would throw
// away the very thing the annotation exists to carry.
TEST_F(Ixdtf, AZoneNameWithNoDatabaseIsRefusedRatherThanApproximated) {
  const char * text = "2026-09-20T17:30:00+02:00[Europe/Paris]";
  GCHRON_ZonedDateTime zoned{};
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_parse_rfc9557(text, std::strlen(text), nullptr, nullptr, &zoned,
          nullptr, nullptr));
}

// An anonymous zone has no name to annotate, so the output is plain RFC 3339
// rather than an invented one.
TEST_F(Ixdtf, AnAnonymousZoneWritesPlainRfc3339) {
  const GCHRON_Zone * rule = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_zonedb_posix(db_, "EST5EDT,M3.2.0,M11.1.0", &rule));
  EXPECT_EQ(nullptr, gchron_zone_id(rule));

  GCHRON_Instant moment{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1781539200, 0, &moment));
  GCHRON_ZonedDateTime zoned{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_from_instant(moment, rule, &zoned));

  GCHRON_Result result;
  std::string text = write(zoned, &result);
  ASSERT_EQ(GCHRON_OK, result);
  EXPECT_EQ("2026-06-15T12:00:00-04:00", text);
  EXPECT_EQ(nullptr, std::strchr(text.c_str(), '['));
}

// Property 3 of design.md section 12.1, for this format.
TEST_F(Ixdtf, ParseOfWriteIsTheIdentityForEveryNamedZone) {
  const char * const ids[] = {
    "Europe/Paris", "America/New_York", "Asia/Kathmandu", "Pacific/Chatham",
    "Australia/Lord_Howe", "Europe/Dublin", "America/St_Johns",
  };
  const int64_t moments[] = {
    0, 1541300400, 1781539200, 1793511000, 2145916800, 4102444800,
  };
  for (const char * id : ids) {
    const GCHRON_Zone * z = nullptr;
    ASSERT_EQ(GCHRON_OK, gchron_zonedb_zone(db_, id, &z)) << id;
    for (int64_t seconds : moments) {
      GCHRON_Instant moment{};
      ASSERT_EQ(GCHRON_OK, gchron_instant_create(seconds, 0, &moment));
      GCHRON_ZonedDateTime zoned{};
      ASSERT_EQ(GCHRON_OK, gchron_zoned_from_instant(moment, z, &zoned));

      GCHRON_Result result;
      std::string text = write(zoned, &result);
      ASSERT_EQ(GCHRON_OK, result) << id << " at " << seconds;

      GCHRON_ZonedDateTime back{};
      ASSERT_EQ(GCHRON_OK, parse(text, &back)) << text;
      EXPECT_TRUE(gchron_zoned_identical(&zoned, &back)) << text;
    }
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
