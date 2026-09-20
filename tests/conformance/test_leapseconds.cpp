/**
 * @file
 *
 * leap.h against the tzdb's own `leapseconds` file.
 *
 * The built-in table is generated from `leap-seconds.list`, so checking it
 * against that file would prove only that the generator can read back what it
 * wrote. `leapseconds` is the other file the tzdb ships - the same facts in
 * zic's syntax, written by zic's maintainers, one leap second per line with
 * the sign spelled out. Agreeing with *that* is evidence.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/chron.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace {

struct Leap {
  int year;
  int month;
  int day;
  bool inserted;      ///< `+`; a `-` would be a removed second.
  int tai_minus_utc;  ///< After this leap second.
  int line;
};

struct Vectors {
  std::vector<Leap> leaps;
  int64_t expires = 0;
};

void load(Vectors & vectors) {
  std::string path = gchrontest::data_dir() + "/vectors/leap/leapseconds.vec";
  std::ifstream in(path);
  // Not a skip. A run that cannot find its vectors has a broken harness, and
  // a harness that skipped would report a green run for a question it never
  // asked (design.md section 12.3).
  EXPECT_TRUE(in.is_open())
      << "missing vector file: " << path
      << " (run tools/oracle/leapseconds.py)";

  std::string line;
  int number = 0;
  while (std::getline(in, line)) {
    ++number;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream fields(line);
    std::string kind;
    fields >> kind;
    if (kind == "expires") {
      fields >> vectors.expires;
      continue;
    }
    ASSERT_EQ("leap", kind) << "line " << number;

    std::string date;
    std::string sign;
    Leap leap{};
    fields >> date >> sign >> leap.tai_minus_utc;
    ASSERT_EQ(10u, date.size()) << "line " << number;
    leap.year = std::atoi(date.substr(0, 4).c_str());
    leap.month = std::atoi(date.substr(5, 2).c_str());
    leap.day = std::atoi(date.substr(8, 2).c_str());
    leap.inserted = (sign == "+");
    leap.line = number;
    vectors.leaps.push_back(leap);
  }
}

/** A date, or a failed assertion. */
GCHRON_Date date_of(const Leap & leap) {
  GCHRON_Date date{};
  EXPECT_EQ(GCHRON_OK,
      gchron_date_create(leap.year, leap.month, leap.day, &date));
  return date;
}

class LeapSeconds : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(load(vectors_));
    ASSERT_FALSE(vectors_.leaps.empty()) << "no vectors loaded";
    table_ = gchron_leap_table_builtin();
    ASSERT_NE(nullptr, table_);
  }

  Vectors vectors_;
  const GCHRON_LeapTable * table_ = nullptr;
};

TEST_F(LeapSeconds, EveryLeapSecondTheTzdbListsIsOneThisLibraryKnows) {
  size_t checked = 0;
  for (const Leap & leap : vectors_.leaps) {
    GCHRON_Date date = date_of(leap);
    bool is_leap = false;
    ASSERT_EQ(GCHRON_OK, gchron_leap_is_leap_day(table_, &date, &is_leap))
        << "line " << leap.line;
    EXPECT_TRUE(is_leap)
        << "the tzdb says a second was added at the end of " << leap.year
        << "-" << leap.month << "-" << leap.day
        << " and this library does not (line " << leap.line << ")";
    ++checked;
  }
  EXPECT_EQ(vectors_.leaps.size(), checked);
}

TEST_F(LeapSeconds, TheOffsetAfterEachLeapSecondIsWhatTheTzdbSays) {
  for (const Leap & leap : vectors_.leaps) {
    GCHRON_Date date = date_of(leap);
    int64_t epoch_day = 0;
    ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&date, &epoch_day));

    // Midnight *after* the leap second: the first instant under the new
    // offset.
    GCHRON_Instant after{ (epoch_day + 1) * 86400, 0 };
    int32_t offset = 0;
    ASSERT_EQ(GCHRON_OK, gchron_leap_offset_at(table_, after, &offset))
        << "line " << leap.line;
    EXPECT_EQ(leap.tai_minus_utc, offset) << "line " << leap.line;

    // And one second before it, the old offset still stood.
    GCHRON_Instant before{ (epoch_day + 1) * 86400 - 1, 0 };
    int32_t previous = 0;
    ASSERT_EQ(GCHRON_OK, gchron_leap_offset_at(table_, before, &previous));
    EXPECT_EQ(leap.tai_minus_utc - (leap.inserted ? 1 : -1), previous)
        << "line " << leap.line;
  }
}

TEST_F(LeapSeconds, NoOtherDayIsALeapDay) {
  // The complement of the vector list, which is the half a list of positives
  // cannot check on its own: a gchron_leap_is_leap_day that simply answered
  // "yes" would pass the test above.
  std::vector<int64_t> known;
  for (const Leap & leap : vectors_.leaps) {
    GCHRON_Date date = date_of(leap);
    int64_t day = 0;
    ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&date, &day));
    known.push_back(day);
  }

  GCHRON_Instant expiry{};
  ASSERT_EQ(GCHRON_OK, gchron_leap_table_expiry(table_, &expiry));

  GCHRON_Date first = date_of(vectors_.leaps.front());
  int64_t start = 0;
  ASSERT_EQ(GCHRON_OK, gchron_date_to_epoch_day(&first, &start));
  int64_t stop = expiry.sec / 86400 - 1;

  size_t positives = 0;
  size_t checked = 0;
  for (int64_t day = start; day < stop; ++day) {
    GCHRON_Date date{};
    ASSERT_EQ(GCHRON_OK, gchron_date_from_epoch_day(day, &date));
    bool is_leap = false;
    ASSERT_EQ(GCHRON_OK, gchron_leap_is_leap_day(table_, &date, &is_leap));
    bool expected =
        std::find(known.begin(), known.end(), day) != known.end();
    ASSERT_EQ(expected, is_leap)
        << "epoch day " << day << " (" << date.year << "-"
        << static_cast<int>(date.month) << "-" << static_cast<int>(date.day)
        << ")";
    positives += is_leap ? 1 : 0;
    ++checked;
  }
  EXPECT_EQ(vectors_.leaps.size(), positives);
  EXPECT_GT(checked, 15000u) << "the sweep stopped covering days";
}

TEST_F(LeapSeconds, TheExpiryIsTheOneTheTzdbPublishes) {
  GCHRON_Instant expiry{};
  ASSERT_EQ(GCHRON_OK, gchron_leap_table_expiry(table_, &expiry));
  EXPECT_EQ(vectors_.expires, expiry.sec);
}

TEST_F(LeapSeconds, TheFileOnThisMachineAgreesWithTheBuiltInTable) {
  // The generated table and a fresh parse of leap-seconds.list must hold the
  // same rows. This is the one check that *is* against the generator's own
  // source, and it is here to catch a stale generated file rather than to
  // prove the data right.
  GCHRON_LeapTable * loaded = nullptr;
  GCHRON_Result result =
      gchron_leap_table_file(nullptr, nullptr, &loaded, nullptr);
  if (result == GCHRON_ERR_IO) {
    // Counted and reported, never silent: this machine has no copy.
    GTEST_SKIP() << "no leap-seconds.list on this machine; the built-in "
                    "table was checked against the tzdb vectors above";
  }
  ASSERT_EQ(GCHRON_OK, result);
  ASSERT_NE(nullptr, loaded);

  EXPECT_EQ(gchron_leap_table_count(table_), gchron_leap_table_count(loaded))
      << "src/leap/leap_builtin.c is stale; run tools/leap/embed.py";
  size_t shared = gchron_leap_table_count(table_);
  if (gchron_leap_table_count(loaded) < shared) {
    shared = gchron_leap_table_count(loaded);
  }
  for (size_t i = 0; i < shared; ++i) {
    GCHRON_LeapEntry a{};
    GCHRON_LeapEntry b{};
    ASSERT_EQ(GCHRON_OK, gchron_leap_table_entry(table_, i, &a));
    ASSERT_EQ(GCHRON_OK, gchron_leap_table_entry(loaded, i, &b));
    EXPECT_EQ(a.at.sec, b.at.sec) << "row " << i;
    EXPECT_EQ(a.tai_minus_utc, b.tai_minus_utc) << "row " << i;
    EXPECT_EQ(a.negative, b.negative) << "row " << i;
  }
  gchron_leap_table_destroy(loaded);
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
