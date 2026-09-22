/**
 * @file
 *
 * The four text grammars no other harness reached: RFC 9557, HTTP-date,
 * RFC 5322, and the YAML timestamp.
 *
 * Every one of them reads text somebody else wrote - a timestamp out of an
 * HTTP header, an email, a YAML document, an interchange format - which is
 * design.md section 1.2's definition of untrusted. `fuzz_parse` covers RFC
 * 3339 and TOML and stops there; these four were written in phase 3 and
 * phase 1 and were never added to it.
 *
 * Each grammar here has a **writer**, so the harness asserts the round trip
 * rather than only surviving the read: a value this library produced from
 * text, written back out, must read the same way again. That is the property
 * that catches a reader and a writer disagreeing about a spelling, which no
 * amount of not-crashing would show. See design.md section 8.7.
 *
 * **The clock is fixed, and has to be.** HTTP-date's RFC 850 form and RFC
 * 5322's obsolete two-digit years are resolved against "now", so a harness
 * on the system clock would give a different answer tomorrow: a corpus entry
 * that reproduces a crash today would stop reproducing it, and the crash
 * would look fixed. Mistake M18 is the library deciding "now" for a caller;
 * this is the same mistake inside a test.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/chron.h>
#include <ghoti.io/chron/clock.h>
#include <ghoti.io/chron/format.h>
#include <ghoti.io/chron/parse.h>
#include <ghoti.io/chron/zone.h>
#include <ghoti.io/chron/zoned.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

/** 2026-01-01T00:00:00Z, so a two-digit year resolves the same way forever. */
GCHRON_FixedClock make_clock() {
  GCHRON_FixedClock clock{};
  GCHRON_Instant at{};
  at.sec = INT64_C(1767225600);
  at.nsec = 0;
  gchron_clock_fixed(at, &clock);
  return clock;
}

/** An offset date-time round-trips through its own grammar. */
void roundtrip_offset(const GCHRON_OffsetDateTime * odt, bool http) {
  char text[64];
  size_t length = 0;
  const GCHRON_Result written = http
      ? gchron_write_http_date(odt, text, sizeof(text), &length)
      : gchron_write_rfc5322(odt, text, sizeof(text), &length);
  if (written != GCHRON_OK) {
    return;
  }
  assert(length < sizeof(text));

  /*
   * Re-read with the same fixed clock. An IMF-fixdate and an RFC 5322 date
   * both carry a four-digit year, so this leg does not depend on the clock -
   * but passing it keeps the two reads identical in every respect except the
   * text, which is what makes a difference between them mean something.
   */
  GCHRON_FixedClock clock = make_clock();
  GCHRON_OffsetDateTime again{};
  const GCHRON_Result reread = http
      ? gchron_parse_http_date(text, length, &clock.clock, nullptr, &again,
            nullptr, nullptr)
      : gchron_parse_rfc5322(text, length, &clock.clock, nullptr, &again,
            nullptr, nullptr);

  // What this library wrote, this library reads.
  assert(reread == GCHRON_OK);
  assert(gchron_offset_identical(odt, &again));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) {
    return 0;
  }

  const uint8_t selector = data[0];
  const char * text = reinterpret_cast<const char *>(data + 1);
  const size_t length = size - 1;

  GCHRON_FixedClock clock = make_clock();

  switch (selector & 3) {
    case 0: {
      /*
       * RFC 9557. A zone database is needed for the `[Zone]` annotation to
       * mean anything; without one every bracketed name is unresolvable and
       * the annotation grammar is never exercised past its first character.
       */
      GCHRON_ZoneDb * db = nullptr;
      if (gchron_zonedb_system(nullptr, nullptr, &db) != GCHRON_OK) {
        return 0;
      }
      GCHRON_ZonedDateTime zoned{};
      if (gchron_parse_rfc9557(text, length, db, nullptr, &zoned, nullptr,
              nullptr) == GCHRON_OK) {
        char out[GCHRON_RFC9557_MAX];
        size_t written = 0;
        if (gchron_write_rfc9557(&zoned, nullptr, out, sizeof(out), &written)
            == GCHRON_OK) {
          assert(written < sizeof(out));
          GCHRON_ZonedDateTime again{};
          assert(gchron_parse_rfc9557(out, written, db, nullptr, &again,
                     nullptr, nullptr) == GCHRON_OK);
          assert(gchron_zoned_identical(&zoned, &again));
        }
      }
      gchron_zonedb_destroy(db);
      break;
    }

    case 1: {
      GCHRON_OffsetDateTime odt{};
      if (gchron_parse_http_date(text, length, &clock.clock, nullptr, &odt,
              nullptr, nullptr) == GCHRON_OK) {
        roundtrip_offset(&odt, true);
      }
      break;
    }

    case 2: {
      GCHRON_OffsetDateTime odt{};
      if (gchron_parse_rfc5322(text, length, &clock.clock, nullptr, &odt,
              nullptr, nullptr) == GCHRON_OK) {
        roundtrip_offset(&odt, false);
      }
      break;
    }

    default: {
      GCHRON_YamlValue value{};
      if (gchron_parse_yaml_timestamp(text, length, nullptr, &value, nullptr,
              nullptr) == GCHRON_OK) {
        char out[64];
        size_t written = 0;
        if (gchron_write_yaml_timestamp(&value, nullptr, out, sizeof(out),
                &written) == GCHRON_OK) {
          assert(written < sizeof(out));
          GCHRON_YamlValue again{};
          assert(gchron_parse_yaml_timestamp(out, written, nullptr, &again,
                     nullptr, nullptr) == GCHRON_OK);
          /*
           * `identical` rather than an equality of instants: a YAML timestamp
           * records what it *said* as well as what it meant, and a writer
           * that dropped the distinction would still produce the same moment.
           */
          assert(gchron_yaml_timestamp_identical(&value, &again));
        }
      }
      break;
    }
  }
  return 0;
}
