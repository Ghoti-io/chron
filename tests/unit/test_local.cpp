/**
 * @file
 *
 * `gchron_zonedb_local()` - the zone a machine says it is in.
 *
 * Its own binary because every test here writes `TZ`, and a process-wide
 * environment variable set in one suite is a trap for every other suite in
 * the same executable.
 *
 * This function had no test at all until 2026-09-21, which left `local.c` the
 * worst-covered file in the library. It is also the one an application
 * actually calls, and its three fallbacks fail by returning the *wrong* zone
 * rather than by returning an error - the shape that is invisible until
 * somebody notices their timestamps are an hour out.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdlib>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

/** Restores `TZ` to whatever the process started with. */
class Local : public ::testing::Test {
protected:
  void SetUp() override {
    const char * tz = std::getenv("TZ");
    had_tz_ = tz != nullptr;
    if (had_tz_) {
      saved_ = tz;
    }
  }
  void TearDown() override {
    if (had_tz_) {
      ::setenv("TZ", saved_.c_str(), 1);
    }
    else {
      ::unsetenv("TZ");
    }
  }

  static void set_tz(const char * value) {
    if (value == nullptr) {
      ::unsetenv("TZ");
    }
    else {
      ::setenv("TZ", value, 1);
    }
  }

  /** The zone `TZ=value` resolves to, and its identifier or "". */
  static std::string local_id(const char * value, GCHRON_Result * result) {
    gchrontest::ZoneDb db;
    const GCHRON_Zone * zone = nullptr;
    set_tz(value);
    *result = gchron_zonedb_local(db.get(), &zone);
    if (*result != GCHRON_OK) {
      return std::string();
    }
    const char * id = gchron_zone_id(zone);
    return id != nullptr ? std::string(id) : std::string();
  }

private:
  bool had_tz_ = false;
  std::string saved_;
};

} // namespace

TEST_F(Local, TzNamingAZoneIsThatZone) {
  GCHRON_Result result;
  EXPECT_EQ("Europe/Paris", local_id("Europe/Paris", &result));
  EXPECT_EQ(GCHRON_OK, result);

  // POSIX makes everything after a leading colon implementation-defined, and
  // every Unix reads it as the same value.
  EXPECT_EQ("Europe/Paris", local_id(":Europe/Paris", &result));
  EXPECT_EQ(GCHRON_OK, result);
}

/*
 * `GMT0` is both: a file the tzdb ships and a POSIX rule string that parses.
 * The file is the right answer - it carries whatever transitions the tzdb
 * recorded, and the rule carries only what the string says - and the order of
 * the two attempts in local.c is the only thing that decides it. Both give
 * offset zero, so the offset cannot tell them apart; the identifier can,
 * because gchron_zonedb_posix() builds an anonymous zone and a name lookup
 * does not.
 *
 * `EST5EDT` looks like the better example and is not one: this library
 * refuses a daylight-saving abbreviation with no transition rule, because
 * POSIX leaves those dates implementation-defined and glibc fills them in
 * with a United States guess. So it reaches the zone file whichever order the
 * two attempts are made in, and pins nothing. local.c named it until
 * 2026-09-21.
 */
TEST_F(Local, AValueThatIsBothAZoneAndARuleIsReadAsTheZone) {
  GCHRON_Result result;
  EXPECT_EQ("GMT0", local_id("GMT0", &result))
      << "the rule string was used where the zone file was meant";
  EXPECT_EQ(GCHRON_OK, result);

  // The value that is only a file still reaches it, for the duller reason.
  EXPECT_EQ("EST5EDT", local_id("EST5EDT", &result));
  EXPECT_EQ(GCHRON_OK, result);
  EXPECT_EQ("", local_id("EST5EDT,M3.2.0,M11.1.0", &result))
      << "a rule with transition dates is no file's name";
  EXPECT_EQ(GCHRON_OK, result);
}

TEST_F(Local, TzNamingARuleWithNoZoneFileIsThatRule) {
  GCHRON_Result result;
  // No zone is called this, and it parses as a rule: five hours west, no
  // daylight saving.
  EXPECT_EQ("", local_id("XYZ5", &result)) << "a rule has no identifier";
  EXPECT_EQ(GCHRON_OK, result);

  gchrontest::ZoneDb db;
  const GCHRON_Zone * zone = nullptr;
  set_tz("XYZ5");
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_local(db.get(), &zone));
  GCHRON_Instant when = { 1789918200, 0 };
  GCHRON_ZoneInfo info{};
  ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(zone, when, &info));
  EXPECT_EQ(-5 * 3600, info.offset_sec);
}

/*
 * POSIX: a `TZ` that names nothing means UTC. Following it rather than
 * failing, because a program whose environment is wrong should still be able
 * to print a timestamp.
 *
 * What comes back is gchron_zonedb_utc(), which is a fixed zone at offset
 * zero and carries no identifier - so a caller cannot mistake it for the
 * machine's own zone, and writing it out gives `Z` rather than a name that
 * would claim more than is known. local.c said "UTC by name" until
 * 2026-09-21; the behaviour was right and the comment was not.
 */
TEST_F(Local, AnUnusableTzIsUtcAndCarriesNoName) {
  for (const char * bad : { "Not/AZone", "../../etc/passwd", ":",
      "Europe/Paris/../../etc/shadow", "Etc/../Etc/UTC" }) {
    gchrontest::ZoneDb db;
    const GCHRON_Zone * zone = nullptr;
    set_tz(bad);
    ASSERT_EQ(GCHRON_OK, gchron_zonedb_local(db.get(), &zone)) << bad;
    EXPECT_EQ(nullptr, gchron_zone_id(zone)) << bad;

    GCHRON_Instant when = { 1789918200, 0 };
    GCHRON_ZoneInfo info{};
    ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(zone, when, &info)) << bad;
    EXPECT_EQ(0, info.offset_sec) << bad;
    EXPECT_FALSE(info.is_dst) << bad;
  }
}

/*
 * An empty `TZ` is not a value; POSIX gives it no meaning and the shells set
 * it by accident. Falling through to the machine's own configuration is what
 * every other implementation does.
 */
TEST_F(Local, AnEmptyTzFallsThroughToTheMachine) {
  GCHRON_Result with_empty;
  std::string from_empty = local_id("", &with_empty);
  GCHRON_Result with_none;
  std::string from_none = local_id(nullptr, &with_none);
  EXPECT_EQ(with_none, with_empty);
  EXPECT_EQ(from_none, from_empty);
}

/*
 * With no `TZ` the answer comes from `/etc/localtime` or `/etc/timezone`.
 * Which of the three paths runs depends on how the machine is set up, and the
 * path is hardcoded, so a test cannot choose. What every path owes is a zone
 * that can answer a question, and one the database hands back again rather
 * than rebuilding.
 *
 * **Two of the three are not reached on a machine whose `/etc/localtime` is a
 * symlink**, which is every Debian and every Fedora: the `/etc/timezone`
 * fallback and the unnamed-zone path that interns under `LOCAL_KEY`. Nothing
 * here can force them, and saying so beats a test that looks as though it
 * covered them.
 */
TEST_F(Local, TheMachinesOwnZoneIsUsableAndHandedBackRatherThanRebuilt) {
  gchrontest::ZoneDb db;
  const GCHRON_Zone * zone = nullptr;
  set_tz(nullptr);
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_local(db.get(), &zone));
  ASSERT_NE(nullptr, zone);

  GCHRON_Instant when = { 1789918200, 0 };
  GCHRON_ZoneInfo info{};
  ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(zone, when, &info));
  EXPECT_GE(info.offset_sec, -18 * 3600);
  EXPECT_LE(info.offset_sec, 18 * 3600);

  // Zone identity is pointer identity within one database, so asking twice
  // must not build it twice.
  const GCHRON_Zone * again = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_local(db.get(), &again));
  EXPECT_EQ(zone, again);

  const char * id = gchron_zone_id(zone);
  if (id != nullptr) {
    const GCHRON_Zone * by_name = nullptr;
    ASSERT_EQ(GCHRON_OK, gchron_zonedb_zone(db.get(), id, &by_name));
    EXPECT_EQ(zone, by_name) << "the local zone and its own name disagree";
  }
}

TEST_F(Local, NeitherArgumentMayBeNull) {
  gchrontest::ZoneDb db;
  const GCHRON_Zone * zone = nullptr;
  set_tz("Europe/Paris");
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_zonedb_local(nullptr, &zone));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_zonedb_local(db.get(), nullptr));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
