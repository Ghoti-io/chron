/**
 * @file
 *
 * Shared helpers for the Ghoti.io Chron tests.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_TEST_HELPERS_H
#define GHOTI_IO_GCHRON_TEST_HELPERS_H

#include <cstdlib>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

namespace gchrontest {

/**
 * Directory holding the checked-in vectors. The Makefile bakes in
 * GCHRON_TEST_DATA so the binaries can run from the build tree; the
 * environment variable wins when it is set, and there is a relative fallback
 * for a manual build.
 */
inline std::string data_dir() {
  const char * env = std::getenv("GCHRON_TEST_DATA");
  if (env) {
    return std::string(env);
  }
#ifdef GCHRON_TEST_DATA
  return std::string(GCHRON_TEST_DATA);
#else
  return std::string("tests/data");
#endif
}

/**
 * How many years the exhaustive civil sweep covers, either side of year 0.
 *
 * design.md section 12 asks for +/-100,000 years, which is 73 million days
 * and a few seconds of an ordinary run. Under Valgrind that is an hour, so
 * GCHRON_CIVIL_SWEEP_YEARS narrows it - and the test **says how far it swept
 * in its own output** rather than skipping, so a narrowed run is visible
 * rather than silently green.
 */
inline int sweep_years() {
  const char * env = std::getenv("GCHRON_CIVIL_SWEEP_YEARS");
  if (env) {
    long value = std::strtol(env, nullptr, 10);
    if (value > 0 && value <= 1000000) {
      return static_cast<int>(value);
    }
  }
  return 100000;
}

/** A date, for a test that does not care whether construction can fail. */
inline GCHRON_Date date(int32_t year, int month, int day) {
  GCHRON_Date d{};
  EXPECT_EQ(GCHRON_OK, gchron_date_create(year, month, day, &d));
  return d;
}

/** A time of day, likewise. */
inline GCHRON_Time timeofday(int hour, int minute, int second,
    int32_t nsec = 0) {
  GCHRON_Time t{};
  EXPECT_EQ(GCHRON_OK, gchron_time_create(hour, minute, second, nsec, &t));
  return t;
}

/** A civil date-time, likewise. */
inline GCHRON_DateTime datetime(int32_t year, int month, int day, int hour,
    int minute, int second, int32_t nsec = 0) {
  GCHRON_DateTime dt{};
  dt.date = date(year, month, day);
  dt.time = timeofday(hour, minute, second, nsec);
  return dt;
}

} // namespace gchrontest

#endif // GHOTI_IO_GCHRON_TEST_HELPERS_H
