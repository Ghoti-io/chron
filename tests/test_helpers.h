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

/**
 * An allocator that grants a fixed number of allocations and then fails every
 * one after them.
 *
 * The library's out-of-memory branches are unreachable from any input: they
 * are taken when the allocator says no, and the default allocator does not.
 * Sweeping the grant count over a compile walks the failure one allocation
 * further in each time, which reaches those branches and - because the suite
 * runs under Valgrind - also checks that a half-built structure is released
 * rather than abandoned.
 *
 * `free_fn` always frees, so a sweep leaks nothing whichever allocation was
 * refused. A refused `realloc` leaves the original block intact, as the C
 * library's does, because the caller still owns it.
 */
class FailingAllocator {
public:
  explicit FailingAllocator(int grants) : grants_(grants) {
    allocator_.ctx = this;
    allocator_.malloc_fn = &FailingAllocator::do_malloc;
    allocator_.calloc_fn = &FailingAllocator::do_calloc;
    allocator_.realloc_fn = &FailingAllocator::do_realloc;
    allocator_.free_fn = &FailingAllocator::do_free;
  }

  FailingAllocator(const FailingAllocator &) = delete;
  FailingAllocator & operator=(const FailingAllocator &) = delete;

  const GCHRON_Allocator * get() const { return &allocator_; }

  /** How many allocations were actually granted before the refusals began. */
  int granted() const { return granted_; }

private:
  bool grant() {
    if (granted_ >= grants_) {
      return false;
    }
    granted_ += 1;
    return true;
  }

  static void * do_malloc(void * ctx, size_t size) {
    FailingAllocator * self = static_cast<FailingAllocator *>(ctx);
    return self->grant() ? std::malloc(size) : nullptr;
  }

  static void * do_calloc(void * ctx, size_t nitems, size_t size) {
    FailingAllocator * self = static_cast<FailingAllocator *>(ctx);
    return self->grant() ? std::calloc(nitems, size) : nullptr;
  }

  static void * do_realloc(void * ctx, void * ptr, size_t size) {
    FailingAllocator * self = static_cast<FailingAllocator *>(ctx);
    return self->grant() ? std::realloc(ptr, size) : nullptr;
  }

  static void do_free(void *, void * ptr) { std::free(ptr); }

  GCHRON_Allocator allocator_{};
  int grants_;
  int granted_ = 0;
};

} // namespace gchrontest

#endif // GHOTI_IO_GCHRON_TEST_HELPERS_H
