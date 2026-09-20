/**
 * @file
 *
 * Result codes, diagnostics, units and limits.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstring>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

TEST(Core, EveryResultCodeHasItsOwnString) {
  std::set<std::string> seen;
  for (int i = 0; i < GCHRON_RESULT_COUNT; ++i) {
    const char * s = gchron_result_string(static_cast<GCHRON_Result>(i));
    ASSERT_NE(nullptr, s);
    EXPECT_STRNE("unknown result", s) << "result " << i;
    EXPECT_TRUE(seen.insert(s).second)
        << "result " << i << " shares its string with another";
  }
}

TEST(Core, ResultStringOutsideTheEnumSaysSo) {
  EXPECT_STREQ("unknown result", gchron_result_string(
      static_cast<GCHRON_Result>(GCHRON_RESULT_COUNT)));
  EXPECT_STREQ("unknown result",
      gchron_result_string(static_cast<GCHRON_Result>(-1)));
}

TEST(Core, EveryDiagnosticHasItsOwnString) {
  std::set<std::string> seen;
  for (int i = 0; i < GCHRON_DIAG_COUNT; ++i) {
    const char * s = gchron_diag_string(static_cast<GCHRON_Diag>(i));
    ASSERT_NE(nullptr, s);
    EXPECT_STRNE("unknown diagnostic", s) << "diag " << i;
    EXPECT_TRUE(seen.insert(s).second)
        << "diag " << i << " shares its string with another";
  }
}

TEST(Core, EveryUnitHasItsOwnString) {
  std::set<std::string> seen;
  for (int i = 0; i < GCHRON_UNIT_COUNT; ++i) {
    const char * s = gchron_unit_string(static_cast<GCHRON_Unit>(i));
    ASSERT_NE(nullptr, s);
    EXPECT_STRNE("unknown unit", s) << "unit " << i;
    EXPECT_TRUE(seen.insert(s).second);
  }
}

// design.md section 4.1: hours and below are SI; days and above depend on a
// calendar, and in a zone on the date. Which side of the line a unit falls on
// is what decides whether adding it to an instant is an error.
TEST(Core, HoursAndBelowAreExactAndDaysAndAboveAreNot) {
  EXPECT_TRUE(gchron_unit_is_exact(GCHRON_UNIT_NANOSECOND));
  EXPECT_TRUE(gchron_unit_is_exact(GCHRON_UNIT_SECOND));
  EXPECT_TRUE(gchron_unit_is_exact(GCHRON_UNIT_HOUR));
  EXPECT_FALSE(gchron_unit_is_exact(GCHRON_UNIT_DAY));
  EXPECT_FALSE(gchron_unit_is_exact(GCHRON_UNIT_WEEK));
  EXPECT_FALSE(gchron_unit_is_exact(GCHRON_UNIT_MONTH));
  EXPECT_FALSE(gchron_unit_is_exact(GCHRON_UNIT_YEAR));
  EXPECT_FALSE(gchron_unit_is_exact(GCHRON_UNIT_UNSPECIFIED));
}

// The ordering is load-bearing: a "largest unit" argument compares against it.
TEST(Core, UnitsAreOrderedSmallestToLargest) {
  EXPECT_LT(GCHRON_UNIT_NANOSECOND, GCHRON_UNIT_MICROSECOND);
  EXPECT_LT(GCHRON_UNIT_MICROSECOND, GCHRON_UNIT_MILLISECOND);
  EXPECT_LT(GCHRON_UNIT_MILLISECOND, GCHRON_UNIT_SECOND);
  EXPECT_LT(GCHRON_UNIT_SECOND, GCHRON_UNIT_MINUTE);
  EXPECT_LT(GCHRON_UNIT_MINUTE, GCHRON_UNIT_HOUR);
  EXPECT_LT(GCHRON_UNIT_HOUR, GCHRON_UNIT_DAY);
  EXPECT_LT(GCHRON_UNIT_DAY, GCHRON_UNIT_WEEK);
  EXPECT_LT(GCHRON_UNIT_WEEK, GCHRON_UNIT_MONTH);
  EXPECT_LT(GCHRON_UNIT_MONTH, GCHRON_UNIT_YEAR);
}

// A struct a caller zero-initialised and forgot a field of must not name a
// real unit; GCHRON_UNIT_UNSPECIFIED is what zero means.
TEST(Core, ZeroIsNotAUnit) {
  EXPECT_EQ(0, GCHRON_UNIT_UNSPECIFIED);
}

TEST(Core, LimitsDefaultFillsEveryField) {
  GCHRON_Limits limits;
  std::memset(&limits, 0xAA, sizeof(limits));
  gchron_limits_default(&limits);
  EXPECT_EQ(GCHRON_DEFAULT_MAX_PARSE_LENGTH, limits.max_parse_length);
}

TEST(Core, LimitsDefaultOnNullIsIgnored) {
  gchron_limits_default(nullptr);
}

TEST(Core, ErrorClearSaysNothingWentWrong) {
  GCHRON_Error err;
  std::memset(&err, 0xAA, sizeof(err));
  gchron_error_clear(&err);
  EXPECT_EQ(GCHRON_OK, err.code);
  EXPECT_EQ(GCHRON_DIAG_NONE, err.diag);
  EXPECT_EQ(0u, err.offset);
  EXPECT_EQ(0u, err.length);
  EXPECT_EQ(nullptr, err.message);
}

TEST(Version, ReportsWhatTheBuildSaysItIs) {
  ASSERT_NE(nullptr, gchron_version_string());
  EXPECT_EQ(GCHRON_VERSION_MAJOR, gchron_version_major());
  EXPECT_EQ(GCHRON_VERSION_MINOR, gchron_version_minor());
  EXPECT_EQ(GCHRON_VERSION_PATCH, gchron_version_patch());
  EXPECT_STREQ(GCHRON_VERSION_STRING, gchron_version_string());
}

TEST(Allocator, DefaultIsAvailable) {
  EXPECT_NE(nullptr, gchron_allocator_default());
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
