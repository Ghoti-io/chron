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
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <ghoti.io/cutil/dir.h>
#endif

namespace {

/**
 * A directory-backed database to hold the embedded one up against.
 *
 * On Windows gchron_zonedb_system() has no directory to open unless `$TZDIR`
 * names one, which would make every comparison here a skip on the one
 * platform where the embedded table is what callers actually get. The build
 * names the tree the table was generated from instead, so the two readers are
 * still compared over the same release.
 */
GCHRON_Result open_directory_db(GCHRON_ZoneDb ** out) {
#if defined(_WIN32) && defined(GCHRON_TEST_ZONEINFO)
  if (std::getenv("TZDIR") == nullptr) {
    return gchron_zonedb_directory(GCHRON_TEST_ZONEINFO, nullptr, nullptr,
        out);
  }
#endif
  return gchron_zonedb_system(nullptr, nullptr, out);
}

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
  if (open_directory_db(&system_db) != GCHRON_OK) {
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

TEST(WindowsZones, EveryNameItCarriesResolvesInTheEmbeddedDatabase) {
  /*
   * This is the property the whole mapping exists for: on Windows,
   * gchron_zonedb_local() takes what GetDynamicTimeZoneInformation() reports,
   * looks it up here, and hands the result to gchron_zonedb_zone(). If any
   * step disagrees the zone is simply unavailable, and this is the test that
   * would say so.
   *
   * It caught a real gap. CLDR names `Asia/Calcutta`, `Europe/Kiev` and five
   * more backward-compatibility identifiers, and Debian ships those as a
   * separate `tzdata-legacy` package that is not installed - so an embedded
   * table built from the files present lacked exactly the names Windows would
   * hand it. The generator now folds in `tzdata.zi`'s own link table, which
   * ships with base tzdata and lists every one.
   */
  size_t count = gchron_zone_windows_mapping_count();
  ASSERT_GT(count, 100u) << "the Windows mapping is missing or truncated";
  ASSERT_NE(nullptr, gchron_zone_windows_mapping_version());

  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_embedded(nullptr, nullptr, &db));

  // The case WINDOWS-TODO.md 6b names as done.
  const char * id = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_zone_id_from_windows("Pacific Standard Time", &id));
  EXPECT_STREQ("America/Los_Angeles", id);

  // Every row, not a sample: the seven that were missing were exactly the
  // ones a spot check would not have named.
  size_t checked = 0;
  for (size_t i = 0; i < count; ++i) {
    const char * name = nullptr;
    const char * mapped = nullptr;
    ASSERT_EQ(GCHRON_OK,
        gchron_zone_windows_mapping_at(i, &name, &mapped));
    ASSERT_NE(nullptr, name);
    ASSERT_NE(nullptr, mapped);

    // Sorted, because the lookup binary-searches it.
    if (i > 0) {
      const char * previous = nullptr;
      ASSERT_EQ(GCHRON_OK,
          gchron_zone_windows_mapping_at(i - 1, &previous, nullptr));
      ASSERT_LT(std::strcmp(previous, name), 0);
    }

    // Reachable through the lookup a caller would use...
    const char * looked_up = nullptr;
    ASSERT_EQ(GCHRON_OK, gchron_zone_id_from_windows(name, &looked_up))
        << name;
    EXPECT_STREQ(mapped, looked_up);

    // ...and openable by the database Windows will actually be using.
    const GCHRON_Zone * zone = nullptr;
    EXPECT_EQ(GCHRON_OK, gchron_zonedb_zone(db, mapped, &zone))
        << name << " maps to " << mapped
        << ", which the embedded database cannot open";
    ++checked;
  }
  EXPECT_EQ(count, checked);
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_zone_windows_mapping_at(count, nullptr, nullptr));

  // A name no CLDR release carries is RANGE - there is a table, it simply
  // predates that Windows release.
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_zone_id_from_windows("No Such Standard Time", &id));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_zone_id_from_windows(nullptr, &id));

  gchron_zonedb_destroy(db);
}


TEST(ZoneDbLinks, ADirectoryDatabaseResolvesTheTzdbsLinksToo) {
  /*
   * A zoneinfo directory need not contain a file for every name the tzdb
   * defines. Debian splits the backward-compatibility names into a
   * `tzdata-legacy` package that is not installed by default, so on a stock
   * Debian there is no `Asia/Calcutta`, no `Europe/Kiev` and no `US/Eastern`
   * on disk - while `tzdata.zi`, which ships with base tzdata and which the
   * database already reads for its version, lists every one.
   *
   * Before this, the embedded database could open those names and the system
   * one could not, so gchron_zonedb_default() answered differently depending
   * on which it had chosen. They are names CLDR's Windows mapping uses, and
   * that Java and a great deal of existing configuration still use.
   */
  GCHRON_ZoneDb * system_db = nullptr;
  if (open_directory_db(&system_db) != GCHRON_OK) {
    GTEST_SKIP() << "no system zone database on this machine";
  }
  GCHRON_ZoneDb * embedded_db = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_zonedb_embedded(nullptr, nullptr, &embedded_db));

  static const struct {
    const char * link;
    const char * canonical;
  } kLinks[] = {
    { "Asia/Calcutta", "Asia/Kolkata" },
    { "Europe/Kiev", "Europe/Kyiv" },
    { "America/Godthab", "America/Nuuk" },
    { "Asia/Rangoon", "Asia/Yangon" },
    { "US/Eastern", "America/New_York" },
    { "GMT", nullptr },  // a link whose target varies by release
  };

  for (const auto & row : kLinks) {
    const GCHRON_Zone * from_system = nullptr;
    const GCHRON_Zone * from_embedded = nullptr;
    ASSERT_EQ(GCHRON_OK,
        gchron_zonedb_zone(system_db, row.link, &from_system)) << row.link;
    ASSERT_EQ(GCHRON_OK,
        gchron_zonedb_zone(embedded_db, row.link, &from_embedded))
        << row.link;

    // Both sources must agree on what it resolved to...
    const char * a = gchron_zone_canonical_id(from_system);
    const char * b = gchron_zone_canonical_id(from_embedded);
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    EXPECT_STREQ(b, a) << row.link;
    if (row.canonical != nullptr) {
      EXPECT_STREQ(row.canonical, a) << row.link;
    }

    // ...and on what it means.
    GCHRON_Instant when{ 1789918200, 0 };
    GCHRON_ZoneInfo x{};
    GCHRON_ZoneInfo y{};
    ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(from_system, when, &x));
    ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(from_embedded, when, &y));
    EXPECT_EQ(y.offset_sec, x.offset_sec) << row.link;
    EXPECT_STREQ(y.abbreviation, x.abbreviation) << row.link;
  }

  gchron_zonedb_destroy(embedded_db);
  gchron_zonedb_destroy(system_db);
}

TEST(ZoneDbLinks, WhatTheDatabaseListsIsWhatItCanOpen) {
  /*
   * The listing and the lookup must agree about what the database contains.
   * They are built by different code - one walks a directory and then folds
   * in the link table, the other tries a file and then falls back to it - so
   * this is a real check and not a restatement.
   */
  GCHRON_ZoneDb * db = nullptr;
  if (open_directory_db(&db) != GCHRON_OK) {
    GTEST_SKIP() << "no system zone database on this machine";
  }
  const char * const * ids = nullptr;
  size_t count = 0;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_list(db, &ids, &count));
  ASSERT_GT(count, 400u);

  // Every tenth, so the test stays quick while still covering the links,
  // which sort throughout the list rather than clustering at the end.
  size_t checked = 0;
  for (size_t i = 0; i < count; i += 10) {
    const GCHRON_Zone * zone = nullptr;
    ASSERT_EQ(GCHRON_OK, gchron_zonedb_zone(db, ids[i], &zone))
        << ids[i] << " is listed but cannot be opened";
    ++checked;
  }
  EXPECT_GT(checked, 40u);
  gchron_zonedb_destroy(db);
}

/**
 * A throwaway zoneinfo tree, removed when the test ends.
 *
 * Small enough to build by hand: two directories, two copies of a real TZif
 * file, and a `tzdata.zi` the test writes itself.
 */
class Fixture {
public:
  bool build() {
#ifdef _WIN32
    // No /tmp, no mkdtemp, and no zoneinfo under /usr/share: cutil makes the
    // directory, and the build says where the generator's tree is.
    char * made = nullptr;
    if (gcu_dir_temp_create(nullptr, "gchron_link", nullptr, &made)
        != GCU_FILE_OK) {
      return false;
    }
    root_ = made;
    gcu_dir_free_path(nullptr, made);
#ifdef GCHRON_TEST_ZONEINFO
    std::string tzif = read_file(GCHRON_TEST_ZONEINFO "/UTC");
#else
    std::string tzif;
#endif
#else
    char pattern[] = "/tmp/gchron_link_XXXXXX";
    const char * made = mkdtemp(pattern);
    if (made == nullptr) {
      return false;
    }
    root_ = made;

    std::string tzif = read_file("/usr/share/zoneinfo/UTC");
#endif
    if (tzif.size() < 4 || tzif.compare(0, 4, "TZif") != 0) {
      return false;
    }
    return make_dir(root_ + "/db") && make_dir(root_ + "/outside")
        && write_file(root_ + "/db/Inside", tzif)
        && write_file(root_ + "/outside/Target", tzif);
  }

  /** The database directory. */
  std::string db() const { return root_ + "/db"; }

  bool write_zi(const std::string & text) {
    return write_file(root_ + "/db/tzdata.zi", text);
  }

  ~Fixture() {
    if (root_.empty()) {
      return;
    }
    static const char * const kFiles[] = {
      "/db/Inside", "/db/tzdata.zi", "/outside/Target",
    };
    for (const char * file : kFiles) {
      std::remove((root_ + file).c_str());
    }
    rmdir((root_ + "/db").c_str());
    rmdir((root_ + "/outside").c_str());
    rmdir(root_.c_str());
  }

private:
  static bool make_dir(const std::string & path) {
#ifdef _WIN32
    return gcu_dir_create(path.c_str()) == GCU_FILE_OK;
#else
    return mkdir(path.c_str(), 0700) == 0;
#endif
  }
  static std::string read_file(const char * path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
  }
  static bool write_file(const std::string & path, const std::string & body) {
    std::ofstream out(path, std::ios::binary);
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    return out.good();
  }

  std::string root_;
};

TEST(ZoneDbLinks, ALinkCannotNameAFileOutsideTheDatabase) {
  /*
   * A zone identifier that reaches this library from a document goes through
   * gchron_zone_id_is_safe() before it is turned into a path. The *target* of
   * a link did not: it is read out of the directory's own `tzdata.zi`, which
   * looked trustworthy because in practice it is written by zic.
   *
   * It is not trustworthy in the only case that matters. A caller may open a
   * database anywhere - $TZDIR names one, and so does an unpacked archive or
   * a container image - and a `tzdata.zi` in it is then whatever that
   * directory holds. `L ../outside/Target Escape` would have been joined
   * unexamined, so a lookup of a name the database *lists* would have read
   * and parsed a file the database does not contain.
   *
   * The escape is built to succeed if it is allowed to: the target is a real
   * TZif file, so without the check this test reports GCHRON_OK for `Escape`
   * rather than failing on the parse afterwards and leaving the reason
   * ambiguous.
   */
  Fixture fixture;
  if (!fixture.build()) {
    GTEST_SKIP() << "could not build a fixture zoneinfo tree";
  }
  ASSERT_TRUE(fixture.write_zi("# version test\n"
      "L Inside Alias\n"
      "L ../outside/Target Escape\n"));

  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_zonedb_directory(fixture.db().c_str(), nullptr, nullptr, &db));

  /*
   * First that the mechanism is live in this fixture. Without this the test
   * would pass just as well against a build in which link resolution was
   * broken outright, which is not what it is asking.
   */
  const GCHRON_Zone * alias = nullptr;
  EXPECT_EQ(GCHRON_OK, gchron_zonedb_zone(db, "Alias", &alias))
      << "the fixture's ordinary link does not resolve, so this test cannot "
         "tell a refused escape from a link table that never loaded";

  const GCHRON_Zone * escaped = nullptr;
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_zonedb_zone(db, "Escape", &escaped))
      << "a link target left the database directory";
  EXPECT_EQ(nullptr, escaped);

  gchron_zonedb_destroy(db);
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
