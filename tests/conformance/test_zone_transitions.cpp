/**
 * @file
 *
 * Every transition of twenty zones, answered by `zdump` and asked of this
 * library.
 *
 * `zdump` is the reference implementation of the tzdb itself, so for each row
 * three things are checked at once: that the offset this library reports at
 * that instant is the offset the tzdb says; that the daylight-saving flag and
 * the abbreviation match; and that the civil reading derived from the instant
 * is the one `zdump` printed. A reader that got the offset right and the
 * arithmetic wrong would pass the first and fail the third.
 *
 * The rows come in pairs - the last second before a transition and the first
 * second of it - so every one of them is a boundary, which is where an
 * off-by-one in the binary search or in the footer hand-over lives.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

struct Row {
  std::string zone;
  int64_t seconds;
  int32_t utoff;
  bool is_dst;
  std::string abbreviation;
  std::string local_civil;
  int line;
};

std::vector<Row> load() {
  std::vector<Row> rows;
  std::string path =
      gchrontest::data_dir() + "/vectors/zones/transitions.vec";
  std::ifstream in(path);
  // Not a skip. The vectors are committed so that a run needs neither zdump
  // nor a zoneinfo directory to *read* them; a run that cannot find them has
  // a broken harness (design.md section 12.3).
  EXPECT_TRUE(in.is_open())
      << "missing vector file: " << path << " (run tools/oracle/zdump.py)";

  std::string line;
  int number = 0;
  while (std::getline(in, line)) {
    ++number;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream fields(line);
    Row row;
    std::string isdst;
    if (!std::getline(fields, row.zone, '\t')) {
      continue;
    }
    std::string seconds;
    std::string utoff;
    std::getline(fields, seconds, '\t');
    std::getline(fields, utoff, '\t');
    std::getline(fields, isdst, '\t');
    std::getline(fields, row.abbreviation, '\t');
    std::getline(fields, row.local_civil, '\t');
    row.seconds = std::strtoll(seconds.c_str(), nullptr, 10);
    row.utoff = static_cast<int32_t>(std::strtol(utoff.c_str(), nullptr, 10));
    row.is_dst = (isdst == "1");
    row.line = number;
    rows.push_back(row);
  }
  return rows;
}

/** `YYYY-MM-DDThh:mm:ss`, as the vector spells a civil reading. */
std::string civil_string(const GCHRON_DateTime & dt) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02uT%02u:%02u:%02u",
      dt.date.year, static_cast<unsigned>(dt.date.month),
      static_cast<unsigned>(dt.date.day),
      static_cast<unsigned>(dt.time.hour),
      static_cast<unsigned>(dt.time.minute),
      static_cast<unsigned>(dt.time.second));
  return std::string(buffer);
}

class ZoneVectors : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(GCHRON_OK, gchrontest::open_zonedb(&db_))
        << "this machine has no system zoneinfo directory";
  }
  void TearDown() override { gchron_zonedb_destroy(db_); }
  GCHRON_ZoneDb * db_ = nullptr;
};

} // namespace

TEST_F(ZoneVectors, EveryTransitionAgreesWithZdump) {
  std::vector<Row> rows = load();
  ASSERT_GT(rows.size(), 8000u)
      << "the committed corpus shrank; regenerate it with "
         "tools/oracle/zdump.py rather than letting it check less";

  std::map<std::string, const GCHRON_Zone *> zones;
  size_t checked = 0;
  size_t missing_zones = 0;

  for (const Row & row : rows) {
    auto found = zones.find(row.zone);
    if (found == zones.end()) {
      const GCHRON_Zone * zone = nullptr;
      GCHRON_Result result = gchron_zonedb_zone(db_, row.zone.c_str(), &zone);
      if (result != GCHRON_OK) {
        // This machine's database does not have the zone the vectors were
        // generated from. Counted and reported, never silently passed over:
        // design.md section 12 says skips are counted, never silent.
        ++missing_zones;
        zones[row.zone] = nullptr;
        continue;
      }
      found = zones.emplace(row.zone, zone).first;
    }
    if (found->second == nullptr) {
      continue;
    }

    GCHRON_Instant instant{};
    ASSERT_EQ(GCHRON_OK,
        gchron_instant_create(row.seconds, 0, &instant));

    GCHRON_ZoneInfo info{};
    ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(found->second, instant, &info))
        << row.zone << " at " << row.seconds;

    EXPECT_EQ(row.utoff, info.offset_sec)
        << row.zone << ":" << row.line << " at " << row.seconds;
    EXPECT_EQ(row.is_dst, info.is_dst)
        << row.zone << ":" << row.line << " at " << row.seconds;
    EXPECT_STREQ(row.abbreviation.c_str(), info.abbreviation)
        << row.zone << ":" << row.line << " at " << row.seconds;

    // The civil reading too, so that a right offset with wrong arithmetic
    // cannot pass.
    GCHRON_ZonedDateTime zoned{};
    GCHRON_DateTime civil{};
    ASSERT_EQ(GCHRON_OK,
        gchron_zoned_from_instant(instant, found->second, &zoned));
    ASSERT_EQ(GCHRON_OK, gchron_zoned_to_civil(&zoned, &civil));
    EXPECT_EQ(row.local_civil, civil_string(civil))
        << row.zone << ":" << row.line << " at " << row.seconds;
    ++checked;
  }

  std::printf("[          ] %zu of %zu zdump rows checked", checked,
      rows.size());
  if (missing_zones > 0) {
    std::printf("; %zu zones absent from this machine's database",
        missing_zones);
  }
  std::printf("\n");
  EXPECT_EQ(0u, missing_zones)
      << "a zone the vectors cover is not installed here, so those rows "
         "checked nothing";
}

// The transition search has to agree with the table it searches: walking
// forward from an instant must land on exactly the transitions the vectors
// list, in order, with nothing invented between them.
TEST_F(ZoneVectors, NextTransitionWalksTheSameBoundariesZdumpPrinted) {
  std::vector<Row> rows = load();
  ASSERT_FALSE(rows.empty());

  // The vectors are pairs: row 2k is the second before a change, row 2k+1 is
  // the first second of it. So every odd-indexed row's instant is a
  // transition, and the search should find it from just before.
  std::map<std::string, const GCHRON_Zone *> zones;
  size_t checked = 0;

  for (size_t i = 1; i < rows.size(); i += 2) {
    const Row & before = rows[i - 1];
    const Row & at = rows[i];
    if (before.zone != at.zone || at.seconds != before.seconds + 1) {
      continue; // not a pair; the file begins or ends a zone here
    }
    auto found = zones.find(at.zone);
    if (found == zones.end()) {
      const GCHRON_Zone * zone = nullptr;
      if (gchron_zonedb_zone(db_, at.zone.c_str(), &zone) != GCHRON_OK) {
        zones[at.zone] = nullptr;
        continue;
      }
      found = zones.emplace(at.zone, zone).first;
    }
    if (found->second == nullptr) {
      continue;
    }

    GCHRON_Instant probe{};
    ASSERT_EQ(GCHRON_OK, gchron_instant_create(before.seconds, 0, &probe));
    GCHRON_Transition transition{};
    GCHRON_Result result =
        gchron_zone_next_transition(found->second, probe, &transition);
    ASSERT_EQ(GCHRON_OK, result)
        << at.zone << ": no next transition after " << before.seconds;
    EXPECT_EQ(at.seconds, transition.at.sec) << at.zone << ":" << at.line;
    EXPECT_EQ(before.utoff, transition.before.offset_sec)
        << at.zone << ":" << at.line;
    EXPECT_EQ(at.utoff, transition.after.offset_sec)
        << at.zone << ":" << at.line;

    // And backwards from the transition itself lands on the same place.
    GCHRON_Transition backwards{};
    ASSERT_EQ(GCHRON_OK,
        gchron_zone_prev_transition(found->second, transition.at, &backwards));
    EXPECT_EQ(at.seconds, backwards.at.sec) << at.zone << ":" << at.line;
    ++checked;
  }
  std::printf("[          ] %zu transition boundaries walked\n", checked);
  EXPECT_GT(checked, 3000u);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
