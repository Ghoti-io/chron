# Ghoti.io Chron

Instants, civil dates and times, calendars, durations, time zones, and the
text formats for all of them.

## Formats

This is what the library implements.

- Civil arithmetic, and the Gregorian, Julian, hybrid and tabular calendars.
- Instants, offsets and durations.
- RFC 3339, TOML, RFC 9557 and ISO 8601.
- Time zones from TZif and POSIX `TZ`.
- Formatting through LDML patterns and `strftime`.

## Before you call it

- Terms:
  - A `GCHRON_Instant` is a point on the timeline (absolute, like a timestamp).
  - A `GCHRON_DateTime` is a reading on a wall clock. Going from the reading to the instant needs a zone and a policy, because the reading may name no instant, or two.
- Every policy enum has `REJECT` as its zero value. A zero-initialised options struct refuses the ambiguous case. `NULL` options mean `gchron_parse_options_default()`. The other presets are `gchron_parse_options_json_schema()` and `gchron_parse_options_toml()`.
- An error carries a static message and a byte offset into the input. Nothing is allocated for it.
- Dates, calendars, durations, instants and the RFC 3339 and TOML grammars need no zoneinfo file. Named zones do. `make check-layering` fails the build if a header that needs no data file includes one that does.

| Standard | What it means here |
| --- | --- |
| RFC 3339 | Parsed and formatted in both directions. |
| TOML | Local dates and times, through `gchron_parse_options_toml()`. |
| RFC 9557 | The zoned form. Needs TZif files. |
| ISO 8601 | The grammars in [documentation/text-formats.md](documentation/text-formats.md). |
| LDML patterns, `strftime` | Month and day names, through `format.h`. |

| Type | What it means here |
| --- | --- |
| `GCHRON_Instant` | Seconds and nanoseconds since 1970. Every day is 86,400 seconds; leap seconds are not in the count. |
| `GCHRON_Date`, `GCHRON_Time`, `GCHRON_DateTime` | A civil reading, with no zone. Months are 1–12. Year 0 is 1 BCE. Monday is 1. |
| `GCHRON_OffsetDateTime` | A civil reading and the offset it was written with. `-00:00` means the offset is unknown, which is a different statement from `Z`. |
| `GCHRON_Duration` | Calendar units and exact units together. Adding a month to an instant is an error: a month has no length in seconds. |
| `GCHRON_Interval` | Half-open `[start, end)`. |

## Examples

```c
#include <ghoti.io/chron/chron.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  const char * text = "1985-04-12T23:20:50.52Z";
  GCHRON_OffsetDateTime when;
  GCHRON_Error error;

  if (gchron_parse_rfc3339_date_time(text, strlen(text), NULL, &when, NULL,
          &error) != GCHRON_OK) {
    fprintf(stderr, "%s at byte %zu\n", error.message, error.offset);
    return 1;
  }

  GCHRON_Instant instant;
  gchron_offset_to_instant(&when, &instant);
  printf("%lld.%09d\n", (long long)instant.sec, instant.nsec);
  return 0;
}
```

```
482196050.520000000
```

In a zone, the conversion is the call that can fail. 02:30 on the morning
the clocks spring forward never happened, and `GCHRON_RESOLVE_REJECT`
refuses it:

```c
GCHRON_ZoneDb * db;
const GCHRON_Zone * zone;
GCHRON_ZonedDateTime zoned;

gchron_zonedb_default(NULL, NULL, &db);
gchron_zonedb_zone(db, "America/New_York", &zone);

GCHRON_DateTime civil = { { 2026, 3, 8 }, { 2, 30, 0, 0 } };
if (gchron_zoned_from_civil(civil, zone, GCHRON_RESOLVE_REJECT, &zoned)
    == GCHRON_ERR_GAP) {
  GCHRON_CivilOffsets options;
  gchron_zone_offsets_for_civil(zone, civil, &options);
  printf("that reading did not occur; the gap is %d seconds\n",
      options.gap_seconds);
}

gchron_zonedb_destroy(db);
```

```
that reading did not occur; the gap is 3600 seconds
```

`GCHRON_RESOLVE_EARLIER`, `LATER` and `COMPATIBLE` are the three ways to ask
for an answer anyway. `examples/timestamp.c` prints everything the library
knows about one RFC 3339 timestamp.

## Compile and link

Once the library is installed, pkg-config carries the include path, the
library, and its dependencies:

```bash
cc -o show show.c $(pkg-config --cflags --libs ghoti.io-chron-0)
```

The module name ends in the major version, `-0` for this release, so two
majors can be installed side by side. A build made with `make BRANCH=-dev`
installs `ghoti.io-chron-dev` instead.

## Building the library

[cutil](https://github.com/Ghoti-io/cutil) must already be installed where
pkg-config can see it. A dependency it cannot find is a hard error naming
the fix.

```bash
make
make test
sudo make install
```

From the parent of a suite checkout:

```bash
./suite/install.sh
export PKG_CONFIG_PATH="$PWD/.local/share/pkgconfig"
make -C libs/chron test PREFIX="$PWD/.local"
```

`make test` is the suite. `make help` lists the rest, including
`make test-asan` and `make test-valgrind`. The differentials against outside
implementations are not part of `make test`.

| Target | What it does |
| --- | --- |
| `make examples` | `examples/timestamp.c` |
| `make fuzz` | The text, arithmetic, duration, TZif and `TZ` harnesses |
| `make docs` | The Doxygen manual, into `./docs` |

## The API

Everything is prefixed `gchron_` / `GCHRON_`, under `<ghoti.io/chron/...>`.
`<ghoti.io/chron/chron.h>` is the umbrella.

What each group of headers needs:

| Headers | Holds |
| --- | --- |
| `core.h`, `civil.h`, `calendar.h`, `duration.h` | results, dates, times, the Gregorian, Julian, hybrid and tabular calendars, durations. No data file |
| `instant.h`, `offset.h`, `parse.h` | instants, offsets, RFC 3339 and TOML in both directions. No data file |
| `zone.h`, `zoned.h` | named zones, gaps and overlaps, RFC 9557. Needs TZif files |
| `format.h` | LDML patterns, `strftime`, month and day names |

`clock.h` and `interop.h` are the clocks and the foreign representations
(ASN.1 time, `struct timeval`, Windows `SYSTEMTIME`). `leap.h` is leap
seconds and TAI; nothing else includes it, so a program that does not
convert to TAI links none of it.

A calendar is a small public vtable. A calendar with fixed month lengths and
a cyclic leap rule needs no code, only a filled-in `GCHRON_TabularCalendar`.
The Gregorian and Julian calendars are closed-form algorithms, and the
tabular engine is checked against them.

[Formats](#formats) is what is implemented.
[Before you call it](#before-you-call-it) is what that changes about a call.

## Dependencies

Found through pkg-config, and the installed `.pc` file names it, so a
program that links `ghoti.io-chron-0` links this too.

- [ghoti.io-cutil](https://github.com/Ghoti-io/cutil) — the allocator.

## Documentation

| Page | What it settles |
| --- | --- |
| [documentation/design.md](documentation/design.md) | The design |
| [documentation/text-formats.md](documentation/text-formats.md) | The grammars |

`make docs` builds the manual.

## Status

The formats above are built. The embedded zone database is generated at
build time from the machine's zoneinfo tree.

The Windows zone-name mapping is generated and committed. It has not been
run on Windows.

## License

LGPL-3.0-only. See [COPYING.LESSER](COPYING.LESSER) for the license, and
[COPYING](COPYING) for the GPL text it is written as additional permissions
on top of.

Contributions are not being accepted at this time; see
[CONTRIBUTING.md](CONTRIBUTING.md) for what is useful instead.
