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

/*
 * A zone directory too long for the filesystem to accept is a fact about the
 * environment, not about the identifier the caller passed.
 *
 * cutil reports `ENAMETOOLONG` as GCU_FILE_ERR_INVALID, on the reasoning that
 * only the caller can change its argument - which is right at that boundary
 * and wrong when it crosses this one, because the argument *this* library's
 * caller supplied is a zone name that may be perfectly good. Passed straight
 * through it told them to fix `Europe/Paris`.
 *
 * GCHRON_ERR_UNSUPPORTED - "this database does not have that zone" - is the
 * same answer an unreadable directory gives, which is what an over-long one
 * is a case of.
 */
TEST(ZoneDb, ADirectoryTooLongForTheFilesystemIsNotTheCallersZoneName) {
  std::string dir(5000, 'a');
  dir[0] = '/';
  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_zonedb_directory(dir.c_str(), nullptr, nullptr, &db));

  const GCHRON_Zone * zone = nullptr;
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_zonedb_zone(db, "Europe/Paris", &zone))
      << "an over-long directory was reported as a bad zone identifier";
  EXPECT_EQ(nullptr, zone);

  // A name the tzdb only reaches through its link table takes the same route,
  // which it can only do because the failure above is spelled GCHRON_ERR_IO
  // underneath: that is the value gating the link lookup.
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_zonedb_zone(db, "US/Eastern", &zone));

  gchron_zonedb_destroy(db);

  // And a genuinely bad argument still says so.
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_zonedb_zone(nullptr, "Europe/Paris", &zone));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/*
 * max_tzif_bytes says "largest TZif image the zone loader will *read*", and
 * four places honour it: the parse-time length check in tzif.c, and three
 * reads - a named zone, a link target, and /etc/localtime.
 *
 * Only the parse-time one was tested, and the three reads could all be set to
 * "unlimited" with the whole suite still green. That is invisible from a
 * return code: an oversized zone fails with GCHRON_ERR_LIMIT whether the read
 * stopped at the cap or the parse rejected a megabyte already sitting in
 * memory. The status is the same and the memory is not, and a caller setting
 * this field is bounding the memory.
 *
 * So this asserts the bound. The file is far larger than the cap, and the
 * largest single allocation has to stay near the cap rather than near the
 * file. Dropping the cap at zonedb.c's named-zone read makes this fail with
 * "largest single allocation 2097152 bytes against a 4096-byte cap".
 *
 * It covers one of the three reads. The link-target read beside it and
 * local.c's /etc/localtime read are still decided by no test: the first needs
 * a link table a directory database does not build from a fixture, and the
 * second reads an absolute system path this suite cannot point elsewhere.
 * Both are recorded here rather than left to be rediscovered, because the
 * suite passing says nothing about either.
 */
TEST(ZoneDb, AnOversizedZoneFileIsNotReadJustToBeRejected) {
  const size_t cap = 4096;

  // The largest single allocation made while refusing a zone file of the
  // given size. Returned rather than asserted so the two readings can be
  // compared with each other, which is the whole test.
  auto peak_refusing = [cap](size_t file_size) {
    gchrontest::TempDir dir;
    dir.write("Oversized", std::string(file_size, '\0'));

    GCHRON_Limits limits{};
    gchron_limits_default(&limits);
    limits.max_tzif_bytes = cap;

    gchrontest::RecordingAllocator recorder;
    GCHRON_ZoneDb * db = nullptr;
    EXPECT_EQ(GCHRON_OK, gchron_zonedb_directory(dir.path().c_str(),
        recorder.get(), &limits, &db));

    const GCHRON_Zone * zone = nullptr;
    EXPECT_EQ(GCHRON_ERR_LIMIT, gchron_zonedb_zone(db, "Oversized", &zone));

    gchron_zonedb_destroy(db);

    /*
     * One bound beside the invariance, because invariance alone would also
     * hold for a reader that allocated a huge constant whatever the input:
     * nothing varies with the file size, so nothing moves, and the equality
     * below passes. This rules that out and is still not a number anybody
     * chose - it is the claim a caller setting max_tzif_bytes actually has,
     * which is that refusing a file does not cost holding it.
     */
    EXPECT_LT(recorder.largest(), file_size)
        << "refusing a " << file_size << "-byte file allocated "
        << recorder.largest() << " bytes in a single call";

    return recorder.largest();
  };

  const size_t small = peak_refusing(256 * 1024);
  const size_t large = peak_refusing(1024 * 1024);

  // The property, with no threshold in it: refusing a file four times the
  // size must not cost four times the memory, because the cap is what the
  // reader stops at and the cap did not change. A number to compare against
  // would need choosing, and would drift into a test of the reader's
  // buffering; this needs nothing chosen.
  EXPECT_EQ(small, large)
      << "the file was read before it was refused: refusing 256 KiB peaked at "
      << small << " bytes and refusing 1 MiB at " << large
      << ", against a " << cap << "-byte cap that did not move";
}

/*
 * The second of max_tzif_bytes' three reads: the one taken when a name is
 * not a file in the directory but the tzdb's link table says what it stands
 * for.
 *
 * The comment above says this read was decided by no test because a
 * directory database does not build a link table from a fixture. **That was
 * wrong**, and wrong in the direction that keeps a gap open: the link table
 * is built by reading `tzdata.zi` out of the database's own directory, and a
 * TempDir can write one. Nothing exotic was needed - a two-line fixture
 * reaches it.
 *
 * Refusal here is GCHRON_ERR_UNSUPPORTED rather than GCHRON_ERR_LIMIT,
 * because the link arm turns every failure into "not in this database"
 * (zonedb.c). That is defensible and it means the status says nothing at all
 * about memory, which is the whole reason this asserts the peak instead.
 */
TEST(ZoneDb, AnOversizedLinkTargetIsNotReadJustToBeRejected) {
  const size_t cap = 4096;

  auto peak_refusing = [cap](size_t file_size) {
    gchrontest::TempDir dir;
    dir.write("RealZone", std::string(file_size, '\0'));
    /* `L <target> <link name>` - the abbreviated form `tzdata.zi` uses. */
    dir.write("tzdata.zi", "L RealZone AliasZone\n");

    GCHRON_Limits limits{};
    gchron_limits_default(&limits);
    limits.max_tzif_bytes = cap;

    gchrontest::RecordingAllocator recorder;
    GCHRON_ZoneDb * db = nullptr;
    EXPECT_EQ(GCHRON_OK, gchron_zonedb_directory(dir.path().c_str(),
        recorder.get(), &limits, &db));

    /*
     * There is no file called `AliasZone`, so this only refuses at all if the
     * link table resolved it to `RealZone` and tried that instead. A lookup
     * that never followed the link would refuse too, for the wrong reason -
     * which is what the companion test below pins down.
     */
    const GCHRON_Zone * zone = nullptr;
    EXPECT_NE(GCHRON_OK, gchron_zonedb_zone(db, "AliasZone", &zone));

    gchron_zonedb_destroy(db);

    EXPECT_LT(recorder.largest(), file_size)
        << "refusing a " << file_size << "-byte link target allocated "
        << recorder.largest() << " bytes in a single call";

    return recorder.largest();
  };

  const size_t small = peak_refusing(256 * 1024);
  const size_t large = peak_refusing(1024 * 1024);

  EXPECT_EQ(small, large)
      << "the link target was read before it was refused: refusing 256 KiB "
      << "peaked at " << small << " bytes and refusing 1 MiB at " << large
      << ", against a " << cap << "-byte cap that did not move";
}

/*
 * That the link was followed at all, which the test above assumes and cannot
 * show on its own: an unresolved name and a resolved-then-refused one both
 * come back GCHRON_ERR_UNSUPPORTED.
 *
 * `AliasZone` resolves to `RealZone`, whose contents are far too small to be
 * a TZif image - so a lookup that followed the link gets as far as the parser
 * and fails there, while one that did not follow it never opens a file. The
 * two are told apart by the bytes read, not by the result.
 */
TEST(ZoneDb, ALinkNameIsResolvedThroughTzdataZi) {
  gchrontest::TempDir dir;
  /*
   * Large on purpose. An earlier draft used 64 bytes and was **vacuous**: the
   * database's own structures - eight size_t limits plus pointers - already
   * allocate more than that, so "an allocation at least this big happened"
   * was true whether or not a file was ever opened. It survived a mutant that
   * disabled link resolution outright. The fixture has to dwarf every
   * structural allocation before its size means anything.
   */
  const std::string body(128 * 1024, 'x');
  dir.write("RealZone", body);
  dir.write("tzdata.zi", "L RealZone AliasZone\n");

  gchrontest::RecordingAllocator recorder;
  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_directory(dir.path().c_str(),
      recorder.get(), nullptr, &db));

  const size_t before = recorder.largest();
  const GCHRON_Zone * zone = nullptr;
  EXPECT_NE(GCHRON_OK, gchron_zonedb_zone(db, "AliasZone", &zone));
  gchron_zonedb_destroy(db);

  EXPECT_GT(recorder.largest(), before);
  EXPECT_GE(recorder.largest(), body.size())
      << "no allocation as large as the link target's " << body.size()
      << " bytes was made, so `AliasZone` was refused without the link "
      << "table ever sending it to `RealZone` (largest before the lookup was "
      << before << ")";
}

/*
 * A line `tzdata.zi` does not use, next to one it does.
 *
 * build_links() walks the file twice: once to count the link lines so the
 * two parallel arrays can be allocated at the right size, and once to carve
 * them up. The two passes have to agree about what a link line *is*, and a
 * disagreement is not a miscount - the second pass stops at `index < count`,
 * so every line it accepts that the first did not count spends a slot that
 * belonged to a real link, and the real link is dropped off the end.
 *
 * `L` is the abbreviated spelling `zishrink.awk` emits and is all a stock
 * `tzdata.zi` contains. `Link` is the spelling the tzdb's own source files
 * use, and zic accepts, so it is what a hand-assembled or unshrunk file has
 * in it. Neither pass is obliged to support it - but they are obliged to
 * make the same decision about it.
 */
TEST(ZoneDb, ALineOnlyOnePassCallsALinkDoesNotCostARealLinkItsSlot) {
  gchrontest::TempDir dir;
  const std::string body(128 * 1024, 'x');
  dir.write("RealZone", body);
  dir.write("tzdata.zi",
      "Link RealZone LongFormAlias\n"
      "L RealZone AliasZone\n");

  gchrontest::RecordingAllocator recorder;
  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_directory(dir.path().c_str(),
      recorder.get(), nullptr, &db));

  const size_t before = recorder.largest();
  const GCHRON_Zone * zone = nullptr;
  EXPECT_NE(GCHRON_OK, gchron_zonedb_zone(db, "AliasZone", &zone));
  gchron_zonedb_destroy(db);

  EXPECT_GE(recorder.largest(), body.size())
      << "`AliasZone` stopped resolving because of a line above it that only "
      << "one of the two passes treats as a link: the counting pass requires "
      << "`L` and then a space, the rewriting pass requires only `L`, so "
      << "`Link ...` took the single slot the count had reserved for "
      << "`L RealZone AliasZone` (largest allocation was " << recorder.largest()
      << ", floor before the lookup " << before << ")";
}

/*
 * A directory that is a directory and holds no zones.
 *
 * Found by tests/fuzz/fuzz_zonedir.cpp on its second execution, which is
 * about as shallow as a finding gets - the input was a single newline. No
 * test had ever built a directory database over a directory with nothing in
 * it to collect, so the identifier array was never allocated and
 * gchron_zonedb_list() handed `qsort` a null base. glibc's `qsort` returns
 * immediately when the count is zero, so nothing misbehaved and nothing ever
 * would; but `qsort`'s first parameter is declared `__nonnull`, which makes
 * the call undefined regardless of the count, and UBSan says so.
 *
 * A caller reaches it by naming a path that exists and holds no zone files -
 * a mistyped configuration value, or a directory whose zones have not been
 * installed yet.
 *
 * This is a regression test for a diagnostic, so it fails only under
 * `make test-asan`. Under a plain `make test` it passes either way, and that
 * is not a reason to leave it out: the sanitiser build is a gate this suite
 * runs, and the next person to break this will break it there.
 */
TEST(ZoneDb, ADirectoryWithNoZonesListsNothingWithoutUndefinedBehaviour) {
  gchrontest::TempDir dir;
  dir.write("not-a-zone.txt", "nothing here is a TZif image\n");

  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_directory(dir.path().c_str(), nullptr,
      nullptr, &db));

  const char * const * ids = nullptr;
  size_t count = 0;
  EXPECT_EQ(GCHRON_OK, gchron_zonedb_list(db, &ids, &count));
  EXPECT_EQ(0u, count);

  // Asking twice takes the cached path, which must also not sort a null base.
  EXPECT_EQ(GCHRON_OK, gchron_zonedb_list(db, &ids, &count));
  EXPECT_EQ(0u, count);

  gchron_zonedb_destroy(db);
}
