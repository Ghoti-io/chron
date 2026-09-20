/**
 * @file
 *
 * The TZif reader, on arbitrary bytes.
 *
 * design.md section 1.2 counts a TZif file among the three untrusted inputs:
 * it is a binary format read from disk, and a corrupt or hostile one must
 * produce GCHRON_ERR_CORRUPT or GCHRON_ERR_LIMIT, never a crash and never a
 * zone that silently answers wrongly.
 *
 * The harness does not stop at the parse. A file that *is* accepted is then
 * used - offsets asked for, transitions walked, civil readings converted -
 * because the second half of that promise is about what an accepted file
 * does, and a fuzzer that only checked the reader would never reach the
 * binary search or the footer hand-over with a table it had chosen itself.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <ghoti.io/chron/chron.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) {
    return 0;
  }

  const uint8_t options = data[0];
  GCHRON_Limits limits;
  gchron_limits_default(&limits);

  // The options byte drives the limits, so that one harness covers the reader
  // at every value rather than only at the default - which is where the
  // ERR_LIMIT paths are, and a limit nothing ever trips is a promise nothing
  // keeps.
  if (options & 0x01) {
    limits.max_tzif_bytes = (size_t)(options >> 4) * 64 + 1;
  }
  if (options & 0x02) {
    limits.max_transitions = (size_t)(options >> 5) * 8;
  }
  if (options & 0x04) {
    limits.max_zone_types = (size_t)(options >> 6) * 4;
  }

  GCHRON_ZoneDb * db = nullptr;
  if (gchron_zonedb_memory(data + 1, size - 1, "fuzz/zone", nullptr, &limits,
          &db) != GCHRON_OK) {
    return 0;
  }

  const GCHRON_Zone * zone = nullptr;
  if (gchron_zonedb_zone(db, "fuzz/zone", &zone) == GCHRON_OK) {
    // A lattice that straddles the epoch, the 2038 boundary where a "fat"
    // table ends, and the far future the footer governs.
    static const int64_t kProbes[] = {
      INT64_MIN / 2, -2208988800, -1, 0, 1, 951782400, 1541300400,
      2145916800, 4102444800, INT64_MAX / 2,
    };
    for (int64_t seconds : kProbes) {
      GCHRON_Instant instant{};
      if (gchron_instant_create(seconds, 0, &instant) != GCHRON_OK) {
        continue;
      }
      GCHRON_ZoneInfo info{};
      if (gchron_zone_offset_at(zone, instant, &info) == GCHRON_OK) {
        // The abbreviation is borrowed from the zone and must be a real
        // string: a designation index the reader failed to bound would show
        // up here rather than in the parse.
        volatile size_t length = std::strlen(info.abbreviation);
        (void)length;
      }
      GCHRON_Transition transition{};
      gchron_zone_next_transition(zone, instant, &transition);
      gchron_zone_prev_transition(zone, instant, &transition);

      GCHRON_ZonedDateTime zoned{};
      GCHRON_DateTime civil{};
      if (gchron_zoned_from_instant(instant, zone, &zoned) == GCHRON_OK) {
        gchron_zoned_to_civil(&zoned, &civil);
        gchron_zoned_start_of_day(&zoned, &zoned);
      }
    }

    // And the other direction, which is where the gap and overlap search is.
    GCHRON_DateTime civil{};
    if (gchron_date_create(2026, 3, 8, &civil.date) == GCHRON_OK
        && gchron_time_create(2, 30, 0, 0, &civil.time) == GCHRON_OK) {
      GCHRON_CivilOffsets offsets{};
      gchron_zone_offsets_for_civil(zone, civil, &offsets);
      for (int policy = 0; policy <= GCHRON_RESOLVE_COMPATIBLE; ++policy) {
        GCHRON_ZonedDateTime zoned{};
        gchron_zoned_from_civil(civil, zone,
            static_cast<GCHRON_Resolve>(policy), &zoned);
      }
    }
  }

  gchron_zonedb_destroy(db);
  return 0;
}
