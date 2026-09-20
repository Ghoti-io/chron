/**
 * @file
 *
 * The `leap-seconds.list` reader.
 *
 * A file the library parses is a file somebody can hand it, and this one
 * arrives from a package manager, an NTP distribution, or a download - so it
 * gets the same treatment as TZif (design.md section 1.2).
 *
 * The harness asserts **invariants**, not merely that nothing crashed: a
 * table that parses must be ordered, must be able to name its own expiry, and
 * must answer consistently about every row it holds. A reader that returned
 * GCHRON_OK on nonsense would survive a crash-only fuzzer indefinitely.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/chron.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  GCHRON_LeapTable * table = nullptr;
  GCHRON_Error err{};

  if (gchron_leap_table_parse(reinterpret_cast<const char *>(data), size,
          nullptr, &table, &err) != GCHRON_OK) {
    // A refusal must say why, and must not have left anything behind.
    assert(table == nullptr);
    assert(err.code != GCHRON_OK);
    return 0;
  }
  assert(table != nullptr);

  size_t count = gchron_leap_table_count(table);
  assert(count > 0);  // An empty table is refused, not returned.

  GCHRON_Instant expiry{};
  assert(gchron_leap_table_expiry(table, &expiry) == GCHRON_OK);

  GCHRON_LeapEntry previous{};
  for (size_t i = 0; i < count; ++i) {
    GCHRON_LeapEntry entry{};
    assert(gchron_leap_table_entry(table, i, &entry) == GCHRON_OK);

    if (i > 0) {
      // Strictly increasing: the lookup binary-searches this, and a table
      // that was not ordered would make it return the wrong row rather than
      // fail.
      assert(entry.at.sec > previous.at.sec);
      // `negative` is a statement about the previous row, so it must agree
      // with the two offsets rather than being independently settable.
      assert(entry.negative
          == (entry.tai_minus_utc < previous.tai_minus_utc));
    }
    else {
      // The first row is where the count began, never a leap second.
      assert(!entry.negative);
    }

    // Every row's own instant must resolve to that row's offset - one second
    // before it must not. This is the property that catches an off-by-one in
    // the search, which no vector file would.
    if (entry.at.sec < expiry.sec) {
      int32_t offset = 0;
      assert(gchron_leap_offset_at(table, entry.at, &offset) == GCHRON_OK);
      assert(offset == entry.tai_minus_utc);

      if (i > 0) {
        GCHRON_Instant before{ entry.at.sec - 1, 0 };
        int32_t earlier = 0;
        assert(gchron_leap_offset_at(table, before, &earlier) == GCHRON_OK);
        assert(earlier == previous.tai_minus_utc);
      }
    }
    previous = entry;
  }

  // Past the expiry the answer is EXPIRED, never a guess - unless the expiry
  // is itself before the table begins, in which case that instant is out of
  // range as well and RANGE is reported first. Both are refusals; which one
  // wins is a detail, and asserting the wrong one made this harness fail on a
  // file whose expiry had been corrupted into 1901.
  {
    int32_t offset = 0;
    GCHRON_Instant past{ expiry.sec, 0 };
    GCHRON_LeapEntry first{};
    assert(gchron_leap_table_entry(table, 0, &first) == GCHRON_OK);
    GCHRON_Result result = gchron_leap_offset_at(table, past, &offset);
    assert(result == (expiry.sec < first.at.sec ? GCHRON_ERR_RANGE
                                                : GCHRON_ERR_EXPIRED));
  }

  // Before the first row there is no integer offset to report.
  {
    GCHRON_LeapEntry first{};
    assert(gchron_leap_table_entry(table, 0, &first) == GCHRON_OK);
    if (first.at.sec > INT64_MIN) {
      int32_t offset = 0;
      GCHRON_Instant before{ first.at.sec - 1, 0 };
      assert(gchron_leap_offset_at(table, before, &offset)
          == GCHRON_ERR_RANGE);
    }
  }

  // And the round trip, wherever the table can answer at all.
  {
    GCHRON_LeapEntry first{};
    assert(gchron_leap_table_entry(table, 0, &first) == GCHRON_OK);
    for (int64_t probe : { first.at.sec, expiry.sec - 1 }) {
      GCHRON_Instant utc{ probe, 500000000 };
      GCHRON_TaiInstant tai{};
      // A refusal is fine - out of range, expired, or the second a negative
      // leap removed. What must not happen is a conversion that succeeds and
      // does not come back.
      if (gchron_tai_from_instant(table, utc, false, &tai) != GCHRON_OK) {
        continue;
      }
      GCHRON_Instant back{};
      bool leap = true;
      assert(gchron_tai_to_instant(table, tai, &back, &leap) == GCHRON_OK);
      assert(back.sec == utc.sec);
      assert(back.nsec == utc.nsec);
      assert(!leap);
    }
  }

  gchron_leap_table_destroy(table);
  return 0;
}
