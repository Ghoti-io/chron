/**
 * @file
 *
 * Zones, and the two ways a wall-clock reading can fail to name an instant.
 *
 * The zdump differential in tests/conformance checks the offset at every
 * transition of twenty zones. What is here is the behaviour no transition
 * table states directly: what happens when a caller asks about a reading that
 * never occurred, or occurred twice, and what each GCHRON_Resolve does about
 * it.
 *
 * Every expected value below came from Python's `zoneinfo` reading the same
 * TZif files - never from anybody's recollection of what a zone does, which
 * design.md section 12 names as the thing under test rather than the
 * authority.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

class Zones : public ::testing::Test {
protected:
  void SetUp() override {
    // Not a skip: a machine with no zoneinfo cannot check any of this, and a
    // suite that passed anyway would be measuring nothing.
    ASSERT_EQ(GCHRON_OK, gchron_zonedb_system(nullptr, nullptr, &db_))
        << "no system zoneinfo directory";
  }
  void TearDown() override { gchron_zonedb_destroy(db_); }

  const GCHRON_Zone * zone(const char * id) {
    const GCHRON_Zone * z = nullptr;
    EXPECT_EQ(GCHRON_OK, gchron_zonedb_zone(db_, id, &z)) << id;
    return z;
  }

  GCHRON_ZoneDb * db_ = nullptr;
};

} // namespace

TEST_F(Zones, ADatabaseSaysWhereItsDataCameFromAndWhichRelease) {
  EXPECT_EQ(GCHRON_ZONE_SOURCE_SYSTEM, gchron_zonedb_source(db_));
  // "Which rules produced this timestamp" is an audit question, and a library
  // that cannot answer has made the dispute unresolvable (section 6.6).
  const char * version = gchron_zonedb_version(db_);
  ASSERT_NE(nullptr, version);
  EXPECT_GE(std::strlen(version), 5u) << "a tzdata release is like 2026c";
}

TEST_F(Zones, ZonesAreCachedAndIdentityIsPointerIdentity) {
  const GCHRON_Zone * a = zone("Europe/Paris");
  const GCHRON_Zone * b = zone("Europe/Paris");
  EXPECT_EQ(a, b) << "a database hands out one zone per identifier";
  EXPECT_STREQ("Europe/Paris", gchron_zone_id(a));
}

// Mistake M12: `CST` is China, Cuba and Central, and `IST` is India, Israel
// and Ireland. Abbreviations are output-only, and no lookup takes one.
TEST_F(Zones, AnAbbreviationIsNotAnIdentifier) {
  const GCHRON_Zone * z = nullptr;
  EXPECT_NE(GCHRON_OK, gchron_zonedb_zone(db_, "CST", &z));
  EXPECT_NE(GCHRON_OK, gchron_zonedb_zone(db_, "IST", &z));
  EXPECT_NE(GCHRON_OK, gchron_zonedb_zone(db_, "PDT", &z));
}

// A zone identifier reaches this library from a document - RFC 9557 puts one
// inside a timestamp - and is about to become a path.
TEST_F(Zones, AnIdentifierThatCouldEscapeTheDirectoryIsRefused) {
  const GCHRON_Zone * z = nullptr;
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_zonedb_zone(db_, "../../etc/passwd", &z));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_zonedb_zone(db_, "/etc/passwd", &z));
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_zonedb_zone(db_, "Europe/../../etc/passwd", &z));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_zonedb_zone(db_, "Europe//Paris", &z));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_zonedb_zone(db_, "Europe/Paris/", &z));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_zonedb_zone(db_, "", &z));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_zonedb_zone(db_, "Europe\\Paris", &z));
  // A well-formed identifier that simply is not there is a different answer.
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_zonedb_zone(db_, "Europe/Atlantis", &z));
}

/*
 * A backward-compatibility link is an ordinary file in the directory - `zic`
 * writes it as a copy or a hard link - so it loads like any other zone and
 * answers the same questions as what it links to.
 *
 * Whether any are *installed* is the distribution's decision, not this
 * library's: Debian moved them to a separate `tzdata-legacy` package, so this
 * machine has 486 zones and no `US/Eastern`. The test therefore checks the
 * behaviour on whichever links the database actually has, and **says how many
 * it found** - because design.md section 12 asks that skips be counted rather
 * than silent, and a test that quietly checked nothing would look identical
 * to one that passed.
 */
TEST_F(Zones, BackwardCompatibilityLinksResolveWhereTheyAreInstalled) {
  struct Link { const char * link; const char * canonical; };
  const Link links[] = {
    { "US/Eastern", "America/New_York" },
    { "Asia/Calcutta", "Asia/Kolkata" },
    { "Europe/Kiev", "Europe/Kyiv" },
    { "Australia/Canberra", "Australia/Sydney" },
    { "GMT", "Etc/GMT" },
  };
  GCHRON_Instant midsummer{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1781539200, 0, &midsummer));

  size_t checked = 0;
  for (const Link & entry : links) {
    const GCHRON_Zone * via_link = nullptr;
    const GCHRON_Zone * via_name = nullptr;
    if (gchron_zonedb_zone(db_, entry.link, &via_link) != GCHRON_OK) {
      continue;
    }
    ASSERT_EQ(GCHRON_OK,
        gchron_zonedb_zone(db_, entry.canonical, &via_name))
        << entry.link << " is installed but " << entry.canonical << " is not";

    GCHRON_ZoneInfo a{};
    GCHRON_ZoneInfo b{};
    ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(via_link, midsummer, &a));
    ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(via_name, midsummer, &b));
    EXPECT_EQ(b.offset_sec, a.offset_sec) << entry.link;
    EXPECT_STREQ(b.abbreviation, a.abbreviation) << entry.link;
    // The identifier it was asked for is the identifier it reports.
    EXPECT_STREQ(entry.link, gchron_zone_id(via_link));
    ++checked;
  }
  std::printf("[          ] %zu of %zu backward links installed here\n",
      checked, sizeof(links) / sizeof(links[0]));
}

// A fixed offset is a zone too, so that every code path is one code path.
TEST_F(Zones, AFixedOffsetIsAZone) {
  const GCHRON_Zone * utc = nullptr;
  const GCHRON_Zone * plus_530 = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_utc(db_, &utc));
  ASSERT_EQ(GCHRON_OK,
      gchron_zonedb_fixed(db_, 5 * 3600 + 30 * 60, &plus_530));

  EXPECT_TRUE(gchron_zone_is_fixed(utc));
  EXPECT_TRUE(gchron_zone_is_fixed(plus_530));

  GCHRON_Instant now{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1781539200, 0, &now));
  GCHRON_ZoneInfo info{};
  ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(plus_530, now, &info));
  EXPECT_EQ(5 * 3600 + 30 * 60, info.offset_sec);
  EXPECT_FALSE(info.is_dst);
  EXPECT_STREQ("+0530", info.abbreviation);

  // It never changes, and saying so is an answer rather than an error in the
  // data.
  GCHRON_Transition transition{};
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_zone_next_transition(plus_530, now, &transition));

  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_zonedb_fixed(db_, 86400, &plus_530));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_zonedb_fixed(db_, -86400, &plus_530));
}

/*
 * The gap. On 8 March 2026 New York's clocks go from 01:59:59 to 03:00:00, so
 * 02:30 never happens - and mistake M1 is that a library which has one type
 * for both kinds of time cannot say so.
 */
TEST_F(Zones, AReadingInAGapNamesNoInstant) {
  const GCHRON_Zone * ny = zone("America/New_York");
  GCHRON_DateTime civil = gchrontest::datetime(2026, 3, 8, 2, 30, 0);
  GCHRON_CivilOffsets options{};

  // The primitive reports a gap as a count of zero and *not* as an error: a
  // calendar interface needs to show the ambiguity, not have it resolved out
  // of sight.
  ASSERT_EQ(GCHRON_OK, gchron_zone_offsets_for_civil(ny, civil, &options));
  EXPECT_EQ(0, options.count);
  EXPECT_EQ(3600, options.gap_seconds);

  GCHRON_ZonedDateTime zoned{};
  // The default refuses (design.md section 3.7), and on failure the out
  // parameter is not written.
  EXPECT_EQ(GCHRON_ERR_GAP,
      gchron_zoned_from_civil(civil, ny, GCHRON_RESOLVE_REJECT, &zoned));

  // EARLIER is the last instant before the jump; LATER the first after it.
  ASSERT_EQ(GCHRON_OK,
      gchron_zoned_from_civil(civil, ny, GCHRON_RESOLVE_LATER, &zoned));
  EXPECT_EQ(-4 * 3600, zoned.offset_sec);
  GCHRON_DateTime back{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_to_civil(&zoned, &back));
  EXPECT_EQ(3, back.time.hour);
  EXPECT_EQ(0, back.time.minute);

  ASSERT_EQ(GCHRON_OK,
      gchron_zoned_from_civil(civil, ny, GCHRON_RESOLVE_EARLIER, &zoned));
  ASSERT_EQ(GCHRON_OK, gchron_zoned_to_civil(&zoned, &back));
  EXPECT_EQ(1, back.time.hour);
  EXPECT_EQ(59, back.time.minute);
  EXPECT_EQ(59, back.time.second);

  // COMPATIBLE pushes forward by the length of the gap: 02:30 becomes 03:30.
  // This is what java.time and legacy JavaScript Date do, and it is what a
  // caller has to name rather than get by default.
  ASSERT_EQ(GCHRON_OK,
      gchron_zoned_from_civil(civil, ny, GCHRON_RESOLVE_COMPATIBLE, &zoned));
  ASSERT_EQ(GCHRON_OK, gchron_zoned_to_civil(&zoned, &back));
  EXPECT_EQ(3, back.time.hour);
  EXPECT_EQ(30, back.time.minute);
}

/*
 * The overlap. On 1 November 2026 New York's clocks go back, so 01:30 happens
 * twice: once on EDT and once on EST, one hour apart.
 */
TEST_F(Zones, AReadingInAnOverlapNamesTwoInstants) {
  const GCHRON_Zone * ny = zone("America/New_York");
  GCHRON_DateTime civil = gchrontest::datetime(2026, 11, 1, 1, 30, 0);
  GCHRON_CivilOffsets options{};

  ASSERT_EQ(GCHRON_OK, gchron_zone_offsets_for_civil(ny, civil, &options));
  ASSERT_EQ(2, options.count);
  // Earliest first. Both values are Python's zoneinfo's, not recollection.
  EXPECT_EQ(1793511000, options.instants[0].sec);
  EXPECT_EQ(1793514600, options.instants[1].sec);
  EXPECT_EQ(-4 * 3600, options.options[0].offset_sec);
  EXPECT_TRUE(options.options[0].is_dst);
  EXPECT_STREQ("EDT", options.options[0].abbreviation);
  EXPECT_EQ(-5 * 3600, options.options[1].offset_sec);
  EXPECT_FALSE(options.options[1].is_dst);
  EXPECT_STREQ("EST", options.options[1].abbreviation);

  GCHRON_ZonedDateTime zoned{};
  EXPECT_EQ(GCHRON_ERR_AMBIGUOUS,
      gchron_zoned_from_civil(civil, ny, GCHRON_RESOLVE_REJECT, &zoned));

  ASSERT_EQ(GCHRON_OK,
      gchron_zoned_from_civil(civil, ny, GCHRON_RESOLVE_EARLIER, &zoned));
  EXPECT_EQ(1793511000, zoned.instant.sec);
  ASSERT_EQ(GCHRON_OK,
      gchron_zoned_from_civil(civil, ny, GCHRON_RESOLVE_LATER, &zoned));
  EXPECT_EQ(1793514600, zoned.instant.sec);
  // Temporal's COMPATIBLE takes the first occurrence of an overlap.
  ASSERT_EQ(GCHRON_OK,
      gchron_zoned_from_civil(civil, ny, GCHRON_RESOLVE_COMPATIBLE, &zoned));
  EXPECT_EQ(1793511000, zoned.instant.sec);
}

TEST_F(Zones, AnOrdinaryReadingNamesExactlyOneInstant) {
  const GCHRON_Zone * ny = zone("America/New_York");
  GCHRON_DateTime civil = gchrontest::datetime(2026, 6, 15, 12, 0, 0);
  GCHRON_CivilOffsets options{};
  ASSERT_EQ(GCHRON_OK, gchron_zone_offsets_for_civil(ny, civil, &options));
  ASSERT_EQ(1, options.count);
  EXPECT_EQ(1781539200, options.instants[0].sec);

  // Every GCHRON_Resolve gives the same answer when there is nothing to
  // resolve, the refusing default included.
  for (int policy = 0; policy <= GCHRON_RESOLVE_COMPATIBLE; ++policy) {
    GCHRON_ZonedDateTime zoned{};
    ASSERT_EQ(GCHRON_OK,
        gchron_zoned_from_civil(civil, ny,
            static_cast<GCHRON_Resolve>(policy), &zoned))
        << "policy " << policy;
    EXPECT_EQ(1781539200, zoned.instant.sec) << "policy " << policy;
  }
}

/*
 * Mistake M11. On 4 November 2018 São Paulo's clocks went from 23:59:59
 * straight to 01:00:00, so midnight did not exist that day and
 * `setHours(0,0,0,0)` has no answer. The first instant of the day is
 * 01:00:00-02:00, which Python's zoneinfo confirms is Unix second
 * 1541300400.
 */
TEST_F(Zones, StartOfDayIsNotMidnightWhereMidnightDidNotHappen) {
  const GCHRON_Zone * sp = zone("America/Sao_Paulo");
  GCHRON_DateTime noon = gchrontest::datetime(2018, 11, 4, 12, 0, 0);
  GCHRON_ZonedDateTime zoned{};
  ASSERT_EQ(GCHRON_OK,
      gchron_zoned_from_civil(noon, sp, GCHRON_RESOLVE_REJECT, &zoned));

  GCHRON_ZonedDateTime start{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_start_of_day(&zoned, &start));
  EXPECT_EQ(1541300400, start.instant.sec);

  GCHRON_DateTime civil{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_to_civil(&start, &civil));
  EXPECT_EQ(4, civil.date.day);
  EXPECT_EQ(1, civil.time.hour) << "midnight did not exist that day";
  EXPECT_EQ(0, civil.time.minute);

  // And on an ordinary day it is midnight.
  GCHRON_DateTime ordinary = gchrontest::datetime(2018, 11, 6, 12, 0, 0);
  ASSERT_EQ(GCHRON_OK,
      gchron_zoned_from_civil(ordinary, sp, GCHRON_RESOLVE_REJECT, &zoned));
  ASSERT_EQ(GCHRON_OK, gchron_zoned_start_of_day(&zoned, &start));
  ASSERT_EQ(GCHRON_OK, gchron_zoned_to_civil(&start, &civil));
  EXPECT_EQ(0, civil.time.hour);
}

/*
 * Europe/Dublin runs Irish Standard Time in summer and marks *winter* as its
 * daylight-saving type, with a negative offset from standard. A caller that
 * inferred the offset from the flag gets Ireland backwards, which is why
 * GCHRON_ZoneInfo carries both.
 */
TEST_F(Zones, TheDaylightSavingFlagIsNotTheSignOfTheOffset) {
  const GCHRON_Zone * dublin = zone("Europe/Dublin");
  GCHRON_ZoneInfo winter{};
  GCHRON_ZoneInfo summer{};
  GCHRON_Instant january{};
  GCHRON_Instant july{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1768478400, 0, &january));
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1784113200, 0, &july));

  ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(dublin, january, &winter));
  ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(dublin, july, &summer));

  EXPECT_EQ(0, winter.offset_sec);
  EXPECT_STREQ("GMT", winter.abbreviation);
  EXPECT_EQ(3600, summer.offset_sec);
  EXPECT_STREQ("IST", summer.abbreviation);
  // The smaller offset is the one the zone calls daylight saving.
  EXPECT_TRUE(winter.is_dst);
  EXPECT_FALSE(summer.is_dst);
}

/*
 * Lord Howe Island shifts by thirty minutes rather than an hour, so a gap
 * there is half the length every other implementation's constant assumes.
 */
TEST_F(Zones, AGapNeedNotBeAnHourLong) {
  const GCHRON_Zone * lhi = zone("Australia/Lord_Howe");
  GCHRON_CivilOffsets options{};

  GCHRON_DateTime in_gap = gchrontest::datetime(2026, 10, 4, 2, 15, 0);
  ASSERT_EQ(GCHRON_OK, gchron_zone_offsets_for_civil(lhi, in_gap, &options));
  EXPECT_EQ(0, options.count);
  EXPECT_EQ(1800, options.gap_seconds) << "Lord Howe shifts by half an hour";

  GCHRON_DateTime in_overlap = gchrontest::datetime(2026, 4, 5, 1, 45, 0);
  ASSERT_EQ(GCHRON_OK,
      gchron_zone_offsets_for_civil(lhi, in_overlap, &options));
  ASSERT_EQ(2, options.count);
  EXPECT_EQ(1775313900, options.instants[0].sec);
  EXPECT_EQ(1775315700, options.instants[1].sec);
  EXPECT_EQ(1800, options.instants[1].sec - options.instants[0].sec);
}

/*
 * Samoa crossed the date line on 30 December 2011: the day did not happen at
 * all. A gap need not be an hour, and it need not be a day either - it is
 * whatever the two offsets differ by.
 */
TEST_F(Zones, AWholeDayCanBeMissing) {
  const GCHRON_Zone * apia = zone("Pacific/Apia");
  GCHRON_DateTime civil = gchrontest::datetime(2011, 12, 30, 12, 0, 0);
  GCHRON_CivilOffsets options{};
  ASSERT_EQ(GCHRON_OK, gchron_zone_offsets_for_civil(apia, civil, &options));
  EXPECT_EQ(0, options.count);
  EXPECT_EQ(86400, options.gap_seconds);

  GCHRON_ZonedDateTime zoned{};
  EXPECT_EQ(GCHRON_ERR_GAP,
      gchron_zoned_from_civil(civil, apia, GCHRON_RESOLVE_REJECT, &zoned));
}

// Mistake M16 again, for zoned values: two readings in different zones are
// the same moment, and are not the same value.
TEST_F(Zones, CompareOrdersByInstantAndIdenticalComparesTheZoneToo) {
  const GCHRON_Zone * ny = zone("America/New_York");
  const GCHRON_Zone * paris = zone("Europe/Paris");
  GCHRON_Instant moment{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1781539200, 0, &moment));

  GCHRON_ZonedDateTime a{};
  GCHRON_ZonedDateTime b{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_from_instant(moment, ny, &a));
  ASSERT_EQ(GCHRON_OK, gchron_zoned_from_instant(moment, paris, &b));

  EXPECT_EQ(0, gchron_zoned_compare(&a, &b));
  EXPECT_FALSE(gchron_zoned_identical(&a, &b));
  EXPECT_TRUE(gchron_zoned_identical(&a, &a));
  EXPECT_LT(gchron_zoned_compare(nullptr, &a), 0);
  EXPECT_EQ(0, gchron_zoned_compare(nullptr, nullptr));

  // Different readings, same moment.
  GCHRON_DateTime ca{};
  GCHRON_DateTime cb{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_to_civil(&a, &ca));
  ASSERT_EQ(GCHRON_OK, gchron_zoned_to_civil(&b, &cb));
  EXPECT_NE(0, gchron_datetime_compare(&ca, &cb));
}

// Section 3.5: adding twenty-four hours is not adding a day, and on the night
// the clocks change they give different readings. Exact units are all phase 1
// offers; the calendar-unit question is phase 2's.
TEST_F(Zones, TwentyFourHoursIsNotADayAcrossATransition) {
  const GCHRON_Zone * ny = zone("America/New_York");
  GCHRON_DateTime civil = gchrontest::datetime(2026, 3, 7, 12, 0, 0);
  GCHRON_ZonedDateTime before{};
  ASSERT_EQ(GCHRON_OK,
      gchron_zoned_from_civil(civil, ny, GCHRON_RESOLVE_REJECT, &before));

  GCHRON_Duration day{};
  day.hours = 24;
  GCHRON_ZonedDateTime after{};
  ASSERT_EQ(GCHRON_OK,
      gchron_zoned_add(&before, &day, GCHRON_RESOLVE_REJECT,
          GCHRON_OVERFLOW_REJECT, &after));

  GCHRON_DateTime reading{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_to_civil(&after, &reading));
  EXPECT_EQ(8, reading.date.day);
  // Twenty-four hours after noon on the 7th is 13:00 on the 8th, because the
  // clocks went forward in between.
  EXPECT_EQ(13, reading.time.hour);

  // And one *day* keeps the wall-clock time, skipping the hour the zone
  // skipped: noon on the 7th plus a day is noon on the 8th, twenty-three
  // hours later. This is the row of design.md section 4.1's table that
  // matters.
  GCHRON_Duration calendar_day{};
  calendar_day.days = 1;
  ASSERT_EQ(GCHRON_OK,
      gchron_zoned_add(&before, &calendar_day, GCHRON_RESOLVE_REJECT,
          GCHRON_OVERFLOW_REJECT, &after));
  ASSERT_EQ(GCHRON_OK, gchron_zoned_to_civil(&after, &reading));
  EXPECT_EQ(8, reading.date.day);
  EXPECT_EQ(12, reading.time.hour) << "the same wall-clock time";
  EXPECT_EQ(23 * 3600, after.instant.sec - before.instant.sec)
      << "twenty-three hours of elapsed time, not twenty-four";
}

TEST_F(Zones, TheZoneIsAskedAgainRatherThanTheCachedOffsetTrusted) {
  const GCHRON_Zone * ny = zone("America/New_York");
  GCHRON_Instant moment{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1781539200, 0, &moment));
  GCHRON_ZonedDateTime zoned{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_from_instant(moment, ny, &zoned));

  // What the zone itself says, asked directly.
  GCHRON_ZoneInfo truth{};
  ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(ny, moment, &truth));
  EXPECT_EQ(-4 * 3600, truth.offset_sec) << "mid-June in New York is EDT";

  GCHRON_DateTime honest{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_to_civil(&zoned, &honest));

  // A caller who edited the cached offset by hand still gets the zone's
  // answer: the cache is a convenience and never the authority (section 3.5).
  zoned.offset_sec = 12345;
  GCHRON_DateTime civil{};
  ASSERT_EQ(GCHRON_OK, gchron_zoned_to_civil(&zoned, &civil));
  EXPECT_EQ(0, gchron_datetime_compare(&honest, &civil));
  EXPECT_EQ(12, civil.time.hour) << "16:00 UTC is 12:00 EDT";
}

TEST_F(Zones, ListingADatabaseGivesEveryIdentifierSorted) {
  const char * const * ids = nullptr;
  size_t count = 0;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_list(db_, &ids, &count));
  ASSERT_GT(count, 300u) << "the tzdb has hundreds of zones";

  bool found_paris = false;
  for (size_t i = 0; i + 1 < count; ++i) {
    // Sorted, so that a listing is stable across filesystems: readdir returns
    // entries in whatever order the directory happens to hold.
    ASSERT_LT(std::strcmp(ids[i], ids[i + 1]), 0)
        << ids[i] << " then " << ids[i + 1];
    if (std::strcmp(ids[i], "Europe/Paris") == 0) {
      found_paris = true;
    }
  }
  EXPECT_TRUE(found_paris);
}

TEST_F(Zones, DumpWritesSomethingForEveryInput) {
  FILE * sink = std::tmpfile();
  ASSERT_NE(nullptr, sink);
  gchron_zonedb_dump(db_, sink);
  gchron_zonedb_dump(nullptr, sink);
  gchron_zone_dump(zone("Europe/Paris"), sink);
  gchron_zone_dump(nullptr, sink);
  GCHRON_ZonedDateTime zoned{};
  GCHRON_Instant moment{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1781539200, 0, &moment));
  ASSERT_EQ(GCHRON_OK,
      gchron_zoned_from_instant(moment, zone("Europe/Paris"), &zoned));
  gchron_zoned_dump(&zoned, sink);
  gchron_zoned_dump(nullptr, sink);
  EXPECT_GT(std::ftell(sink), 0);
  std::fclose(sink);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
