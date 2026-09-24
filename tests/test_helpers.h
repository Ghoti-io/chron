/**
 * @file
 *
 * Shared helpers for the Ghoti.io Chron tests.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_TEST_HELPERS_H
#define GHOTI_IO_GCHRON_TEST_HELPERS_H

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#ifdef _WIN32
#include <ghoti.io/cutil/dir.h>
#endif

namespace gchrontest {

/**
 * Set an environment variable for the library under test to read.
 *
 * The library reads `TZ` and `TZDIR` with getenv(), so on Windows the value
 * has to reach the C runtime's copy of the environment, which _putenv_s()
 * updates and SetEnvironmentVariable() does not. The C runtime cannot hold a
 * variable set to the empty string - _putenv_s() with "" removes it - so on
 * Windows an empty value reads as unset.
 */
inline void set_env(const char * name, const char * value) {
#ifdef _WIN32
  EXPECT_EQ(0, ::_putenv_s(name, value)) << name;
#else
  EXPECT_EQ(0, ::setenv(name, value, 1)) << name;
#endif
}

/** Remove an environment variable, as set_env() would see it. */
inline void unset_env(const char * name) {
#ifdef _WIN32
  EXPECT_EQ(0, ::_putenv_s(name, "")) << name;
#else
  EXPECT_EQ(0, ::unsetenv(name)) << name;
#endif
}

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
 * A directory of its own, removed when it goes out of scope.
 *
 * For the tests that have to ask what happens when a file is *there* and
 * cannot be used - malformed, or unreadable - which no fixture checked into
 * the repository can be, because git does not carry a mode that would make
 * one unreadable and a checkout would fix it if it did.
 */
class TempDir {
public:
  TempDir() {
#ifdef _WIN32
    // There is no /tmp, and no mkdtemp; cutil makes the directory in the
    // system's temporary directory instead, with the same exclusivity.
    char * made = nullptr;
    EXPECT_EQ(GCU_FILE_OK,
        gcu_dir_temp_create(nullptr, "gchrontest", nullptr, &made))
        << "could not make a temporary directory";
    if (made != nullptr) {
      path_ = made;
      gcu_dir_free_path(nullptr, made);
    }
#else
    char pattern[] = "/tmp/gchrontestXXXXXX";
    const char * made = ::mkdtemp(pattern);
    EXPECT_NE(nullptr, made) << "could not make a temporary directory";
    if (made != nullptr) {
      path_ = made;
    }
#endif
  }
  ~TempDir() {
    if (path_.empty()) {
      return;
    }
    // Whatever mode a test left a file in, it has to be removable.
    for (const std::string & f : files_) {
      ::chmod(f.c_str(), 0600);
      ::unlink(f.c_str());
    }
    ::rmdir(path_.c_str());
  }
  TempDir(const TempDir &) = delete;
  TempDir & operator=(const TempDir &) = delete;

  const std::string & path() const { return path_; }

  /** Write a file into it, and remember it for the cleanup. */
  std::string write(const char * name, const std::string & content,
      int mode = 0600) {
    std::string full = path_ + "/" + name;
    files_.push_back(full);
    std::FILE * f = std::fopen(full.c_str(), "wb");
    EXPECT_NE(nullptr, f) << full;
    if (f != nullptr) {
      if (!content.empty()) {
        EXPECT_EQ(content.size(),
            std::fwrite(content.data(), 1, content.size(), f));
      }
      std::fclose(f);
      EXPECT_EQ(0, ::chmod(full.c_str(), static_cast<mode_t>(mode))) << full;
    }
    return full;
  }

private:
  std::string path_;
  std::vector<std::string> files_;
};

/**
 * The system zone database, released when it goes out of scope.
 *
 * A test that needs a zone for one assertion should not have to carry a
 * fixture for it. Construction asserts rather than skipping: a machine with
 * no zoneinfo cannot answer these questions, and a green run that never asked
 * them is the thing design.md section 12 exists to prevent.
 */
class ZoneDb {
public:
  ZoneDb() {
    EXPECT_EQ(GCHRON_OK, gchron_zonedb_system(nullptr, nullptr, &db_))
        << "no system zoneinfo directory";
  }
  ~ZoneDb() { gchron_zonedb_destroy(db_); }
  ZoneDb(const ZoneDb &) = delete;
  ZoneDb & operator=(const ZoneDb &) = delete;

  GCHRON_ZoneDb * get() const { return db_; }

private:
  GCHRON_ZoneDb * db_ = nullptr;
};

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
/**
 * An allocator that grants everything and remembers the largest single
 * request.
 *
 * For asserting a *bound* rather than a status. A cap on how many bytes a
 * reader will read cannot be observed from the return code - the call fails
 * with GCHRON_ERR_LIMIT whether the cap stopped the read or a later check
 * rejected what the read had already pulled into memory - and the difference
 * is the whole point of the cap. The largest request is what separates them.
 */
class RecordingAllocator {
public:
  RecordingAllocator() {
    allocator_.ctx = this;
    allocator_.malloc_fn = &RecordingAllocator::do_malloc;
    allocator_.calloc_fn = &RecordingAllocator::do_calloc;
    allocator_.realloc_fn = &RecordingAllocator::do_realloc;
    allocator_.free_fn = &RecordingAllocator::do_free;
  }

  RecordingAllocator(const RecordingAllocator &) = delete;
  RecordingAllocator & operator=(const RecordingAllocator &) = delete;

  const GCHRON_Allocator * get() const { return &allocator_; }

  /** The largest number of bytes asked for in one call. */
  size_t largest() const { return largest_; }

private:
  void note(size_t size) {
    if (size > largest_) {
      largest_ = size;
    }
  }

  static void * do_malloc(void * ctx, size_t size) {
    RecordingAllocator * self = static_cast<RecordingAllocator *>(ctx);
    self->note(size);
    return std::malloc(size);
  }

  static void * do_calloc(void * ctx, size_t n, size_t size) {
    RecordingAllocator * self = static_cast<RecordingAllocator *>(ctx);
    self->note(n * size);
    return std::calloc(n, size);
  }

  static void * do_realloc(void * ctx, void * ptr, size_t size) {
    RecordingAllocator * self = static_cast<RecordingAllocator *>(ctx);
    self->note(size);
    return std::realloc(ptr, size);
  }

  static void do_free(void *, void * ptr) { std::free(ptr); }

  GCHRON_Allocator allocator_{};
  size_t largest_ = 0;
};

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
