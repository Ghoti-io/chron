/**
 * @file
 *
 * The directory zone database, and the `tzdata.zi` link table it folds in.
 *
 * design.md section 1.2 treats any file the library can be handed as
 * untrusted, and this was the last reader with no harness at all. It is the
 * one that most wanted one: `build_links()` is hand-written pointer
 * arithmetic that rewrites the file in place into NUL-terminated fields,
 * walks it twice, and sorts two arrays in parallel by hand - and every other
 * reader of comparable shape (TZif, `leap-seconds.list`, the POSIX TZ footer)
 * has been fuzzed since the phase that wrote it.
 *
 * `gchron_zonedb_directory()` takes a path rather than bytes, so unlike the
 * other harnesses this one has to put the input on a filesystem. The cost is
 * a few hundred executions a second rather than a few hundred thousand, which
 * is the price of the only entry point the library offers.
 *
 * What it asserts beyond "did not crash":
 *
 * 1. **Determinism.** The same directory read twice gives the same list, in
 *    the same order. The parser rewrites its own buffer and sorts two arrays
 *    together by hand; either could be made to depend on memory it did not
 *    initialise, and a listing that changed between two reads of one
 *    unchanged file would be exactly that.
 * 2. **Consistency.** Asking for the same identifier twice answers the same
 *    way. The second call takes a different path through the cache.
 * 3. **Well-formedness.** Every identifier handed back is a non-empty C
 *    string the caller can use.
 *
 * It deliberately does *not* assert that everything listed can be opened.
 * That property holds for a real zoneinfo directory and is checked against
 * one in tests/unit/test_embedded.cpp, but it is not true here by
 * construction: the fuzzer can write a link naming a target that does not
 * exist, and a directory of arbitrary files contains no valid TZif images
 * anyway. Asserting it would report the harness's own fixture as a library
 * defect.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/chron.h>
#include <ghoti.io/chron/zone.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

/**
 * A directory of this process's own, made once and reused.
 *
 * Made once because mkdtemp() per execution would measure the filesystem
 * rather than the parser. Removed at exit so a campaign does not leave one
 * temporary directory per run behind.
 */
class Sandbox {
public:
  Sandbox() {
    char pattern[] = "/tmp/gchronfuzzXXXXXX";
    const char * made = ::mkdtemp(pattern);
    if (made != nullptr) {
      path_ = made;
    }
  }

  ~Sandbox() {
    if (path_.empty()) {
      return;
    }
    for (const std::string & name : written_) {
      ::unlink((path_ + "/" + name).c_str());
    }
    ::rmdir(path_.c_str());
  }

  const std::string & path() const { return path_; }

  void write(const char * name, const void * data, size_t size) {
    const std::string full = path_ + "/" + name;
    if (std::find(written_.begin(), written_.end(), std::string(name))
        == written_.end()) {
      written_.push_back(name);
    }
    std::FILE * f = std::fopen(full.c_str(), "wb");
    if (f == nullptr) {
      return;
    }
    if (size != 0) {
      (void)std::fwrite(data, 1, size, f);
    }
    std::fclose(f);
  }

private:
  std::string path_;
  std::vector<std::string> written_;
};

/** The listing, copied out so two reads can be compared. */
std::vector<std::string> listing(GCHRON_ZoneDb * db) {
  std::vector<std::string> out;
  const char * const * ids = nullptr;
  size_t count = 0;

  if (gchron_zonedb_list(db, &ids, &count) != GCHRON_OK) {
    return out;
  }
  assert(count == 0 || ids != nullptr);
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    // Every identifier is a string the caller can use: not null, not empty.
    assert(ids[i] != nullptr);
    assert(ids[i][0] != '\0');
    out.push_back(ids[i]);
  }
  return out;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  static Sandbox sandbox;
  if (sandbox.path().empty() || size < 1) {
    return 0;
  }

  /*
   * One byte of options, the rest is the file. The cap is driven from the
   * input for the reason fuzz_tzif gives: the ERR_LIMIT paths are only
   * reachable at a limit something actually trips, and a limit nothing trips
   * is a promise nothing keeps.
   */
  const uint8_t options = data[0];
  const uint8_t * body = data + 1;
  const size_t body_size = size - 1;

  GCHRON_Limits limits;
  gchron_limits_default(&limits);
  if (options & 1) {
    limits.max_tzif_bytes = 64;
  }
  if (options & 2) {
    limits.max_zones = 4;
  }

  sandbox.write("tzdata.zi", body, body_size);
  /*
   * A file for a link to point at. Its contents are not a TZif image, so
   * opening it fails in the parser - which is the point: the link has to be
   * *followed* to fail there, and a name that was never resolved fails
   * earlier and differently.
   */
  static const char kTarget[] = "not a TZif image";
  sandbox.write("RealZone", kTarget, sizeof(kTarget) - 1);

  GCHRON_ZoneDb * first = nullptr;
  if (gchron_zonedb_directory(sandbox.path().c_str(), nullptr, &limits,
          &first) != GCHRON_OK) {
    assert(first == nullptr);
    return 0;
  }
  assert(first != nullptr);

  const std::vector<std::string> ids = listing(first);

  // Property 2: the same question, twice, through two different paths.
  for (const std::string & id : ids) {
    const GCHRON_Zone * a = nullptr;
    const GCHRON_Zone * b = nullptr;
    const GCHRON_Result ra = gchron_zonedb_zone(first, id.c_str(), &a);
    const GCHRON_Result rb = gchron_zonedb_zone(first, id.c_str(), &b);
    assert(ra == rb);
    if (ra == GCHRON_OK) {
      assert(a != nullptr);
      assert(gchron_zone_id(a) != nullptr);
    }
    else {
      assert(a == nullptr);
    }
  }

  // Property 1: the same directory, read again, says the same thing.
  GCHRON_ZoneDb * second = nullptr;
  if (gchron_zonedb_directory(sandbox.path().c_str(), nullptr, &limits,
          &second) == GCHRON_OK) {
    const std::vector<std::string> again = listing(second);
    assert(ids == again);
    gchron_zonedb_destroy(second);
  }

  gchron_zonedb_destroy(first);
  return 0;
}
