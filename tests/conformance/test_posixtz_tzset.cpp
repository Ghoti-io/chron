/**
 * @file
 *
 * The POSIX `TZ` footer grammar, checked against glibc's own `tzset`.
 *
 * design.md section 12 names this differential: glibc's `tzset` plus
 * `localtime_r`, with `TZ` set to each footer string in the system database,
 * across a lattice of instants. It is the strongest check available for this
 * grammar, because glibc is the implementation everything else on the machine
 * has been agreeing with for thirty years.
 *
 * The corpus is not written out here. It is **harvested from the system's own
 * TZif files** - every distinct footer string in every zone installed - so it
 * is whatever the tzdb actually ships rather than whatever a person thought
 * to type. A dozen hand-written rules would have missed the negative
 * daylight-saving offset, the thirty-minute shift, and the southern-hemisphere
 * rule that wraps the year, all three of which are in the harvest.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

/** Read the POSIX TZ footer out of a version-2 TZif image. */
bool footer_of(const std::string & path, std::string * out) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return false;
  }
  std::string bytes((std::istreambuf_iterator<char>(in)),
      std::istreambuf_iterator<char>());
  if (bytes.size() < 45 || bytes.compare(0, 4, "TZif") != 0) {
    return false;
  }
  // The footer is the last line of the file, between two newlines. Finding it
  // from the end avoids re-implementing the whole header walk here - which
  // would make this oracle share code with the thing it is checking, and an
  // oracle that shares the defect is not an oracle.
  size_t last = bytes.find_last_of('\n');
  if (last == std::string::npos || last + 1 != bytes.size()) {
    return false;
  }
  size_t first = bytes.find_last_of('\n', last - 1);
  if (first == std::string::npos) {
    return false;
  }
  *out = bytes.substr(first + 1, last - first - 1);
  return !out->empty();
}

/** Every distinct footer rule in the system's zoneinfo directory. */
std::vector<std::string> harvest() {
  std::set<std::string> rules;
  GCHRON_ZoneDb * db = nullptr;
  const char * const * ids = nullptr;
  size_t count = 0;

  if (gchron_zonedb_system(nullptr, nullptr, &db) != GCHRON_OK) {
    return {};
  }
  const char * dir = std::getenv("TZDIR");
  std::string root = (dir && *dir) ? dir : "/usr/share/zoneinfo";
  if (gchron_zonedb_list(db, &ids, &count) == GCHRON_OK) {
    for (size_t i = 0; i < count; ++i) {
      std::string rule;
      if (footer_of(root + "/" + ids[i], &rule)) {
        rules.insert(rule);
      }
    }
  }
  gchron_zonedb_destroy(db);
  return std::vector<std::string>(rules.begin(), rules.end());
}

/**
 * What glibc says a rule does at an instant.
 *
 * `tm_gmtoff` and `tm_zone` are glibc extensions, which is fine: this is a
 * test, it only ever runs where the oracle exists, and the oracle *is* glibc.
 */
struct GlibcAnswer {
  long offset;
  int is_dst;
  std::string abbreviation;
};

bool ask_glibc(const std::string & rule, std::time_t when, GlibcAnswer * out) {
  ::setenv("TZ", rule.c_str(), 1);
  ::tzset();
  std::tm parts{};
  if (::localtime_r(&when, &parts) == nullptr) {
    return false;
  }
  out->offset = parts.tm_gmtoff;
  out->is_dst = parts.tm_isdst > 0 ? 1 : 0;
  out->abbreviation = parts.tm_zone ? parts.tm_zone : "";
  return true;
}

} // namespace

TEST(PosixTz, EveryFooterInTheSystemDatabaseAgreesWithGlibc) {
  std::vector<std::string> rules = harvest();
  // Not a skip. A machine with no zoneinfo cannot run this differential at
  // all, and a suite that went green anyway would be reporting a pass on a
  // question it never asked (design.md section 12.3).
  ASSERT_FALSE(rules.empty())
      << "no TZif footers found; this differential checked nothing";

  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_system(nullptr, nullptr, &db));

  // A lattice rather than a sweep: every six hours through four years either
  // side of 2026 lands on both sides of every changeover a recurring rule can
  // have, including the 00:00 and 03:00 ones and the southern-hemisphere
  // pairs that wrap a year.
  const int64_t kStart = INT64_C(1704067200);  // 2024-01-01T00:00:00Z
  const int64_t kStep = 6 * 3600;
  const int64_t kSpan = 4LL * 365 * 86400;

  size_t rules_checked = 0;
  size_t probes = 0;
  size_t unparsed = 0;
  std::vector<std::string> refused;

  for (const std::string & rule : rules) {
    const GCHRON_Zone * zone = nullptr;
    if (gchron_zonedb_posix(db, rule.c_str(), &zone) != GCHRON_OK) {
      // Counted and named, never passed over: a rule this library cannot read
      // is the interesting result, not a reason to check one fewer.
      ++unparsed;
      if (refused.size() < 10) {
        refused.push_back(rule);
      }
      continue;
    }
    for (int64_t at = kStart; at < kStart + kSpan; at += kStep) {
      GlibcAnswer expected{};
      if (!ask_glibc(rule, static_cast<std::time_t>(at), &expected)) {
        continue;
      }
      GCHRON_Instant instant{};
      ASSERT_EQ(GCHRON_OK, gchron_instant_create(at, 0, &instant));
      GCHRON_ZoneInfo actual{};
      ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(zone, instant, &actual))
          << rule << " at " << at;

      ASSERT_EQ(expected.offset, actual.offset_sec)
          << "TZ=" << rule << " at " << at;
      ASSERT_EQ(expected.is_dst, actual.is_dst ? 1 : 0)
          << "TZ=" << rule << " at " << at;
      ASSERT_EQ(expected.abbreviation, std::string(actual.abbreviation))
          << "TZ=" << rule << " at " << at;
      ++probes;
    }
    ++rules_checked;
  }
  ::unsetenv("TZ");
  ::tzset();
  gchron_zonedb_destroy(db);

  std::printf("[          ] %zu rules, %zu probes against glibc's tzset\n",
      rules_checked, probes);
  for (const std::string & rule : refused) {
    std::printf("[          ] refused: %s\n", rule.c_str());
  }
  EXPECT_EQ(0u, unparsed)
      << unparsed << " footer rules in this machine's own database could not "
      << "be parsed, so those zones would answer wrongly past their tables";
  EXPECT_GT(rules_checked, 20u);
}

// The grammar's edges, which no real footer exercises but a hostile one will.
TEST(PosixTz, TheGrammarsEdgesAreAcceptedOrRefusedDeliberately) {
  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_system(nullptr, nullptr, &db));
  const GCHRON_Zone * zone = nullptr;

  struct Case { const char * rule; bool valid; const char * why; };
  const Case cases[] = {
    { "UTC0", true, "a fixed offset with no daylight-saving half" },
    { "MST7", true, "Arizona" },
    { "EST5EDT,M3.2.0,M11.1.0", true, "the ordinary shape" },
    { "<+05>-5", true, "an angle-bracketed numeric abbreviation" },
    { "<-04>4<-03>,M9.1.6/24,M4.1.6/24", true, "hour 24 in a rule time" },
    { "IST-1GMT0,M10.5.0,M3.5.0/1", true, "Dublin's negative daylight saving" },
    { "AEST-10AEDT,M10.1.0,M4.1.0/3", true, "a southern rule that wraps" },
    { "EST5EDT,J1,J365", true, "Julian day numbering" },
    { "EST5EDT,0,365", true, "zero-based day numbering" },
    { "EST5EDT,M3.2.0/-167,M11.1.0/167", true,
      "POSIX.1-2024's week-either-way transition times" },

    { "", false, "empty" },
    { "E5", false, "an abbreviation shorter than three characters" },
    { "EST", false, "no offset" },
    { "EST5EDT", false,
      "a daylight-saving name with no rule; glibc guesses a United States "
      "rule here, which is a guess about geography" },
    { "EST5EDT,M3.2.0", false, "only one end of the rule" },
    { "EST5EDT,M13.2.0,M11.1.0", false, "month 13" },
    { "EST5EDT,M3.6.0,M11.1.0", false, "week 6" },
    { "EST5EDT,M3.2.7,M11.1.0", false, "weekday 7; POSIX numbers them 0-6" },
    { "EST5EDT,J0,J365", false, "J0; Julian numbering starts at 1" },
    { "EST5EDT,J366,J1", false, "J366; J never counts 29 February" },
    { "EST5EDT,0,366", false, "day 366 in zero-based numbering" },
    { "EST25EDT,M3.2.0,M11.1.0", false, "an offset past 24 hours" },
    { "EST5EDT,M3.2.0/168,M11.1.0", false, "a transition time past 167 hours" },
    { "<+05-5", false, "an unterminated angle bracket" },
    { "EST5EDT,M3.2.0,M11.1.0,", false, "trailing comma" },
    { ":Europe/Paris", false, "the implementation-defined path form" },
  };

  for (const Case & c : cases) {
    GCHRON_Result result = gchron_zonedb_posix(db, c.rule, &zone);
    if (c.valid) {
      EXPECT_EQ(GCHRON_OK, result) << "\"" << c.rule << "\": " << c.why;
    }
    else {
      EXPECT_NE(GCHRON_OK, result) << "\"" << c.rule << "\": " << c.why;
    }
  }
  gchron_zonedb_destroy(db);
}

/*
 * Found by tests/fuzz/fuzz_posix_tz.cpp.
 *
 * `BST5CDT,M1.1.0/0,M1.1.0/1` names two changeovers that land on the same
 * instant: daylight saving would begin at midnight on the first Sunday of
 * January and end an hour later by a clock that is by then an hour fast. The
 * span is empty, so the zone never changes - and the transition search used
 * to report the pair anyway, contradicting the offset lookup beside it. One
 * said the zone turned to daylight saving at that instant; the other said it
 * never did.
 */
TEST(PosixTz, ARuleWhoseTwoChangeoversCoincideHasNoTransitions) {
  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_system(nullptr, nullptr, &db));
  const GCHRON_Zone * zone = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_zonedb_posix(db, "BST5CDT,M1.1.0/0,M1.1.0/1", &zone));

  GCHRON_Instant probe{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1781539200, 0, &probe));

  // The offset lookup says the zone is on standard time, always.
  GCHRON_ZoneInfo info{};
  ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(zone, probe, &info));
  EXPECT_EQ(-5 * 3600, info.offset_sec);
  EXPECT_FALSE(info.is_dst);

  // So the transition search must agree that there is nothing to find.
  GCHRON_Transition transition{};
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_zone_next_transition(zone, probe, &transition));
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_zone_prev_transition(zone, probe, &transition));

  gchron_zonedb_destroy(db);
}

// A rule string describes one recurring changeover applied to every year
// alike, so the transitions it implies must be findable in both directions
// and must agree with the offset lookup that sits beside them.
TEST(PosixTz, TransitionsFoundForwardAndBackAgreeWithTheOffsetLookup) {
  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_system(nullptr, nullptr, &db));
  const GCHRON_Zone * zone = nullptr;
  ASSERT_EQ(GCHRON_OK,
      gchron_zonedb_posix(db, "EST5EDT,M3.2.0,M11.1.0", &zone));

  GCHRON_Instant at{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1704067200, 0, &at));

  for (int i = 0; i < 12; ++i) {
    GCHRON_Transition forward{};
    ASSERT_EQ(GCHRON_OK, gchron_zone_next_transition(zone, at, &forward))
        << "step " << i;
    EXPECT_GT(forward.at.sec, at.sec);

    // The offset a moment before and a moment after must be the two sides the
    // transition reported.
    GCHRON_Instant just_before{};
    GCHRON_ZoneInfo before{};
    GCHRON_ZoneInfo after{};
    ASSERT_EQ(GCHRON_OK,
        gchron_instant_create(forward.at.sec - 1, 0, &just_before));
    ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(zone, just_before, &before));
    ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(zone, forward.at, &after));
    EXPECT_EQ(before.offset_sec, forward.before.offset_sec) << "step " << i;
    EXPECT_EQ(after.offset_sec, forward.after.offset_sec) << "step " << i;
    EXPECT_NE(before.offset_sec, after.offset_sec)
        << "a transition that changes nothing is not a transition";

    // And walking back from it lands on the same instant.
    GCHRON_Transition backward{};
    ASSERT_EQ(GCHRON_OK,
        gchron_zone_prev_transition(zone, forward.at, &backward));
    EXPECT_EQ(forward.at.sec, backward.at.sec) << "step " << i;

    at = forward.at;
  }
  gchron_zonedb_destroy(db);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
