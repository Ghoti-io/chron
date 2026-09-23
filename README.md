# ghoti.io-chron

Instants, civil dates and times, calendars, durations, time zones, and the
parsing and formatting of all of them - a correct, cross-platform,
dependency-free C library for time.

`chron` exists because five libraries in this suite need time and none of them
should own it: `text` (YAML's `!!timestamp`, TOML's four date-time types, JSON
Schema's `format` keywords), `ctang` (a template language has to print and
compute dates), `compress` and `image` (gzip, zip, PNG `tIME` and EXIF all
carry timestamps in different encodings), and any game built on `cjelly`,
which will want a calendar the real world never had.

The one rule everything else follows from: **absolute time and civil time are
different types, and converting between them is a function that can fail.**
A `GCHRON_Instant` is a point on the timeline; a `GCHRON_DateTime` is a
reading on a wall clock; and going from the second to the first needs a zone
and a policy, because the reading may name no instant at all, or two.

```c
#include <ghoti.io/chron/chron.h>
#include <stdio.h>

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

  char out[GCHRON_RFC3339_DATE_TIME_MAX];
  size_t length;
  gchron_write_rfc3339_date_time(&when, NULL, out, sizeof(out), &length);
  printf("%s\n", out);
  return 0;
}
```

And in a zone, where the conversion is a function that can fail:

```c
GCHRON_ZoneDb * db;
const GCHRON_Zone * zone;
GCHRON_ZonedDateTime zoned;

gchron_zonedb_default(NULL, NULL, &db);
gchron_zonedb_zone(db, "America/New_York", &zone);

/* 02:30 on the morning the clocks go forward never happened. The zero
   value of GCHRON_Resolve refuses rather than guessing; EARLIER, LATER
   and COMPATIBLE are the three ways to ask for an answer anyway. */
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

## Building

The only link dependency is [`cutil`](../cutil), found through pkg-config.

```bash
make            # the shared and static libraries
make test       # the unit and conformance suites, plus the gates
make help       # every target
```

To build against a local prefix rather than a system install, as the suite's
`bootstrap.sh` does:

```bash
export PKG_CONFIG_PATH="$PWD/../.local/share/pkgconfig"
make test PREFIX="$PWD/../.local"
```

## What is here

The library is in four tiers, split by **what data each one needs**. The tier
boundary is the dependency boundary, and it is why `text` can use this library
without pulling in a single data file. `make check-layering` fails the build
if a tier includes a higher tier's header.

| Tier | Header | Holds | Needs |
| --- | --- | --- | --- |
| 0 | `core.h`, `civil.h`, `calendar.h` | result codes, limits, diagnostics; dates, times, epoch days, ISO weeks, ordinal dates, the nth weekday of a month; the Gregorian, Julian, hybrid and tabular calendars | nothing |
| 0/1 | `duration.h` | `GCHRON_Duration`, its sign invariant, `GCHRON_Overflow`, and the arithmetic: add, until, balance, round | nothing |
| 1 | `instant.h`, `offset.h`, `parse.h` | instants, intervals, exact arithmetic, the Unix encodings; civil time with an offset; the RFC 3339 and TOML grammars in both directions | nothing |
| 2 | `zone.h`, `zoned.h` | named zones, DST transitions, the gap and overlap policy, the local zone, RFC 9557 | TZif files |
| 3 | `format.h` | LDML patterns, `strftime`, month and day names | a names provider |

All four tiers are built. `documentation/design.md` §16 says what phase 4 adds
and what is deliberately absent rather than stubbed.

### Calendars are open

A calendar here is a small vtable, and the struct is public - because a
library whose set of calendars is closed cannot have the one nobody
anticipated. A calendar with fixed month lengths and a cyclic leap rule needs
no code at all, just a filled-in `GCHRON_TabularCalendar`:

```c
/* Ten months of thirty-six days and a five-day festival. */
static const uint16_t months[11] = { 36,36,36,36,36,36,36,36,36,36,5 };
GCHRON_TabularCalendar shire = {
  .id = "tabular:shire", .month_count = 11, .month_days = months,
  .leap_month = 11, .leap_days = 1, .days_in_week = 5,
  .leap_rule = { .cycle_years = 100, .pattern = bits, .pattern_bytes = 13 },
};
GCHRON_Calendar * calendar;
gchron_calendar_tabular(&shire, NULL, &calendar);
```

The Gregorian and Julian calendars are *not* implemented that way - a
closed-form algorithm is faster - but the tests build both as tabular
calendars and prove them identical to the shipped ones over 800,000 days,
which is what says the engine is right.

### The types

| Type | Is | Notes |
| --- | --- | --- |
| `GCHRON_Instant` | `int64_t` seconds + `int32_t` nanoseconds since 1970 | Unix time: every day is 86,400 seconds, so the leap seconds UTC inserted are not in the count. `design.md` §5.1 argues the decision |
| `GCHRON_Date`, `GCHRON_Time`, `GCHRON_DateTime` | a civil reading, with no zone | months are 1-12, years are astronomical (year 0 is 1 BCE), day-of-week is ISO 8601 (Monday 1, Sunday 7), everywhere |
| `GCHRON_OffsetDateTime` | a civil reading and the offset it was written with | keeps RFC 3339 §4.3's `-00:00`, which means *offset unknown* and is not the same statement as `Z` |
| `GCHRON_Duration` | calendar units and exact units in one type | the *operation* decides what they mean: adding a month to an instant is `GCHRON_ERR_INVALID`, because a month has no length in seconds |
| `GCHRON_YearMonth`, `GCHRON_MonthDay` | for recurrence | "the 29th of February" is a `MonthDay`, and asking for it in a common year is an answer rather than a silent 28th |
| `GCHRON_Interval` | half-open `[start, end)` | so abutting intervals tile the line with no overlap and no gap |

Every one is a plain struct that allocates nothing and is compared through a
function, never `memcmp` - the structs have padding, and a value is not its
bytes.

### Zero is strict

Every policy enum here - `GCHRON_Fraction`, `GCHRON_Leap`, `GCHRON_Overflow`,
and the `GCHRON_Resolve` that arrives with zones - has `REJECT` as its **zero**
value. An options struct a caller zero-initialised and forgot a field of
therefore *refuses* the ambiguous case instead of guessing at it, and the
convenient behaviour is always the one a caller asks for by name:

```c
GCHRON_ParseOptions opts;
gchron_parse_options_json_schema(&opts);   /* truncate; leap seconds at 23:59 UTC */
gchron_parse_options_toml(&opts);          /* truncate; a space separator */
gchron_parse_options_default(&opts);       /* refuse both */
```

`tests/unit/test_policies.cpp` asserts the rule for every enum.

### Errors say where

```c
GCHRON_Error error;
gchron_parse_rfc3339_date_time("1990-02-31T15:59:59Z", 20, NULL, &out, NULL,
    &error);
/* error.diag   == GCHRON_DIAG_DAY_OUT_OF_RANGE
   error.offset == 8, error.length == 2   -- underlining "31"
   error.message is static; nothing is allocated for an error. */
```

## Correctness

The principle is `regex`'s: **the oracle is the authority, and every vector is
generated from one, never written from memory.** A corpus grown from one's own
fixes measures the fixes, and that applies with particular force to time,
where a person's intuition about a calendar is the thing under test.

| Claim | Oracle |
| --- | --- |
| civil ↔ epoch day | an exhaustive sweep over ±100,000 years - 73 million days - checking that the labelling round-trips **and** steps exactly one day at a time in date, weekday and ordinal; plus a property check sampled over the whole nine-digit year range |
| *which* calendar that is | Python's `datetime.date`, for every year 1-9999: the epoch day of 1 January, its weekday, the length of the year, and all twelve month lengths. The sweep alone would pass with August thirty days long |
| ISO week dates, ordinal days, Rata Die | Python's `date.isocalendar()` and `toordinal()`, every 997th day from 0001-01-01 - a stride coprime with 7, with the Gregorian cycle and with every month length |
| RFC 3339 `date-time`, `date`, `time`, `duration` | the JSON-Schema-Test-Suite's optional `format` vectors. **All 207 string cases pass** |
| every transition of twenty zones | `zdump`, the reference implementation of the tzdb itself: 8,540 rows, each checking the offset, the daylight-saving flag, the abbreviation **and** the civil reading derived from the instant |
| the POSIX `TZ` footer grammar | glibc's own `tzset`, over 548,960 probes covering **every distinct footer rule in this machine's database** - harvested from the TZif files rather than typed, which is how the corpus came to contain a negative daylight-saving offset, a thirty-minute shift and a rule that wraps a year |
| every zone, not just twenty | Python's `zoneinfo`, reading the same files through different code: 105,948 probes over all 486 zones, by `make check-oracle-zoneinfo` |
| the Julian and hybrid calendars | the `convertdate` package, which implements Reingold and Dershowitz's algorithms: 35,906 Julian vectors, and every day around each of the three cut-overs |
| duration arithmetic | its own contract, checked as a property rather than against a table: **`from + until(from, to) == to`**, over hundreds of date pairs and every unit. Two defects came out of it that no oracle would have found, because no oracle is asked whether a library agrees with itself |

Vectors are committed, so `make test` never needs an oracle;
`tools/corpus/fetch.sh` and `make vectors` regenerate them, and a CI that runs
both fails on a diff - which is how an upstream corpus change is noticed
rather than absorbed. A missing vector file **fails** rather than skips: a
gate that turns a broken harness into a green run is not measuring anything.

The differentials themselves need `libicu-dev`, `node` and `python3`, so they
are not in `make test` - a machine is not obliged to have them. `make
check-oracles` runs all four, and **`make test-full`** is the suite plus the
four with `REQUIRE_ORACLES=1`, which turns every "skipped, no such tool" into
a failure. That is what a release is measured with; the lenient default is for
the developer who has not installed ICU and still wants the suite to run.

The gates are themselves checked. `make check-layering` was verified by adding
an include of `instant.h` to a tier-0 source and watching the build fail - it
did not, the first time, and `design.md` §13 records why.

Strict aliasing is the one undefined-behaviour class with **no runtime gate at
all**: ASan, UBSan and the fuzzers detect a violation at no optimization level,
so a compile-time warning is the only instrument that exists. It is armed in
`CFLAGS` - `-fstrict-aliasing -Wstrict-aliasing=1`, named rather than left to
the `-O` level, because what arms the warning is the option and gcc only turns
it on by default from `-O2`. A gate resting on that proxy goes silent the
moment somebody changes an `-O`. Level 1 rather than the level 3 `-Wall`
implies: on `libs/model`'s eleven real violations, level 3 found none and
level 1 found all eleven.

Being in `CFLAGS` under `-Werror` is the point - a violation fails the build,
so there is no separate sweep that could fail to look. `make check-aliasing`
is what `CFLAGS` cannot prove about itself: it compiles a planted violation
with the library's own flags and fails if it is *accepted*, since a disarmed
warning looks exactly like a clean library. It was verified by watching it
fail both ways, disarmed and broken-for-another-reason, and the second
attempt found a bug in the first one's error message that only the failing
path could show.

**That instrument is gcc's.** clang accepts `-fstrict-aliasing
-Wstrict-aliasing=1` in silence and implements no such diagnostic - it rejects
only the level 3 spelling, as an unknown warning option. So a `make CC=clang`
build carries the aliasing flags on every compile line and has no aliasing
coverage whatsoever, and nothing about the command line says so. `make
check-aliasing` fails under clang for that reason, which is correct and not a
Makefile fault; its message names the compiler that accepted the violation,
because a disarmed `ALIASING_CFLAGS` and a compiler without the warning look
identical and want opposite fixes. Running the suite under clang therefore
needs a decision about aliasing coverage rather than a flag.

`make check-stamps` guards the build's own incrementality. Every object rule
names a stamp file holding the flags it was compiled with, so changing a flag
rebuilds what it affects; a rule added without one compiles with whatever is
in force and is then never rebuilt again, which is indistinguishable from a
correct incremental build. Nothing in make requires the stamp, so the gate
does - reading the makefile text rather than make's rule database, because
the rule that was actually missing its stamp sits inside an `ifneq` and does
not exist in this library at all. It also checks that the stamp belongs to
the rule's own tree, since a rule copied between the release, ASan and fuzz
trees keeps the old one and then misses exactly the changes it was there to
catch. And it checks that each stamp *records* the variables its own recipes
expand, which is the failure the first two arms cannot see: chron's library
objects compile with `$(LIB_CFLAGS)` while the stamp recorded `$(CFLAGS)`, so
changing `-fvisibility=hidden` - a flag that lives only in the former -
rebuilt 0 of 41 objects. It now rebuilds 43, and the null result was armed
before being believed: built with `default` the library exports 287 symbols
against 240 for `hidden`, so the flag was genuinely an input and the 0 was
staleness rather than a no-op.

Its own sweep is checked three ways. A planted fragment carrying one
unstamped rule and one whose recipe uses a variable the stamp omits must come
back as exactly those two findings. An independent count of the same
population must agree with the sweep's - which promptly disagreed, because the
comment explaining what the sweep looks for was itself counted as an eleventh
compile rule. And the compiler invocations the gate does *not* model - links,
and the three rules that compile a source straight to an executable - are
pinned at ten, so that set cannot grow in silence; those three are safe here
only because each depends on the static archive, which is measured rather than
assumed.

**The fuzzers assert invariants, not just absence of crashes**, which is why
they found three defects the three oracles could not: a seventy-four byte TZif
file claiming 987,654,144 transitions, and two `TZ` rules whose changeovers
coincide or cross a year boundary, where the transition search and the offset
lookup disagreed with each other. `design.md` §16 has the detail. All three
needed input no real database contains.

```bash
make test                    # 379 tests in 30 suites, the conformance runners included
make test-full               # the same, with every gate and differential required
make test-valgrind           # the same, clean
make test-asan               # ASan + UBSan; the UBSan half proves no signed overflow
make fuzz                    # text, arithmetic, durations, TZif and the TZ grammar
make check-symbols           # every exported symbol carries the version namespace
make check-layering          # no tier includes a higher tier's header
make check-aliasing          # the strict-aliasing warning is still armed
make check-stamps            # every compile rule rebuilds when its flags change
make check-oracles           # all four differentials against their oracles
make vectors                 # regenerate the committed vectors from their oracles
```

## Documentation

- `documentation/design.md` - what exists and why, the twenty-four mistakes
  the library exists not to repeat, and the phase plan.
- `make docs` - the Doxygen manual.

## Status

All five phases of `documentation/design.md` §16. All four tiers are
built: civil arithmetic; the Gregorian, Julian, hybrid and tabular calendars;
instants, offsets and durations with their full arithmetic; the RFC 3339,
TOML, RFC 9557 and ISO 8601 duration grammars; time zones - TZif, the POSIX
`TZ` footer, the gap and overlap policy and the local zone; and formatting -
LDML patterns, `strftime`, the named formats, HTTP-date and RFC 5322, with
the thirteen interop encodings and the clock.

Formatting is checked against ICU's `SimpleDateFormat` (3,639 comparisons,
with three stated zone-name divergences) and against glibc's `strftime`.

Leap seconds and TAI are in `leap.h`, which nothing else includes - an
application that does not convert to TAI links none of it. The embedded
time-zone database is generated at build time from the machine's zoneinfo
tree, and `gchron_zonedb_default()` picks whichever of it and the system
database is the newer tzdata release.

The Windows zone mapping is committed - 139 names from CLDR, refreshed with
`tools/tzdata/fetch-cldr.sh` and `make embed-windows-zones`, which are the
only two things here that touch the network. Nothing has been run on Windows;
the workspace's `notes/suite/WINDOWS-TODO.md` §6b and §6c say what would
make it so.

Version 0.0.0.

## License

LGPL-3.0-only. See [COPYING.LESSER](COPYING.LESSER) for the license, and
[COPYING](COPYING) for the GPL text it is written as additional permissions
on top of.

Contributions are not being accepted at this time; see
[CONTRIBUTING.md](CONTRIBUTING.md) for what is useful instead.
