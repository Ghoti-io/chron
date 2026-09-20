/**
 * @file
 *
 * The POSIX `TZ` footer grammar, on arbitrary text.
 *
 * The second of design.md section 1.2's untrusted inputs. It arrives in the
 * footer of every version-2 TZif file and in the `TZ` environment variable,
 * and on a `-b slim` system it is what answers every question about next
 * year - so a rule that parses into nonsense is worse than one that fails.
 *
 * A rule that parses is therefore evaluated, and its transitions walked in
 * both directions, with one invariant asserted: **a transition the rule
 * reports must actually change the offset**, and the offsets either side of
 * it must be what it said they were. A rule evaluator that returned the same
 * instant for ever, or one whose next-transition search disagreed with its
 * own offset lookup, would pass a fuzzer that only checked for crashes.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <ghoti.io/chron/chron.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size == 0 || size > 512) {
    return 0;
  }
  // NUL-terminated, because that is the shape gchron_zonedb_posix takes; the
  // grammar itself is bounded by length inside the parser.
  std::string rule(reinterpret_cast<const char *>(data), size);
  if (rule.find('\0') != std::string::npos) {
    return 0;
  }

  GCHRON_ZoneDb * db = nullptr;
  if (gchron_zonedb_system(nullptr, nullptr, &db) != GCHRON_OK) {
    return 0;
  }
  const GCHRON_Zone * zone = nullptr;
  if (gchron_zonedb_posix(db, rule.c_str(), &zone) != GCHRON_OK) {
    gchron_zonedb_destroy(db);
    return 0;
  }

  static const int64_t kProbes[] = {
    -2208988800, -1, 0, 951782400, 1541300400, 1793511000, 2145916800,
    4102444800,
  };
  for (int64_t seconds : kProbes) {
    GCHRON_Instant instant{};
    if (gchron_instant_create(seconds, 0, &instant) != GCHRON_OK) {
      continue;
    }
    GCHRON_ZoneInfo info{};
    if (gchron_zone_offset_at(zone, instant, &info) != GCHRON_OK) {
      continue;
    }
    volatile size_t length = std::strlen(info.abbreviation);
    (void)length;

    GCHRON_Transition forward{};
    if (gchron_zone_next_transition(zone, instant, &forward) == GCHRON_OK) {
      // It has to be in the future, and it has to change something.
      assert(forward.at.sec > instant.sec);
      assert(forward.before.offset_sec != forward.after.offset_sec
          || forward.before.is_dst != forward.after.is_dst);

      // And the offsets either side must be what the search claimed.
      GCHRON_Instant just_before{};
      GCHRON_ZoneInfo before{};
      GCHRON_ZoneInfo after{};
      if (gchron_instant_create(forward.at.sec - 1, 0, &just_before)
              == GCHRON_OK
          && gchron_zone_offset_at(zone, just_before, &before) == GCHRON_OK
          && gchron_zone_offset_at(zone, forward.at, &after) == GCHRON_OK) {
        assert(before.offset_sec == forward.before.offset_sec);
        assert(after.offset_sec == forward.after.offset_sec);
      }

      // Walking back from a transition lands on that same transition.
      GCHRON_Transition backward{};
      if (gchron_zone_prev_transition(zone, forward.at, &backward)
          == GCHRON_OK) {
        assert(backward.at.sec == forward.at.sec);
      }
    }
  }

  gchron_zonedb_destroy(db);
  return 0;
}
