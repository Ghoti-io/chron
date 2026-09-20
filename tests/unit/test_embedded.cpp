/**
 * @file
 *
 * The embedded time-zone database, and how gchron_zonedb_default() chooses.
 *
 * On Linux this path is the one that gets exercised least, because the system
 * database is always there and always preferred. On Windows it is the only
 * one there is (WINDOWS-TODO.md 6c), so it is tested here deliberately rather
 * than incidentally.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/chron.h>
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

namespace {

class Embedded : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(GCHRON_OK, gchron_zonedb_embedded(nullptr, nullptr, &db_));
    ASSERT_NE(nullptr, db_);
  }
  void TearDown() override { gchron_zonedb_destroy(db_); }

  GCHRON_ZoneDb * db_ = nullptr;
};

TEST_F(Embedded, ItKnowsWhichTzdataReleaseItIs) {
  const char * version = gchron_zonedb_version(db_);
  ASSERT_NE(nullptr, version);
  // A tzdata release is four digits and a lowercase letter, and
  // gchron_zonedb_default() compares two of them with strcmp - which is only
  // an ordering while that shape holds.
  ASSERT_EQ(5u, std::strlen(version)) << version;
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(version[i] >= '0' && version[i] <= '9') << version;
  }
  EXPECT_TRUE(version[4] >= 'a' && version[4] <= 'z') << version;

  EXPECT_EQ(GCHRON_ZONE_SOURCE_EMBEDDED, gchron_zonedb_source(db_));
}

TEST_F(Embedded, ItHoldsTheZonesAndAnswersTheSameAsTheSystemDatabase) {
  // The two databases are built from different bytes by different code paths
  // - one walks a directory, one indexes a blob - so agreeing is evidence
  // that the embedded path is right, not a tautology.
  GCHRON_ZoneDb * system_db = nullptr;
  if (gchron_zonedb_system(nullptr, nullptr, &system_db) != GCHRON_OK) {
    GTEST_SKIP() << "no system zone database on this machine";
  }

  static const char * const kZones[] = {
    "America/New_York", "Europe/Paris", "Asia/Tokyo", "Australia/Sydney",
    "Pacific/Apia", "Europe/Dublin", "America/Sao_Paulo", "UTC",
  };
  // A spread of instants: a DST boundary in each hemisphere, and the far past.
  static const int64_t kInstants[] = {
    1789918200, 1772000000, 1741000000, 1710000000, 0, -2208988800,
  };

  size_t checked = 0;
  for (const char * id : kZones) {
    const GCHRON_Zone * mine = nullptr;
    const GCHRON_Zone * theirs = nullptr;
    if (gchron_zonedb_zone(system_db, id, &theirs) != GCHRON_OK) {
      continue; /* not on this machine */
    }
    ASSERT_EQ(GCHRON_OK, gchron_zonedb_zone(db_, id, &mine)) << id;

    for (int64_t seconds : kInstants) {
      GCHRON_Instant when{ seconds, 0 };
      GCHRON_ZoneInfo a{};
      GCHRON_ZoneInfo b{};
      ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(mine, when, &a)) << id;
      ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(theirs, when, &b)) << id;
      EXPECT_EQ(b.offset_sec, a.offset_sec) << id << " at " << seconds;
      EXPECT_EQ(b.is_dst, a.is_dst) << id << " at " << seconds;
      EXPECT_STREQ(b.abbreviation, a.abbreviation)
          << id << " at " << seconds;
      ++checked;
    }
  }
  EXPECT_GT(checked, 20u) << "almost nothing was compared";
  gchron_zonedb_destroy(system_db);
}

TEST_F(Embedded, ItEnumeratesItselfInSortedOrder) {
  const char * const * ids = nullptr;
  size_t count = 0;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_list(db_, &ids, &count));
  EXPECT_GT(count, 400u);

  for (size_t i = 1; i < count; ++i) {
    // Sorted, because the lookup binary-searches the same table. A listing
    // that came back unsorted would mean the lookup was searching something
    // it had no right to assume was ordered.
    ASSERT_LT(std::strcmp(ids[i - 1], ids[i]), 0)
        << ids[i - 1] << " then " << ids[i];
  }

  // Everything listed must also resolve.
  for (size_t i = 0; i < count; i += 37) {
    const GCHRON_Zone * zone = nullptr;
    EXPECT_EQ(GCHRON_OK, gchron_zonedb_zone(db_, ids[i], &zone)) << ids[i];
  }
}

TEST_F(Embedded, ALinkResolvesAndSaysWhatItResolvedTo) {
  /*
   * This is the one thing the embedded table can do that a directory-backed
   * database cannot: TZif has nowhere to record that `America/Atka` is a link
   * to `America/Adak`, and the generator can see it because the file is a
   * symbolic link.
   */
  const char * const * ids = nullptr;
  size_t count = 0;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_list(db_, &ids, &count));

  size_t links = 0;
  for (size_t i = 0; i < count; ++i) {
    const GCHRON_Zone * zone = nullptr;
    ASSERT_EQ(GCHRON_OK, gchron_zonedb_zone(db_, ids[i], &zone)) << ids[i];
    const char * canonical = gchron_zone_canonical_id(zone);
    ASSERT_NE(nullptr, canonical) << ids[i];

    if (std::strcmp(canonical, ids[i]) != 0) {
      ++links;
      // What a link resolves to must itself be a zone, and must be the same
      // zone: same offsets, because they are literally the same bytes.
      const GCHRON_Zone * target = nullptr;
      ASSERT_EQ(GCHRON_OK, gchron_zonedb_zone(db_, canonical, &target))
          << canonical;
      GCHRON_Instant when{ 1789918200, 0 };
      GCHRON_ZoneInfo a{};
      GCHRON_ZoneInfo b{};
      ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(zone, when, &a));
      ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(target, when, &b));
      EXPECT_EQ(b.offset_sec, a.offset_sec) << ids[i] << " -> " << canonical;
    }
  }
  EXPECT_GT(links, 0u)
      << "no links at all: either the generator stopped following symlinks, "
         "or this machine's tzdata has none";
}

TEST_F(Embedded, AZoneItDoesNotHaveIsUnsupportedNotACrash) {
  const GCHRON_Zone * zone = nullptr;
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_zonedb_zone(db_, "Mars/Olympus_Mons", &zone));
  // And an unsafe identifier is refused before it reaches any lookup.
  EXPECT_EQ(GCHRON_ERR_INVALID,
      gchron_zonedb_zone(db_, "../../etc/passwd", &zone));
}

TEST(ZoneDbDefault, ItPrefersWhicheverDatabaseIsNewerAndSaysWhichItChose) {
  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_default(nullptr, nullptr, &db));

  GCHRON_ZoneSource source = gchron_zonedb_source(db);
  EXPECT_TRUE(source == GCHRON_ZONE_SOURCE_SYSTEM
      || source == GCHRON_ZONE_SOURCE_EMBEDDED);

  // Whichever it chose, it can name itself - which is what makes the choice
  // auditable rather than invisible (design.md section 6.6).
  const char * version = gchron_zonedb_version(db);

  GCHRON_ZoneDb * system_db = nullptr;
  if (gchron_zonedb_system(nullptr, nullptr, &system_db) == GCHRON_OK) {
    const char * system_version = gchron_zonedb_version(system_db);
    GCHRON_ZoneDb * embedded_db = nullptr;
    ASSERT_EQ(GCHRON_OK,
        gchron_zonedb_embedded(nullptr, nullptr, &embedded_db));
    const char * embedded_version = gchron_zonedb_version(embedded_db);

    if (system_version == nullptr) {
      // A database that will not say which release it is still wins: it is
      // the copy somebody is updating.
      EXPECT_EQ(GCHRON_ZONE_SOURCE_SYSTEM, source);
    }
    else {
      const char * newer =
          std::strcmp(embedded_version, system_version) > 0
              ? embedded_version : system_version;
      ASSERT_NE(nullptr, version);
      EXPECT_STREQ(newer, version);
      // Ties go to the system database.
      if (std::strcmp(embedded_version, system_version) <= 0) {
        EXPECT_EQ(GCHRON_ZONE_SOURCE_SYSTEM, source);
      }
    }
    gchron_zonedb_destroy(embedded_db);
    gchron_zonedb_destroy(system_db);
  }
  gchron_zonedb_destroy(db);
}

TEST(WindowsZones, TheMappingIsAbsentRatherThanEmptyWhenNotGenerated) {
  /*
   * The build never reaches the network, so on most machines the CLDR table
   * has not been generated and src/zone/windows_zones_absent.c stands in for
   * it. The distinction it keeps is the point: "there is no table" has a fix
   * the caller can carry out - run tools/tzdata/fetch-cldr.sh - and "this
   * table does not know that name" does not.
   *
   * This runs on every platform because the table is platform-independent
   * data; only the code that consults it is Windows-only.
   */
  size_t count = gchron_zone_windows_mapping_count();
  const char * id = nullptr;
  if (count == 0) {
    EXPECT_EQ(nullptr, gchron_zone_windows_mapping_version());
    EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
        gchron_zone_id_from_windows("Pacific Standard Time", &id));
    return;
  }

  // Generated: then it must answer the case WINDOWS-TODO.md 6b names as done.
  ASSERT_EQ(GCHRON_OK,
      gchron_zone_id_from_windows("Pacific Standard Time", &id));
  EXPECT_STREQ("America/Los_Angeles", id);
  EXPECT_NE(nullptr, gchron_zone_windows_mapping_version());

  // A name a present table does not carry is RANGE, not UNSUPPORTED: there
  // is a table, it simply predates that Windows release.
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_zone_id_from_windows("No Such Standard Time", &id));
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
