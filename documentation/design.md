# The design of ghoti.io-chron

**Status:** all five phases have shipped. This
page says what exists and why, so that the code can be judged against it
rather than the other way round. A change of mind lands here first, in the
same commit as the code that needs it (`CONVENTIONS.md` §9), and §16 marks
what is built.

`chron` is the suite's time library: instants, civil dates and times, time
zones, calendars, durations, and the parsing and formatting of all of them. It
exists because five libraries in this suite need time and none of them should
own it: `text` (YAML's `!!timestamp`, TOML's four date-time types, JSON
Schema's `format` keywords), `ctang` (a template language has to print and
compute dates), `compress` and `image` (gzip, zip, PNG `tIME` and EXIF all
carry timestamps in different encodings), and any game built on `cjelly`,
which will want a calendar the real world never had.

The name is the whole word. The prefix is `GCHRON_` / `gchron_`, which
collides with no prefix in the suite and is not one letter away from
`gmtime`.

---

## 1. What the library is for

The brief is a correct, cross-platform, dependency-free C library for time
that an enterprise can build on and that the rest of this suite can use
without thinking. Each word is a mechanism:

| Property | Mechanism |
| --- | --- |
| **Correct** | Absolute time and civil time are different types and the conversion between them is a function that can fail (§3). Every policy enum's zero value refuses rather than guesses (§3.7). The arithmetic is integer, checked, and returns a result code (§7). Every rule is checked against an outside reference - `zdump`, glibc's `tzset`, Python's `zoneinfo`, ICU, the published calendar tables - never against this library's own opinion (§12). |
| **Cross-platform** | Tiers 0 and 1 (§2) touch no operating-system API at all and behave identically everywhere by construction. The two places that do touch the OS - the clock and the zone-file loader - are one file each, with a Windows branch marked per `CONVENTIONS.md` §11. |
| **Dependency-free** | The only link dependency is `cutil`, for the allocator, checked size math, a mutex, path joining and whole-file reading. Time-zone data comes from the operating system's `zoneinfo` directory or from a table the build generates from the IANA source; ICU is used as an *oracle* in the tests and is never linked (§12). |
| **Enterprise-ready** | No global state: no `TZ` read the caller did not ask for, no `tzset()`, no static return buffers, no hidden "system default zone" (§3.8). "Now" is injectable, so anything that depends on it is testable (§9). The data that answered a question - the tzdata version, the leap table's expiry - is queryable, because "which rules produced this timestamp" is an audit question (§6.6). |
| **Useful** | The first consumers exist today and their exact needs are enumerated in §11. Named formats exist so that nobody types `YYYY` when they meant `yyyy` (§8.4). The encodings other systems use - `FILETIME`, NTP, Excel serials, DOS timestamps - are converted by name, each with its classic defect documented (§10). |

### 1.1 The shape is borrowed, deliberately

Joda-Time, `java.time`, Noda Time, JavaScript's Temporal and Rust's `jiff`
converged, over twenty years and several public post-mortems, on the same
small set of types and the same handful of rules. The convergence is the
design. Where this document departs from it, the departure is marked and
argued. Where it does not, the reader can assume the reason is the one those
libraries' authors gave, and the references at the end say where.

### 1.2 The threat model

Three of the inputs are untrusted:

- **A TZif file** is a binary format read from disk. A corrupt or hostile one
  must produce `GCHRON_ERR_CORRUPT` or `GCHRON_ERR_LIMIT`, never a crash and
  never a zone that silently answers wrongly. It is fuzzed.
- **A POSIX TZ string** (`EST5EDT,M3.2.0,M11.1.0`) is a small grammar that
  arrives in the footer of every modern TZif file and in the `TZ` environment
  variable. It is fuzzed.
- **A date string** from a document is the input `text` will hand this
  library thousands of times per file. Every parser here is bounded by input
  length and is fuzzed.

A fourth is untrusted in a different way: **a format pattern** is usually
written by the programmer, but in `ctang` it comes from a template, and a
template may come from a user. The format compiler is bounded and fuzzed for
the same reason `regex` bounds its pattern parser.

---

## 2. Mistakes this library exists not to repeat

The suite's convention is that a rule names the defect it prevents. Time has
a longer list of famous defects than any other domain a library here covers,
so the list comes first, and the rest of the design refers back to it by row.

| # | The mistake | Where it happened | What `chron` does instead |
| --- | --- | --- | --- |
| M1 | One type for "a point in time" and "a wall-clock reading" | C `struct tm` + `mktime`; JS `Date`; Python naive `datetime` | Separate types (§3). Converting a civil reading to an instant is a function that takes a zone and a policy and can fail with `GAP` or `AMBIGUOUS`. |
| M2 | Global mutable time-zone state | `tzset()`, `TZ`, `localtime()`; Java `TimeZone.setDefault()`; Python `time.tzset()` | No process-wide zone. Every zoned operation takes a zone handle. Reading `TZ` or `/etc/localtime` is an explicit call (§6.5). |
| M3 | A static buffer returned to the caller | `localtime()`, `gmtime()`, `asctime()`, `ctime()` | Value types written to caller-owned storage. |
| M4 | A tri-state `is_dst` flag as the disambiguator | `tm_isdst` (positive, zero, negative = "you figure it out") | An explicit `GCHRON_Resolve` policy on every civil-to-instant conversion (§6.4), and the offset itself in the result. |
| M5 | Floating-point seconds | JS `Date` (ms as double), Python `time.time()`, Ruby | `int64_t` seconds + `int32_t` nanoseconds. No `double` in any core type. A lossy `as_double` accessor exists and says so in its name. |
| M6 | 32-bit seconds | `time_t` on 32-bit ABIs; 2038 | 64-bit seconds everywhere, including the interop conversions that read 32-bit fields (§10). |
| M7 | Zero-based months, 1900-based years, Sunday-is-zero in one API and Monday-is-one in the next | Java `Date.getYear()`, JS `getMonth()`, `struct tm`, `strftime %w` vs `%u` | Months are 1-12, years are astronomical, day-of-week is ISO 8601 (Monday = 1, Sunday = 7) in every type and function. Other numberings exist only as format letters (§8). |
| M8 | Week-based year mistaken for the calendar year | Java/ICU `YYYY` vs `yyyy`; every New Year's Eve outage that resulted | The LDML letters are kept because ICU is the oracle, but the named formats (§8.4) are the recommended route and the compiler warns on `YYYY` without a week letter beside it. |
| M9 | Calendar units and exact units in one bag with no rule | ISO 8601 durations as everyone parses them | A duration carries both kinds of unit, and the *operation* decides: adding a month to an instant is `GCHRON_ERR_INVALID` (§4). |
| M10 | Silent month-end clamping, or silent rejection, with no way to choose | `Jan 31 + 1 month` in every library, differently | `GCHRON_Overflow` policy on every calendar addition; zero is `REJECT` (§4.3). |
| M11 | "Start of day" is midnight | Every `setHours(0,0,0,0)` in a zone where midnight did not exist that day (America/Sao_Paulo, 2018-11-04) | `gchron_zoned_start_of_day()` is a zone-aware function, not a field assignment (§6.4). |
| M12 | Zone abbreviations treated as identifiers | Parsing `CST` (China, Cuba, Central) or `IST` (India, Israel, Ireland) | Abbreviations are output-only. No parser here accepts one as a zone. |
| M13 | The zone name lost at serialisation | RFC 3339 carries only an offset, so `2026-03-08T01:30-05:00` cannot say it meant New York | RFC 9557's `[America/New_York]` suffix is parsed and written (§8.1). |
| M14 | `-00:00` treated as `Z` | `java.time`, most parsers | RFC 3339 §4.3 gives `-00:00` a meaning: *offset unknown*. It is preserved as a flag (§3.4). |
| M15 | Leap seconds either crash the parser or vanish | Python and `java.time` reject `:60`; Temporal clamps it silently | A `GCHRON_Leap` policy with four levels; the default refuses; the flag survives; a separate module does TAI properly (§5). |
| M16 | Comparing offset date-times by their fields | Java's `equals` vs `isEqual` confusion | Two functions with different names: `_compare` orders by instant, `_identical` compares every field (§7.4). |
| M17 | Excel's 1900 leap year, NTP's 2036 era, DOS local time, FILETIME's 1601 epoch, each rediscovered by each program | Everyone converting a file timestamp | An interop module with one function per encoding and the defect documented on each (§10). |
| M18 | "Now" called from inside the library | Two-digit-year rules in HTTP dates; "is this certificate valid"; anything untestable | `GCHRON_Clock` is a parameter (§9). Nothing in the library calls the OS clock unless handed a clock that does. |
| M19 | A calendar the library did not anticipate is impossible | ICU's closed set; `java.time`'s `Chronology` needing a JAR | `GCHRON_Calendar` is a public vtable, and a tabular calendar needs no code at all (§5). |
| M20 | Years capped at four digits | ISO 8601's basic form; many parsers | Nine-digit astronomical years, the ISO 8601 expanded representation on input and output (§3.2). |
| M21 | The proleptic Gregorian calendar applied to dates before it existed, with no way to say otherwise | Every "historical" date computed in a database | The Julian and the hybrid (cut-over) calendars, and the cut-over gap reported with the same `GAP` result as a DST gap (§5.3). |
| M22 | Precision silently truncated | Nanoseconds dropped to microseconds by a parser that could only hold six digits | A `GCHRON_Fraction` policy; zero is `REJECT`; TOML's "truncate" is a setting the TOML resolver chooses (§8.2). |
| M23 | The hidden monotonic reading | Go's `time.Time` carrying an invisible monotonic component that changes what `==` means | A monotonic reading is a `GCHRON_Tick`, not an instant, and only differences between ticks are meaningful (§9). |
| M24 | Undefined behaviour on overflow | Adding a large duration in any C library | Checked arithmetic via `cutil`'s `safemath.h`; `GCHRON_ERR_RANGE` (§7.1). |

---

## 3. The type system

Four tiers, split by **what data each one needs**. The tier boundary is the
dependency boundary, and it is why `text` can use this library without
pulling in a single data file.

| Tier | Holds | Needs | Consumers |
| --- | --- | --- | --- |
| 0 | civil arithmetic: dates, times, day-of-week, weeks, epoch days; calendars | nothing | everyone |
| 1 | instants, exact durations, fixed offsets, RFC 3339 / ISO 8601 / RFC 9557 text | nothing | `text`, `compress`, `image` |
| 2 | named time zones, DST transitions, the local zone | TZif files or the embedded table | `ctang`, applications |
| 3 | presentation: month and day names, LDML patterns, `strftime` | a names provider (English is built in) | `ctang` |

Nothing in tier *n* includes a header from tier *n+1*, and `make
check-layering` fails the build if it does (§13).

### 3.1 Value types

All of these are plain structs, copyable, comparable through the functions
in §7.4, and allocate nothing. None contains a pointer except
`GCHRON_ZonedDateTime`, which borrows a zone (§6.3).

| Type | Fields | Invariant |
| --- | --- | --- |
| `GCHRON_Instant` | `int64_t sec; int32_t nsec` | Unix time (§5.1); `0 <= nsec < 1e9` |
| `GCHRON_Date` | `int32_t year; uint8_t month; uint8_t day` | proleptic Gregorian unless paired with a calendar (§5); a real date in that calendar |
| `GCHRON_Time` | `uint8_t hour, minute, second; int32_t nsec` | `hour < 24`, `second < 60` (§5.4 says what happens to `:60`) |
| `GCHRON_DateTime` | `GCHRON_Date date; GCHRON_Time time` | both invariants |
| `GCHRON_OffsetDateTime` | `GCHRON_DateTime civil; int32_t offset_sec; bool offset_unknown` | `-86400 < offset_sec < 86400` |
| `GCHRON_ZonedDateTime` | `GCHRON_Instant instant; const GCHRON_Zone * zone; int32_t offset_sec` | `offset_sec` is what the zone said for that instant; cached, never authoritative |
| `GCHRON_Duration` | §4 | all non-zero fields share a sign |
| `GCHRON_YearMonth`, `GCHRON_MonthDay` | as named | used for recurrence: "the 29th of February" is a `MonthDay`, not a `Date` |
| `GCHRON_Interval` | `GCHRON_Instant start, end` | half-open `[start, end)` |
| `GCHRON_Tick` | `int64_t nsec` | monotonic; §9 |

The offset is in **seconds**, not minutes, because the tzdb is: Europe/Amsterdam
kept local mean time at `+00:19:32` until 1937, and an offset type that
cannot hold that cannot round-trip the zone's own history.

`GCHRON_Instant` and `GCHRON_Duration` share their normalisation: the
nanosecond field is always in `[0, 1e9)` and the sign lives in the seconds.
`-1 ns` is `{-1, 999999999}`. This is `java.time`'s rule; it makes the
arithmetic uniform at the cost of a formatter having to think for a moment.

### 3.2 Year numbering and range

Years are **astronomical**: year 0 exists and is 1 BCE; year -1 is 2 BCE.
This is what ISO 8601 says and what every algorithm in the literature
assumes; "1 BCE" is a presentation of year 0, produced only by the era format
letters (§8).

The supported range is **years -999,999,999 to 999,999,999** in every
calendar, and `GCHRON_YEAR_MIN` / `GCHRON_YEAR_MAX` say so. Nine digits is
the ISO 8601 expanded representation with a sign, it is a few hundred times
wider than any date anyone has, and it is far inside what the arithmetic can
carry: the epoch-day count for the last supported year is about `3.7e11`, and
seconds about `3.2e16`, against an `int64_t` ceiling of `9.2e18`. The guard
band up to `INT32_MAX` is what lets `year + 1` inside a leap-year test never
overflow. The range is wide because the user who asked for it is writing a
game, and a game's calendar starts wherever it likes.

A value outside the range is `GCHRON_ERR_RANGE` at the point it would be
constructed, never a wrapped or clamped value.

### 3.3 The epoch day

The universal currency between calendars and between civil time and instants
is the **epoch day**: an `int64_t` count of days since 1970-01-01 in the
proleptic Gregorian calendar, day 0 inclusive. An instant's epoch day is
`floor(sec / 86400)`; a date's is what its calendar's `to_epoch_day` returns.

Two other day counts are conversions of it, provided because the literature
uses them and a caller porting a published algorithm should not have to
rederive the constants:

| Count | Epoch | 1970-01-01 is | Function |
| --- | --- | --- | --- |
| Julian Day Number | -4713-11-24 (proleptic Gregorian), noon | 2,440,588 | `gchron_epoch_day_to_jdn` |
| Rata Die (R.D.) | 0001-01-01, R.D. 1 | 719,163 | `gchron_epoch_day_to_rd` |

The JDN is the integer *civil* day; the astronomical Julian Date, which
starts at noon and has a fraction, is not a type here (§2 non-goals).

### 3.4 What `OffsetDateTime` remembers that `Instant` cannot

An `OffsetDateTime` is an instant plus the offset it was *written with*, and
two things about the writing are worth keeping:

- **`offset_unknown`** (M14). RFC 3339 §4.3: `-00:00` means the local offset
  is unknown; `Z` and `+00:00` mean it is known to be zero. A log line with
  `-00:00` in it is telling you something, and a library that rewrites it to
  `Z` on the way through has destroyed evidence. The flag is set by the
  parser, preserved by every operation that keeps the offset, and written
  back by the RFC 3339 formatter.
- **The offset itself**, which is why `gchron_offset_compare` orders by
  instant and `gchron_offset_identical` compares the fields too (M16).

### 3.5 What `ZonedDateTime` is

An instant and a zone. That is all it *is*; the cached `offset_sec` is a
convenience so that formatting does not re-run the transition search. The
civil reading is derived on demand. This is Temporal's model and the
opposite of Joda-Time's, where the zoned type held civil fields and the
arithmetic on them was where the DST bugs lived.

The consequence to know: **arithmetic on a zoned date-time is defined by the
unit** (§4.2). Adding one *day* keeps the wall-clock time and skips or repeats
an hour as the zone requires; adding twenty-four *hours* does not.

### 3.6 Text is not a type

There is no string-holding type. A parsed value is one of the structs above,
and the parse result carries the facts about the text that do not fit the
struct - a leap second was read, a fraction was truncated, a zone suffix
disagreed with the offset - in a `GCHRON_ParseInfo` the caller may ignore.

### 3.7 Zero is strict

Every policy enum in this library - `GCHRON_Resolve` (§6.4),
`GCHRON_Overflow` (§4.3), `GCHRON_Leap` (§5.4), `GCHRON_Fraction` (§8.2),
`GCHRON_ZoneConflict` (§8.1) - has `REJECT` as its **zero** value. An options
struct a caller zero-initialised and forgot a field of therefore *refuses*
the ambiguous case instead of guessing at it. The convenient behaviour is
always the one a caller has to ask for by name. This is the single rule that
most distinguishes a correctness-first time library from a convenient one,
and `tests/unit/test_policies.cpp` checks it for every enum by asserting that
each enum's constant named `_REJECT` is `0`.

### 3.8 No globals

The library holds no mutable process-wide state. The zone database is an
object the caller creates (§6.2). The clock is an object the caller passes
(§9). `TZ`, `TZDIR`, `/etc/localtime` and the Windows registry are read by
functions whose names say they read them, and by nothing else.

---

## 4. Durations

### 4.1 One type, two kinds of unit

`java.time` split calendar units (`Period`: years, months, days) from exact
units (`Duration`: seconds, nanoseconds) into two types, and the split was
correct about the semantics and wrong about the ergonomics: an ISO 8601
duration string `P1Y2M3DT4H5M6S` is neither, and every program that reads one
has to hand-combine two objects. Temporal kept the semantic distinction and
put it in the *operation* rather than the type, and that is what is adopted
here:

```c
typedef struct GCHRON_Duration {
  int64_t years, months, weeks, days;        /* calendar units */
  int64_t hours, minutes, seconds;           /* exact units    */
  int32_t nsec;
} GCHRON_Duration;
```

- **Calendar units** (`years`, `months`, `weeks`, `days`) have no fixed length.
  A day in a zone with a DST transition is 23 or 25 hours; a month is 28 to
  31 days; a year is 365 or 366. Applying one needs a calendar and, for a
  zoned value, a zone.
- **Exact units** (`hours` down to `nsec`) are SI. Applying one to an instant
  is addition.

The rules that follow (M9):

| Operation | Calendar units present? | Result |
| --- | --- | --- |
| `gchron_instant_add(instant, duration)` | yes | `GCHRON_ERR_INVALID` |
| `gchron_instant_add(instant, duration)` | no | addition |
| `gchron_datetime_add(civil, duration, calendar, overflow)` | either | calendar units first, then exact units, in civil space |
| `gchron_zoned_add(zoned, duration, resolve, overflow)` | either | calendar units in the zone's civil space, resolved by `resolve`; then exact units on the instant |

That last row is the one that matters: "tomorrow at the same time" is `+1
day`, and "in twenty-four hours" is `+24 hours`, and on the day the clocks
change they are different instants. A library that gives the same answer to
both has picked one meaning for the caller.

**Sign.** Every non-zero field has the same sign, or the duration is invalid.
A duration of "one month minus one day" is not a duration; it is two
operations, and writing it as one is how `Jan 31 + P1M-1D` came to mean four
different things in four libraries.

### 4.2 Balancing is explicit

`90 minutes` and `1 hour 30 minutes` are the same exact duration and the
library never rewrites one as the other unless asked. `gchron_duration_balance(d,
largest_unit, relative_to)` carries fields into larger units up to
`largest_unit`, and a `largest_unit` at or above `days` requires
`relative_to` - a zoned or civil date-time - because the length of a day is
not otherwise known. Requesting `GCHRON_UNIT_DAY` with no `relative_to` is
`GCHRON_ERR_INVALID`, not "assume 24 hours".

### 4.3 Month-end overflow

`Jan 31 + 1 month` has no correct answer; it has a *chosen* one. Temporal's
two choices are adopted:

| `GCHRON_Overflow` | `Jan 31 + P1M` | `Feb 29 + P1Y` |
| --- | --- | --- |
| `GCHRON_OVERFLOW_REJECT` (0) | `GCHRON_ERR_RANGE` | `GCHRON_ERR_RANGE` |
| `GCHRON_OVERFLOW_CONSTRAIN` | Feb 28 or 29 | Feb 28 |

And the consequence is stated once, here, and again in the header:
**calendar-unit arithmetic is neither associative nor commutative.** `Jan 31
+ P1M + P1M` is Mar 28 under `CONSTRAIN`; `Jan 31 + P2M` is Mar 31. A caller
who needs a stable "same day next month" keeps the `MonthDay` and re-derives
the date.

### 4.4 Differences

`gchron_*_until(a, b, largest_unit, rounding)` produces the duration from
`a` to `b` expressed with fields no larger than `largest_unit`, and for
calendar units it walks the calendar forward from `a` - so `Jan 31 until Mar
1` in months is `1 month 1 day`, not `1 month -2 days`, and `until(a, b)` is
not the negation of `until(b, a)`. That asymmetry is real and is documented
rather than hidden by symmetrising.

### 4.5 Text

ISO 8601 durations (`P1Y2M3DT4H5M6.5S`, `P3W`, `PT0S`), both directions, with
the fraction permitted on the smallest present unit only, as the standard
says. `-P1D` and `P-1D` are both accepted on input (the second is ISO
8601-2), and only `-P1D` is produced.

**RFC 3339 Appendix A's duration grammar is a different, stricter grammar,
and has its own function.** An earlier draft of this page said the two were
the same; the JSON-Schema-Test-Suite's vectors say otherwise, and they are
right. Appendix A permits no sign and no fraction, and its productions
*nest*:

```
dur-second = 1*DIGIT "S"
dur-minute = 1*DIGIT "M" [dur-second]
dur-hour   = 1*DIGIT "H" [dur-minute]
dur-time   = "T" (dur-hour / dur-minute / dur-second)
dur-day    = 1*DIGIT "D"
dur-week   = 1*DIGIT "W"
dur-month  = 1*DIGIT "M" [dur-day]
dur-year   = 1*DIGIT "Y" [dur-month]
dur-date   = (dur-day / dur-month / dur-year) [dur-time]
duration   = "P" (dur-date / dur-time / dur-week)
```

so `P1Y2M` and `P1M2D` are durations and `P1Y2D` is not, and `PT1M2S` is and
`PT1H2S` is not. A parser written as "read numbers and unit letters until they
run out" accepts all four, which is how a JSON Schema `format` check comes to
pass text no other implementation accepts. `gchron_parse_rfc3339_duration` is
Appendix A; `gchron_parse_iso8601_duration` will be the permissive one.

The two grammars also disagree about what a number too large to hold means.
Appendix A's `P999999999999999999999999D` *is* a duration - the suite says
it is valid - and no `int64_t` holds it, so the parser returns
`GCHRON_ERR_RANGE`, and a caller performing a `format` check treats that as a
pass while a caller wanting the value does not. That is exactly the split §7.2
gives the code, and it is why `format` and value conversion can honestly
disagree about one string.

JSON Schema's `format: duration` names Appendix A, and the
JSON-Schema-Test-Suite's optional duration vectors are the first oracle (§12).

---

## 5. Calendars, and the shape of time

### 5.1 The instant is Unix time, and that is a decision about leap seconds

`GCHRON_Instant` counts seconds since 1970-01-01T00:00:00Z **with every day
86,400 seconds long**. That is POSIX time, and it means the 27 leap seconds
UTC has inserted since 1972 are not in the count: the instant `{1483228799,
0}` is both 2016-12-31T23:59:59Z and the leap second 23:59:60Z that followed
it.

The alternatives were considered and the reasons for rejecting them are the
guidance the author asked for:

**Why not count leap seconds (TAI, or "true UTC")?**

1. *No clock you can read does.* Every operating system clock is Unix time.
   Linux repeats a second; Google and Amazon smear the leap across a day so
   nothing repeats; Windows ignores it. A library whose instant disagreed
   with `clock_gettime` by 37 seconds would be right in a way that makes every
   conversion at the OS boundary wrong.
2. *No format you will read does.* RFC 3339 timestamps are UTC labels, and
   the difference between two of them is computed by every program on earth
   as if the days between were all 86,400 seconds. A library that computed it
   differently would disagree with the file's own author.
3. *The table expires.* `leap-seconds.list` carries an expiry date, after
   which the library cannot know whether a leap second occurred. A core type
   whose meaning depends on a file that goes stale twice a year is a core
   type that cannot be trusted in a binary built last year.
4. *The problem is being retired.* The 27th CGPM resolved in November 2022 to
   discontinue leap seconds by 2035. The list will become a fixed historical
   table, which is exactly the shape of a lookup module and not of a core
   type.
5. *A negative leap second has never happened*, is being discussed, and no
   widely deployed system is known to handle one. Building the core around
   the table would mean building it around a case no oracle can check.

**What the library does about them anyway** (M15):

- The **civil types can carry the label**. A `GCHRON_ParseInfo.leap_second`
  flag records that `:60` was read; the resulting `GCHRON_Time` holds `:59`
  of the same minute with the same fraction, which is where the Linux kernel
  puts the repeated second and what Temporal does. The mapping is lossy and
  the flag is the evidence.
- **Parsing takes a `GCHRON_Leap` policy** (§5.4), whose default refuses.
- **A separate module, `leap.h`**, loads `leap-seconds.list` (present on this
  machine at `/usr/share/zoneinfo/leap-seconds.list`), exposes
  `GCHRON_TaiInstant`, converts between it and `GCHRON_Instant`, and reports
  `GCHRON_ERR_EXPIRED` for any conversion past the table's expiry. That is
  the lossless path for the metrology, astronomy and satellite users who
  need it, kept out of everyone else's binary.

### 5.2 Calendars are a labelling of the epoch day

A calendar is the function pair that turns an epoch day into `(year, month,
day)` and back, plus the facts a formatter and an arithmetic routine need:

```c
typedef struct GCHRON_Calendar {
  const char * id;                             /* "gregory", "julian", ... */
  GCHRON_Result (*from_epoch_day)(const GCHRON_Calendar *, int64_t, GCHRON_Date *);
  GCHRON_Result (*to_epoch_day)(const GCHRON_Calendar *, const GCHRON_Date *, int64_t *);
  int (*months_in_year)(const GCHRON_Calendar *, int32_t year);
  int (*days_in_month)(const GCHRON_Calendar *, int32_t year, int month);
  int (*days_in_year)(const GCHRON_Calendar *, int32_t year);
  bool (*is_leap_year)(const GCHRON_Calendar *, int32_t year);
  int days_in_week;                            /* 7 for every real calendar */
  int64_t week_epoch_day;                      /* an epoch day that is day 1 of a week */
  void * ctx;
} GCHRON_Calendar;
```

The calendar does not touch instants, offsets, zones or the time of day. The
tzdb defines its rules in Gregorian and the library evaluates them in
Gregorian; a Julian-calendar application sees Julian dates *and* the same
instants, offsets and zones as everyone else. Every function that takes a
`GCHRON_Date` accepts a calendar argument, and `NULL` means Gregorian.

### 5.3 The calendars shipped

| Calendar | `id` | Algorithm | Notes |
| --- | --- | --- | --- |
| Proleptic Gregorian | `gregory` | Hinnant's `days_from_civil` / `civil_from_days`, 64-bit | the default; the only one the instant and zone paths use |
| Proleptic Julian | `julian` | Reingold & Dershowitz | historical dates; Orthodox liturgical computation; games |
| Hybrid Julian-Gregorian | `hybrid` | Julian before a cut-over epoch day, Gregorian from it | the cut-over is a parameter: 1582-10-15 (Rome), 1752-09-14 (Britain and colonies), 1918-02-14 (Russia). The days that do not exist - 5 to 14 October 1582 in Rome - are reported with `GCHRON_ERR_GAP`, the same result a DST gap gives, and resolved with the same `GCHRON_Resolve` policy (M21) |
| ISO week date | *(a representation)* | ISO 8601 §4.1.4 | `2026-W38-7`; week 1 holds 4 January; the week-year differs from the year in the first and last days (M8) |
| Ordinal date | *(a representation)* | ISO 8601 §4.1.3 | `2026-263` |
| Tabular | `tabular:<name>` | §5.5 | any calendar with fixed month lengths and a cyclic leap rule; needs no code |

Not in the first release, and why: the **Hebrew** and **tabular Islamic**
calendars are arithmetic and will follow when a consumer asks; the
**observational Islamic** calendar depends on a sighting and is not
computable; the **Chinese** and **Persian astronomical** calendars need an
ephemeris, which is astronomy rather than arithmetic and is out of scope
(§2). The Japanese, Buddhist and Republic of China calendars are Gregorian
with a different year label and belong to the names provider (§8.5), not to
this table.

### 5.4 The leap-second policy

```c
typedef enum {
  GCHRON_LEAP_REJECT = 0,   /* ":60" is GCHRON_ERR_FORMAT                           */
  GCHRON_LEAP_CLAMP,        /* ":60" reads as ":59"; leap_second flag set            */
  GCHRON_LEAP_MINUTE,       /* as CLAMP, but only when the minute is 23:59 in UTC
                               after applying the offset; otherwise ERR_FORMAT       */
  GCHRON_LEAP_TABLE         /* as MINUTE, and only on a date the leap table lists;
                               ERR_EXPIRED past the table's expiry                   */
} GCHRON_Leap;
```

`MINUTE` exists because a real consumer needs exactly it: the
JSON-Schema-Test-Suite's optional `date-time` vectors require
`1998-12-31T23:59:60Z` and `1998-12-31T15:59:60.123-08:00` to be valid, and
`1998-12-31T23:58:60Z` ("wrong minute"), `1998-12-31T22:59:60Z` ("wrong
hour"), `1998-12-31T23:59:61Z` and `2016-12-31T24:59:60+01:00` ("hour 24 is
invalid even with a leap second") to be invalid. `TABLE` is the strict
reading that also refuses `1999-06-30T23:59:60Z`, when no leap second
occurred. The two are distinct because the suite tests the first and a
metrologist wants the second.

### 5.5 Tabular calendars, for worlds that do not exist

The user's stated need is a game. A game's calendar is usually simple in
structure and arbitrary in its constants - ten months of thirty-six days and
a five-day festival, a leap day every fourth year except every hundredth - and
what it must never require is a C compiler. So:

```c
typedef struct GCHRON_TabularCalendar {
  const char * id;
  int month_count;
  const uint16_t * month_days;         /* month_count entries, common year */
  int leap_month;                      /* 1-based month that gains leap_days, or 0 */
  int leap_days;
  GCHRON_LeapRule leap_rule;           /* {cycle_years, leap_years_in_cycle, pattern[]} */
  int64_t epoch_day_of_year_zero;      /* where year 0, month 1, day 1 falls */
  int days_in_week;
  int64_t week_epoch_day;
} GCHRON_TabularCalendar;

GCHRON_Result gchron_calendar_tabular(const GCHRON_TabularCalendar *, GCHRON_Allocator *, GCHRON_Calendar ** out);
```

A `GCHRON_LeapRule` is a cycle length and a bit pattern of which years in the
cycle are leap years, which expresses the Julian rule (`4, 0b0001`), the
Gregorian rule (`400`, 97 bits set), the 33-year Persian approximation, the
30-year tabular Islamic cycle, and anything a game designer writes down.
The Gregorian and Julian calendars shipped in §5.3 are *not* implemented
this way - Hinnant's closed-form algorithm is faster and is what the
oracles were checked against - but `tests/unit/test_calendar_tabular.cpp`
constructs both as tabular calendars and proves them identical to the
shipped ones over the whole supported year range. That is the test that
proves the tabular engine correct, and it costs nothing to write.

A custom calendar with a different *day* - a thirty-hour world - is out of
scope. `GCHRON_Time` is 24 hours of 60 minutes of 60 SI seconds, because the
instant is, and a world with a different day is a scaling the application
applies to instants before handing them to this library.

---

## 6. Time zones

### 6.1 Zones do not require ICU

`/usr/share/zoneinfo` is present on Linux, macOS and the BSDs, is maintained
by the operating system's package manager, and holds one **TZif** file per
zone. TZif is a binary format specified in **RFC 8536**: a header, a table
of transition instants, a table of local-time types (offset, DST flag,
abbreviation index), the abbreviation strings, optional leap-second
records, and - from version 2 - a footer holding a **POSIX TZ string** that
describes the rule for every instant after the last listed transition.
Reading it is a few hundred lines. Python's `zoneinfo`, Go's `time` and
Rust's `chrono-tz` do exactly this; ICU bundles a copy of the same data and
its own reader. What ICU sells beyond that is CLDR - names and formats in
three hundred locales - which is tier 3 and is not needed to know what time
it is in Paris.

This machine's copy is tzdata **2026c** in TZif version 2 files, built
`-b fat` as Debian does: the transition tables are pre-expanded to 2037.
`zic`'s default since 2020b is `-b slim`, which other distributions ship:
the tables end at the most recent rule change and the footer string carries
the rule forward. The footer parser is therefore not an optional extra;
without it every zone on a slim system stops working at the last transition
the file lists, and every zone on a fat one stops in 2037.

### 6.2 The zone database is an object

```c
GCHRON_Result gchron_zonedb_default(GCHRON_Allocator *, const GCHRON_Limits *, GCHRON_ZoneDb ** out);
GCHRON_Result gchron_zonedb_system(GCHRON_Allocator *, const GCHRON_Limits *, GCHRON_ZoneDb ** out);
GCHRON_Result gchron_zonedb_directory(const char * path, ...);
GCHRON_Result gchron_zonedb_embedded(...);
GCHRON_Result gchron_zonedb_memory(const void * blob, size_t len, ...);
void          gchron_zonedb_destroy(GCHRON_ZoneDb *);
GCHRON_ZoneSource gchron_zonedb_source(const GCHRON_ZoneDb *);   /* SYSTEM, EMBEDDED, DIRECTORY, MEMORY */

GCHRON_Result gchron_zonedb_zone(GCHRON_ZoneDb *, const char * id, const GCHRON_Zone ** out);
GCHRON_Result gchron_zonedb_fixed(GCHRON_ZoneDb *, int32_t offset_sec, const GCHRON_Zone ** out);
GCHRON_Result gchron_zonedb_utc(GCHRON_ZoneDb *, const GCHRON_Zone ** out);
GCHRON_Result gchron_zonedb_local(GCHRON_ZoneDb *, const GCHRON_Zone ** out);   /* §6.5 */
const char *  gchron_zonedb_version(const GCHRON_ZoneDb *);                     /* "2026c" */
GCHRON_Result gchron_zonedb_list(const GCHRON_ZoneDb *, ...);
```

- `_system` looks in `$TZDIR`, then the platform's directory. On Windows
  there is none, and `_system` returns `GCHRON_ERR_UNSUPPORTED` with a
  message that says to use `_embedded` or `_default`; the explicit call
  does not quietly fall back.
- `_embedded` uses a table `tools/tzdata/embed.py` generates from an IANA
  source tree at build time, carrying the tzdata version string and the
  CLDR `windowsZones.xml` mapping (§6.5). The table is a generated source
  file, never edited, and the generator fails the build rather than write a
  placeholder (`CONVENTIONS.md` §6).
- `_default` is what an application that has no opinion calls, and it
  chooses between the two by **currency**: it reads the system database's
  version (`tzdata.zi`'s `# version` line, or the `+VERSION` file) and the
  embedded table's, and loads whichever is newer - tzdata releases are
  `YYYYx` and order lexically. A system database whose version cannot be
  determined is preferred, on the user's stated reasoning that the
  operating system's copy is the one someone is updating. Where there is no
  system database at all - Windows - the embedded table is used. Whichever
  was chosen, `gchron_zonedb_source()` and `gchron_zonedb_version()` say
  so, and `gchron_zonedb_dump()` prints both: a fallback that cannot be
  seen is the defect `CONVENTIONS.md` §1 names, and this one can be seen.
  The choice is made once, for the whole database; zones are never mixed
  from two sources.
- Fixed-offset zones and UTC are zones too, so every code path is one code
  path.
- Zones are **loaded lazily and cached**, and the database is **internally
  synchronised** with a `GCU_Mutex`, so one database serves a whole process.
  A `GCHRON_Zone` once returned is immutable, borrowed from the database,
  and valid until the database is destroyed. This is the one thread-safety
  rule in the library that is stronger than `CONVENTIONS.md` §5's default,
  and the header says so.
- Backward-compatibility links (`US/Eastern`, `Asia/Calcutta`) resolve, and
  `gchron_zone_canonical_id()` says what they resolved to - **including when
  the directory has no file for them**, which on a default Debian install is
  all of them. `tzdata.zi` lists every link the tzdb defines and ships with
  base tzdata, so it is what both the embedded generator and a
  directory-backed database consult. Without it the two sources disagreed
  about which names existed, and `gchron_zonedb_default()` inherited the
  disagreement.

### 6.3 What a zone answers

```c
GCHRON_Result gchron_zone_offset_at(const GCHRON_Zone *, GCHRON_Instant, GCHRON_ZoneInfo * out);
GCHRON_Result gchron_zone_next_transition(const GCHRON_Zone *, GCHRON_Instant, GCHRON_Transition * out);
GCHRON_Result gchron_zone_prev_transition(const GCHRON_Zone *, GCHRON_Instant, GCHRON_Transition * out);
GCHRON_Result gchron_zone_offsets_for_civil(const GCHRON_Zone *, GCHRON_DateTime, GCHRON_CivilOffsets * out);
```

`GCHRON_ZoneInfo` is the offset, the DST flag and the abbreviation in force
at an instant. `GCHRON_CivilOffsets` is the answer to "what offsets could
this wall-clock reading have had in this zone": zero of them (a gap), one, or
two (an overlap), with both candidates. This is the primitive under §6.4,
and it is public because a scheduler or a calendar UI needs to *show* the
ambiguity, not just resolve it.

The transition functions exist for the same reason. "When does the next
change happen" is what a cron daemon, a UI countdown and a log rotator all
ask, and answering it by probing instants in a loop is how they get it wrong.

### 6.4 Civil to instant: the function that can fail

```c
typedef enum {
  GCHRON_RESOLVE_REJECT = 0,   /* gap -> ERR_GAP; overlap -> ERR_AMBIGUOUS      */
  GCHRON_RESOLVE_EARLIER,      /* overlap: the first occurrence; gap: before it  */
  GCHRON_RESOLVE_LATER,        /* overlap: the second; gap: after it            */
  GCHRON_RESOLVE_COMPATIBLE    /* overlap: earlier; gap: push forward by the gap
                                  (what java.time and legacy JS Date do)       */
} GCHRON_Resolve;

GCHRON_Result gchron_zoned_from_civil(GCHRON_DateTime, const GCHRON_Zone *, GCHRON_Resolve, GCHRON_ZonedDateTime * out);
```

On `GCHRON_ERR_GAP` and `GCHRON_ERR_AMBIGUOUS` the `out` parameter is not
written, per `CONVENTIONS.md` §5, and `gchron_zone_offsets_for_civil` is how
a caller who wants to ask the user gets the candidates.

`gchron_zoned_start_of_day(zoned, out)` (M11) is the reason the policy is a
parameter rather than a global: it wants `LATER` for the gap case
specifically - the first instant of that day is *after* the gap - and a
library that had a single process-wide policy could not say so.

### 6.5 The local zone, and Windows

`gchron_zonedb_local()` is the one function that reads the environment. In
order:

1. `TZ`, if set. Both forms: an IANA identifier (`Europe/Paris`, with or
   without the leading colon) and a POSIX rule string (`CET-1CEST,M3.5.0,M10.5.0/3`),
   which becomes an anonymous zone.
2. `/etc/localtime`: if a symlink, its target's path under `zoneinfo` is the
   identifier; if a regular file, it is read as an anonymous TZif zone and
   `gchron_zone_id()` returns `NULL`. Debian's `/etc/timezone` is consulted
   for the name in that case.
3. On Windows, `GetDynamicTimeZoneInformation()` gives a Windows zone name,
   which the CLDR `windowsZones.xml` table maps to an IANA identifier. The
   branch is written, marked `TODO(windows):`, and listed in the parent
   `WINDOWS-TODO.md` with "done when" being that `gchron_zonedb_local()` on
   a machine set to Pacific Standard Time returns `America/Los_Angeles`.

Nothing else in the library calls this. A `ZonedDateTime` never has an
implicit zone.

### 6.6 What the data says about itself

`gchron_zonedb_version()` returns the tzdata release (`"2026c"`) from
`tzdata.zi` or the embedded table; `gchron_leap_table_expiry()` returns the
instant after which the leap table is not trusted. Both are printed by
`gchron_zonedb_dump()`. When a timestamp in a log is disputed, the first
question is which rules produced it, and a library that cannot answer has
made the dispute unresolvable.

---

## 7. Arithmetic, comparison and errors

### 7.1 Every operation is checked

Every function that computes returns `GCHRON_Result`. Overflow anywhere in
the computation - and adding a `GCHRON_Duration` of `INT64_MAX` seconds is a
legal call - is `GCHRON_ERR_RANGE`, detected before it happens, never observed
after.

`cutil`'s `safemath.h` is the suite's home for this and is what a `size_t`
computation here uses. It covers `size_t`, `uint32_t` and `uint64_t` only,
and every quantity in this library is a **signed** 64-bit count - where
overflow is undefined behaviour rather than a wrap a portable check could
observe after the fact. `src/core/core_internal.h` therefore carries
`gchron_add_i64`, `_sub_i64`, `_mul_i64` and `_neg_i64`, the same shape and
the same contract as `cutil`'s, plus the floor division and floor modulo that
every day and month calculation goes through. They are `static inline` and
exported by nothing; adding the signed forms to `cutil` instead would be a
reasonable convergence and has not been made. There is no signed overflow in this
library, and `make test-asan`'s UBSan half is the gate that proves it,
driven by `tests/fuzz/fuzz_arith.cpp`, which performs random sequences of
operations on random values (M24).

### 7.2 Result vocabulary

The suite's nine codes (`CONVENTIONS.md` §5), plus four this domain needs.
Each addition is the same shape as `regex`'s `ERR_SYNTAX`: a failure the
library exists to report, that folding into an existing code would make
indistinguishable from something else.

| Constant | Means | Why not an existing code |
| --- | --- | --- |
| `GCHRON_ERR_RANGE` | the result is not representable: overflow, or outside the supported years | `ERR_INVALID` is a wrong *argument*; this is a right argument with no answer |
| `GCHRON_ERR_GAP` | that civil time did not occur in that zone or calendar | the caller's remedy is a policy, and it must be able to tell this from `AMBIGUOUS` |
| `GCHRON_ERR_AMBIGUOUS` | that civil time occurred twice | as above |
| `GCHRON_ERR_EXPIRED` | the leap table or an explicit data validity window does not cover the instant asked about | the remedy is "update the data", which is neither `UNSUPPORTED` nor `CORRUPT` |

`GCHRON_ERR_FORMAT` is "this text is not this grammar" and `ERR_CORRUPT` is
"this TZif file is not a TZif file", the same split `text` uses.

### 7.3 Errors carry a position

`GCHRON_Error` is `{code, offset, length, diag, message}`, as `regex` grew
to. For a parser, `offset` and `length` underline the offending characters.
`diag` is an enum so that a test asserts `GCHRON_DIAG_HOUR_OUT_OF_RANGE` and
not a string. `message` is static; nothing is allocated for an error.

### 7.4 Comparison

| Function | Orders by | Exists because |
| --- | --- | --- |
| `gchron_instant_compare` | the timeline | total order |
| `gchron_date_compare`, `_time_compare`, `_datetime_compare` | the fields | total order within one calendar |
| `gchron_offset_compare`, `gchron_zoned_compare` | the **instant** | `01:30-05:00` and `06:30Z` are the same moment |
| `gchron_offset_identical`, `gchron_zoned_identical` | every field, including the offset, the flag and the zone identity | for "did this survive the round trip" |

`memcmp` on any of these structs is wrong (padding), and the header says so.

---

## 8. Text: parsing and formatting

### 8.1 The grammars, by name

Each is a named grammar with a specification, and each has its own function,
because "parse a date" with no grammar named is a heuristic, and heuristics
are how `Date.parse("2026-09-20")` came to give a different day in different
browsers.

| Grammar | Specification | Produces | Notes |
| --- | --- | --- | --- |
| RFC 3339 | RFC 3339 §5.6, with §5.7 restrictions | `OffsetDateTime`, `Date`, `Time` | the strict default. **`t` and `z` need no option**: RFC 5234 §2.3 makes ABNF string literals case-insensitive, so `1963-06-19t08:30:06z` is as conformant as the uppercase spelling, and the JSON Schema `format` vectors require it to be accepted. An earlier draft of this page had them as options; that was a misreading of the ABNF. The **space** the §5.6 *note* permits is a genuine extension and is an option, off by default. `-00:00` sets `offset_unknown` |
| RFC 9557 (IXDTF) | RFC 9557 | `ZonedDateTime` (with a zone database) or `OffsetDateTime` | RFC 3339 plus `[Europe/Paris]` and `[u-ca=julian]` suffixes; a `!` critical flag on an unknown suffix is `ERR_UNSUPPORTED`; offset-versus-zone disagreement is a `GCHRON_ZoneConflict` policy, zero = `REJECT` (M13) |
| ISO 8601 profile | ISO 8601-1:2019 | civil types, offset, week and ordinal dates, durations, intervals | extended and basic forms, expanded years, `24:00:00` as end-of-day (an option, zero = reject), comma as fraction separator |
| YAML 1.1 timestamp | YAML 1.1 type repository | `GCHRON_YamlValue`: a date, a zoneless date-time, or one with an offset | the type repository's regular expression, which is the definition of the type: one- or two-digit month, day and hour, one or more spaces or tabs for `T`, an empty fraction, an optional offset that may omit its minutes. `t` yes and `z` **no**, which is the reverse of RFC 3339. The date-only alternative needs two digits where the one with a time does not. Deviation: whitespace before a numeric offset is accepted, following PyYAML rather than the published expression - see `documentation/text-formats.md` |
| TOML | TOML 1.0.0 | the four civil/offset types, by which fields are present | space for `T`; millisecond precision required; extra digits per `GCHRON_Fraction` |
| HTTP-date | RFC 9110 §5.6.7 | `OffsetDateTime` (always GMT) | IMF-fixdate is produced; RFC 850 and `asctime` forms are accepted, as the RFC requires. RFC 850's two-digit year uses the RFC's rule, which needs a clock (§9) |
| RFC 5322 | RFC 5322 §3.3, with §4.3 obsolete forms | `OffsetDateTime` | email; obsolete zone names map per the RFC's table, and `-0000` sets `offset_unknown` |
| Unix seconds / milliseconds / nanoseconds | - | `Instant` | integer text; a sign; no fraction |

Every parser takes the text and its length - nothing is NUL-terminated by
assumption - and refuses trailing characters unless the options say to
report where it stopped.

### 8.2 The fraction policy

```c
typedef enum {
  GCHRON_FRACTION_REJECT = 0,    /* more than nine digits is ERR_FORMAT    */
  GCHRON_FRACTION_TRUNCATE,      /* keep nine; ParseInfo.fraction_truncated */
} GCHRON_Fraction;
```

TOML says truncate, so `text`'s TOML resolver passes `TRUNCATE`. RFC 3339
does not say, so the default refuses (M22). Rounding is not offered: a
parser that rounds `23:59:59.9999999999` into the next day has changed the
date.

### 8.3 Formatting is compiled

A format pattern is compiled once into a `GCHRON_Format`, immutable and
shareable, and applied many times - the same shape as `GRX_Regex`, for the
same reason: the pattern is parsed and checked once, its errors are reported
with an offset, and a hostile pattern is bounded at compile time.

The native pattern language is **LDML** (Unicode TR35 §8, the syntax ICU,
Java and .NET share): `yyyy-MM-dd'T'HH:mm:ssXXX`. It is chosen over
`strftime` because it is the international standard, because it is the most
expressive (`VV` for a zone identifier, `xxx` versus `XXX` for offset
spelling, `B` for day periods, `G` for eras), and because ICU speaks it
natively and ICU is the oracle - a pattern's meaning is *defined* as what
`icu::SimpleDateFormat` does with it in the root locale (§12).

`strftime` is a second syntax the compiler accepts (`GCHRON_FORMAT_STRFTIME`)
and lowers to the same program; it covers C, Python, Ruby and Rust
`chrono`. A third for PHP's `date()` letters is `ctang`'s to add when it
needs it, and the compiler's syntax table is designed so that adding one is
a table, not a parser.

### 8.4 Named formats

```c
GCHRON_FORMAT_RFC3339           2026-09-20T15:30:00Z
GCHRON_FORMAT_RFC3339_NANOS     2026-09-20T15:30:00.123456789Z
GCHRON_FORMAT_RFC9557           2026-09-20T17:30:00+02:00[Europe/Paris]
GCHRON_FORMAT_ISO8601_BASIC     20260920T153000Z
GCHRON_FORMAT_ISO_WEEK          2026-W38-7
GCHRON_FORMAT_ISO_ORDINAL       2026-263
GCHRON_FORMAT_HTTP              Sun, 20 Sep 2026 15:30:00 GMT
GCHRON_FORMAT_RFC5322           Sun, 20 Sep 2026 17:30:00 +0200
GCHRON_FORMAT_ISO_DURATION      P1DT2H
```

Each is a pre-compiled `GCHRON_Format` and each has a matching parser in
§8.1, so `parse(format(x))` is an identity for every one of them, and
`tests/unit/test_roundtrip.cpp` says so. They exist so that the RFC 3339
timestamp in a log line was never typed by hand (M8): the compiler also
warns, through `GCHRON_DIAG_WEEK_YEAR_WITHOUT_WEEK`, on a pattern containing
`YYYY` and no `w`.

### 8.5 Names come from a provider

Month names, day names, `AM`/`PM`, era names, the first day of the week and
the minimum days in the first week are a **`GCHRON_Names`** vtable. The
library ships one, `gchron_names_english()`, which is the root/C-locale
table and is what every named format uses. It never reads `LC_TIME` or any
other locale setting; `text` learned in `src/text_number.c` what a locale
does to `printf`, and a time library's exposure is a hundred times wider.

An application that has CLDR - because it linked ICU for other reasons, as
`ctang` has - supplies a provider backed by it, through the same seam. This
is the same shape as `text`'s `GTEXT_JSON_Regex_Provider`: the library
defines the interface and ships the minimum, and the dependency decision
stays with the application.

### 8.6 Output contract

Formatters write into a caller buffer and report the length the output
needs, `snprintf`-style: a buffer too small is `GCHRON_ERR_LIMIT` with
`out_len` set to the required size, and the buffer's contents are
unspecified. There is no allocating variant; the longest output of any named
format is bounded and small, and a compiled custom format reports its own
maximum through `gchron_format_max_length()`.

---

## 9. Clocks

```c
typedef struct GCHRON_Clock {
  GCHRON_Result (*now)(void * ctx, GCHRON_Instant * out);
  void * ctx;
} GCHRON_Clock;

const GCHRON_Clock * gchron_clock_system(void);            /* CLOCK_REALTIME / GetSystemTimePreciseAsFileTime */
GCHRON_Result        gchron_clock_fixed(GCHRON_Instant, GCHRON_Clock * out);   /* for tests */
GCHRON_Result        gchron_tick_now(GCHRON_Tick * out);   /* CLOCK_MONOTONIC / QueryPerformanceCounter */
GCHRON_Result        gchron_clock_resolution(const GCHRON_Clock *, GCHRON_Duration * out);
```

Nothing inside the library calls `gchron_clock_system()`. The two places a
"now" is needed - RFC 850's two-digit-year rule and a caller's own `now()` -
take a `GCHRON_Clock`, so that a test can pin the date and the behaviour on
31 December 2049 can be checked in 2026 (M18).

`GCHRON_Tick` is a separate type because a monotonic reading is not a point
on any calendar: it has no epoch, it is not comparable across processes or
reboots, and only `gchron_tick_since(a, b)` - a `GCHRON_Duration` - has a
meaning (M23). `cjelly` will use it for frame timing; `cutil`'s semaphore,
which is below this library in the dependency graph, keeps its own
`clock_gettime` call.

---

## 10. Interop

One function pair per foreign encoding, and the defect in each encoding
written in the header beside it (M17):

| Encoding | Type | Epoch, unit | The thing to know |
| --- | --- | --- | --- |
| `time_t` | `int64_t` | 1970, seconds | 32-bit on some ABIs; the conversion refuses a value that would not fit the platform's `time_t` |
| `struct timespec` | | 1970, s + ns | the same shape as `GCHRON_Instant` |
| `struct tm` | | civil | `tm_mon` is 0-based, `tm_year` is 1900-based, `tm_wday` is Sunday-0; `tm_gmtoff` is not portable, so the conversion *to* `tm` takes the offset to fill it where it exists and drops it where it does not, and says so |
| Windows `FILETIME` | `uint64_t` | 1601-01-01, 100 ns | UTC; the epoch is not Unix's |
| Windows `SYSTEMTIME` | | civil, ms | `wDayOfWeek` is Sunday-0 |
| .NET ticks | `int64_t` | 0001-01-01, 100 ns | proleptic Gregorian |
| NTP timestamp | `uint64_t` | 1900-01-01, 2^-32 s | 32-bit seconds; era 0 ends 2036-02-07; the conversion takes an era, or a pivot instant, and never guesses |
| Excel serial | `double` | 1899-12-30 | Excel believes 1900 was a leap year (a Lotus 1-2-3 compatibility bug): serial 60 is 1900-02-29, which did not occur. The conversion refuses 60, and offsets serials 61 and up by one day. The 1904 date system (Mac) is a second function |
| DOS date/time (zip) | `uint16_t` × 2 | 1980, 2 s | local time with no zone; the conversion produces a `GCHRON_DateTime`, not an instant, and a caller who wants an instant supplies the zone |
| gzip `MTIME` | `uint32_t` | 1970, seconds | `0` means "not available", not the epoch |
| Unix milliseconds | `int64_t` | 1970, ms | JavaScript's `Date.now()` |
| Apple Cocoa | `double` | 2001-01-01, s | `NSDate.timeIntervalSinceReferenceDate` |
| HFS+ | `uint32_t` | 1904-01-01, s | local time in classic HFS, UTC in HFS+ |
| Julian Day, Modified JD | `int64_t` / `double` | §3.3 | MJD = JD − 2,400,000.5 |
| PNG `tIME` | 7 bytes | civil, UTC | year is 2 bytes big-endian; `image` reads it |
| EXIF `DateTime` | 19 bytes | civil, no zone | `YYYY:MM:DD HH:MM:SS` with colons in the date; `OffsetTime` tags (EXIF 2.31) carry the offset separately |

Every conversion *to* a narrower encoding returns `GCHRON_ERR_RANGE` when the
value does not fit, and none rounds a sub-unit fraction without being asked.

---

## 11. The consumers, and what each one needs

| Consumer | Uses | Tier | Notes |
| --- | --- | --- | --- |
| `text` YAML | the YAML 1.1 timestamp grammar; `GCHRON_YamlValue`; canonical output for `!!timestamp` | 1 | replaces the `has_timestamp` side-car in `yaml_resolve.c` and `yaml_internal.h` with a `GCHRON_YamlValue` on the scalar; the public `GTEXT_YAML_Timestamp` becomes a thin view or is replaced outright, which is free while `text` has no consumers |
| `text` TOML | the TOML grammar; all four civil/offset types; `TRUNCATE` | 1 | TOML's Local Time is the one shape `yaml_resolve.c` cannot hold today |
| `text` JSON Schema | `format`: `date-time`, `date`, `time`, `duration` with `GCHRON_LEAP_MINUTE` | 1 | the JSON-Schema-Test-Suite optional vectors become passable |
| `ctang` | everything: parse, zones, arithmetic, LDML and a PHP letter table, the names provider | 0–3 | the first consumer of tiers 2 and 3; where an ICU-backed `GCHRON_Names` would live if `ctang` keeps ICU |
| `compress` | gzip `MTIME`, DOS date/time | 1 | via `interop.h`; `compress` currently writes these fields by hand |
| `image` | PNG `tIME`, EXIF `DateTime` and `OffsetTime`, TIFF `DateTime` | 1 | via `interop.h` |
| `cjelly` | `GCHRON_Tick` for frame timing; later, a date picker | 0, 2 | |
| games | tabular calendars; nine-digit years; the hybrid calendar's gap semantics as a template for a world's own reform | 0 | the reason the range in §3.2 is what it is |

**`text` links `chron`** (decided 2026-09-20; §15). Tiers 0–1 are a few
thousand lines with no data and no OS calls, a provider seam would have to
expose most of their surface anyway, and `text` already depends on `cutil`,
so the DAG stays a DAG (`cutil → chron → text`). The parent `README.md`'s
dependency graph gains the edge when the first `text` commit uses a
`gchron_` symbol, per `CONVENTIONS.md` §1.

---

## 12. Correctness: oracles and tests

The principle from `regex`'s `testing.md` applies unchanged: **the oracle is
the authority, and every vector is generated from one, never written from
memory.** The lesson from this suite's own history - a corpus grown from
one's own fixes measures the fixes - applies with particular force to time,
where a person's intuition about DST is the thing under test. Every corpus
below is produced by software this library did not write.

| Claim | Oracle | Driver | On this machine |
| --- | --- | --- | --- |
| civil ↔ epoch day, Gregorian | exhaustive round trip over ±100,000 years (7.3e7 days, seconds of runtime); property check over the full nine-digit range; Python `date.toordinal()` for years 1–9999 | `tests/unit/test_civil.cpp`; `tools/oracle/ordinal.py` | yes |
| the 33 sample dates in every calendar | Reingold & Dershowitz, *Calendrical Calculations*, the sample-data appendix; the `convertdate` package; `ncal -J` for Julian | `tools/oracle/rd_table.py` | neither installed: `pip install convertdate`, `apt install ncal` |
| every zone's every transition | **`zdump -v`** over all zones in the system database: the UTC instant and the civil time, offset, abbreviation and DST flag on both sides of every transition, from the reference implementation of the tzdb itself. Several hundred thousand assertions, regenerated when tzdata updates | `tools/oracle/zdump.py` → `tests/data/vectors/zones/transitions.vec` | `zdump` 2.41, tzdata 2026c |
| the same, independently | Python 3.13 `zoneinfo` reading the same TZif files through different code | `tools/oracle/zoneinfo_diff.py` | yes |
| POSIX TZ string evaluation | glibc's own `tzset` + `localtime_r` with `TZ` set to each footer string in the database, across a lattice of instants | `tools/oracle/tzset_probe.c` | yes |
| civil → instant in a gap or overlap | Python `zoneinfo` with `fold=0` / `fold=1`, and `zdump`'s transition rows | as above | yes |
| RFC 3339, `date`, `time`, `duration` text | JSON-Schema-Test-Suite `tests/draft2020-12/optional/format/{date-time,date,time,duration}.json` | `tools/oracle/jsonschema_format.py` | fetched by `tools/corpus/fetch.sh` |
| ISO 8601 and RFC 9557 strings | test262's Temporal string-parsing tests, through Node (present for `text`'s js-yaml oracle); Python `datetime.fromisoformat` | `tools/oracle/test262.js`, `tools/oracle/fromiso.py` | node yes |
| LDML pattern semantics | **ICU** `icu::SimpleDateFormat` in the root locale, through a small C++ driver built only when `pkg-config icu-i18n` succeeds. ICU is the definition of what a pattern means and is never linked by the library | `tools/oracle/icu_format.cpp` | `libicu-dev` is installed for `ctang` |
| `strftime` | glibc `strftime` in the C locale | `tools/oracle/strftime_probe.c` | yes |
| HTTP-date, RFC 5322 | the RFCs' own examples, plus `curl`'s `parsedate` behaviour where it is on the machine | vectors committed | |
| leap seconds | `/usr/share/zoneinfo/leap-seconds.list` and its expiry; the JSON Schema leap vectors above | | yes; no `right/` zoneinfo on Debian |

Regeneration is `make vectors`, gated per oracle by an environment variable
as `regex` does, and the vectors are committed so that `make test` never
needs an oracle. A CI with the oracles installed regenerates and fails on a
diff; that is how a tzdata upgrade is noticed rather than absorbed.

**Skips are counted, never silent.** A run with no `zdump` says how many
vectors it could not check.

### 12.1 Properties

Beyond the vectors, four identities hold everywhere and are checked by
property tests over random inputs:

1. `to_epoch_day(from_epoch_day(d)) == d` for every calendar and every `d`
   in range.
2. `instant(zoned_from_instant(i, z)) == i` for every zone.
3. `parse(format(x)) identical x` for every named format and every `x` the
   format can represent losslessly; and where a format is lossy (RFC 3339
   drops the zone name) the loss is exactly the documented one.
4. Calendar A → epoch day → calendar B → epoch day → calendar A is the
   identity, for every pair.

### 12.2 Fuzzing

`tests/fuzz/fuzz_tzif.cpp` (the file reader), `fuzz_posix_tz.cpp` (the
footer grammar), `fuzz_parse.cpp` (every text grammar, the first byte
selecting which), `fuzz_format.cpp` (the pattern compiler and the formatter
against it), `fuzz_duration.cpp`, and `fuzz_arith.cpp` (random operation
sequences, for UBSan). Corpora are seeded from the vectors.

### 12.3 The gates are themselves tested

Every row of `regex`'s `testing.md` §9 table applies, and one is added: the
`zdump` differential must fail when a single transition in one vector file
is edited by hand. A conformance runner that cannot be made to fail is not
measuring anything.

This applies to the build gates as much as to the vector runners, and phase 3
found two that had never been checked: a `test-valgrind` whose bare `for` loop
reported only its last suite's status, and an ASan build with no header
dependency tracking, which had been silently testing stale objects. Both were
green the entire time. A gate is not tested until it has been *observed to
fail* - not reasoned about, run - and each one here has been.

---

## 13. Code layout

```
include/ghoti.io/chron/
  macros.h libver.h libver_gen.h namespace.h allocator.h    per CONVENTIONS.md §2
  core.h        GCHRON_Result (+4), GCHRON_Limits, GCHRON_Error, GCHRON_Diag, units      [tier 0]
  civil.h       Date, Time, DateTime, YearMonth, MonthDay, epoch day, JDN, R.D.,
                ISO week, ordinal, day-of-week, nth-weekday helpers                       [tier 0]
  calendar.h    GCHRON_Calendar, gregory, julian, hybrid, tabular                         [tier 0]
  duration.h    GCHRON_Duration, balance, until, ISO 8601 duration text                   [tier 0/1]
  instant.h     GCHRON_Instant, Interval, exact arithmetic, rounding                       [tier 1]
  offset.h      GCHRON_OffsetDateTime                                                     [tier 1]
  parse.h       every grammar in §8.1 in *both* directions, GCHRON_ParseInfo,
                GCHRON_WriteOptions, the policies                                          [tier 1 (+2 for RFC 9557 zones)]
  format.h      GCHRON_Format, the compiler, named formats, GCHRON_Names                   [tier 3]
  zone.h        GCHRON_ZoneDb, GCHRON_Zone, transitions, GCHRON_Resolve                    [tier 2]
  zoned.h       GCHRON_ZonedDateTime                                                       [tier 2]
  clock.h       GCHRON_Clock, GCHRON_Tick                                                  [tier 1]
  leap.h        the leap table, GCHRON_TaiInstant                                          [tier 1, optional data]
  interop.h     §10                                                                        [tier 1]
  chron.h       umbrella

src/core/       result strings, limits, diagnostics, checked arithmetic helpers
src/civil/      civil.c gregory.c julian.c hybrid.c tabular.c week.c
src/instant/    instant.c duration.c offset.c interval.c round.c
src/parse/      options.c rfc3339.c duration_text.c toml.c write_rfc3339.c
                iso8601.c rfc9557.c yaml11.c httpdate.c rfc5322.c unix.c
src/format/     compile.c ldml.c strftime.c emit.c names_english.c
src/zone/       tzif.c posixtz.c zonedb.c zone.c zoned.c local.c  local_win.c (TODO(windows))
src/zone/embedded/  generated by tools/tzdata/embed.py; never edited
src/clock/      clock.c  (one #ifdef _WIN32 branch)
src/leap/       leap.c tai.c
src/interop/    interop.c
src/chron.c     version

tools/tzdata/   embed.py (IANA source tree → C table + windowsZones mapping); fetch.sh
tools/oracle/   zdump.py zoneinfo_diff.py gchron_zone.c icu_format.cpp strftime_probe.c
                jsonschema_format.py test262.js fromiso.py ordinal.py rd_table.py
tools/corpus/   fetch.sh for JSON-Schema-Test-Suite, test262, the R&D tables
tests/unit/     one file per header; test_policies.cpp (§3.7); test_roundtrip.cpp
tests/conformance/  the vector runner
tests/data/vectors/ zones/ parse/ format/ calendar/
tests/fuzz/     §12.2
```

The writers for the tier-1 grammars live in `parse.h` beside the parsers they
invert, so that `parse(write(x))` is one header's promise and so that a
consumer whose only use of this library is `text`'s can produce an RFC 3339
timestamp without linking the tier-3 pattern compiler. `format.h`'s named
formats (§8.4) are a second route to the same text for a caller who is
already compiling patterns.

`make check-layering` greps for an include of a higher tier's header from a
lower tier's source and fails naming the file, as `regex` does for its
engine/syntax boundary. It is written as a make macro applied once per tier
rather than as a loop over a packed string: the forbidden pattern is a regular
alternation and so contains `|` itself, which a loop splitting on `|` cuts in
half - leaving a check that passes on everything, a violation included. It was
in exactly that state when first written, and §12.3's rule caught it: the gate
is verified by adding an include of `instant.h` to `src/civil/week.c` and
watching the build fail.

### 13.1 Allocation

Tiers 0 and 1 allocate nothing. The objects that do - `GCHRON_ZoneDb`,
`GCHRON_Zone` (owned by its database), `GCHRON_Format`, a tabular
`GCHRON_Calendar`, the leap table - take a `GCHRON_Allocator` (a typedef of
`GCU_Allocator`) at creation and free through it. Nothing is allocated for
the caller on a failing call, and the `CountingAllocator` pattern from
`regex`'s tests proves it on every failure path.

### 13.2 Limits

| Field | Enforced in | Bounds |
| --- | --- | --- |
| `max_tzif_bytes` | zone loader | size of one TZif file |
| `max_transitions` | TZif reader | transition records in one zone |
| `max_zones` | database | zones loaded and cached |
| `max_parse_length` | every parser | bytes of input |
| `max_format_length` | format compiler | bytes of pattern |
| `max_format_items` | format compiler | compiled items |
| `max_leap_records` | leap table | rows |

Zero means no limit; `gchron_limits_default()` fills them; every parser and
loader takes one; `ERR_LIMIT` names the field in the message.

`GCHRON_Limits` carries only the fields something enforces **today** -
`max_parse_length` after phase 0, and `max_tzif_bytes`, `max_transitions`,
`max_zone_types` and `max_zones` after phase 1 - and each of the rest arrives
in the phase that enforces it. A limit nothing reads is a promise nothing keeps, and this
suite has the scar: `regex` declared table flags it never consulted, and the
mechanism they implied was designed twice before anyone noticed the field was
dead.

### 13.3 Threads

Value types are values. `GCHRON_Zone`, `GCHRON_Format` and `GCHRON_Calendar`
are immutable once created and may be shared. `GCHRON_ZoneDb` is internally
synchronised (§6.2). The leap table is immutable once loaded. There is no
other shared state.

---

## 14. Non-goals for the first stable release

- **CLDR data.** Names in any language but English come through the
  provider (§8.5). Shipping CLDR is shipping ICU's problem.
- **Astronomy.** No sunrise, no equation of time, no ΔT, no astronomical
  Julian Date with a fraction, and therefore no Chinese or astronomical
  Persian calendar. The JDN integer is arithmetic and stays.
- **Recurrence rules** (RFC 5545 `RRULE`). The primitives they need -
  nth-weekday-of-month, next transition, `MonthDay` - are here, and `RRULE`
  is a module a calendar application would add.
- **Business calendars.** Holidays and working days are policy.
- **ISO 8601-2 extended date/time (EDTF)** - uncertain and approximate
  dates, seasons, sets. Wanted by historians and possibly by games; a
  later module, and the civil types are designed not to preclude it.
- **A different length of day** (§5.5).
- **Locale-dependent behaviour of any kind.** Nothing reads `LC_*`.
- **Smearing.** The library reports the instants the OS gives it; a smeared
  clock is the OS's decision and is invisible here.

---

## 15. Decisions

Listed so that they were decided on purpose. The first three were put to
the author and answered on 2026-09-20; the rest stand as recommended until
someone objects.

1. **`text` links `chron`.** Decided: yes (§11).
2. **Embedded tzdata is in the first release, as a fallback.** Decided: yes,
   with the operating system's database preferred because it is the copy
   someone is updating. §6.2's `gchron_zonedb_default()` is the shape that
   came out of it: the newer of the two by version, the embedded table where
   there is no system database, and the source always queryable. The
   embedded release is pinned in the Makefile and fetched by
   `tools/tzdata/fetch.sh`.
3. **`GCHRON_` is the prefix.** Decided: yes.
4. **Unified `GCHRON_Duration` (Temporal) versus split `Period`/`Duration`
   (`java.time`).** §4.1 recommends unified, with the distinction enforced by
   the operation. The author's earlier note in conversation recommended the
   split; this document changes that recommendation, and says why.
5. **Default `GCHRON_Resolve`.** `REJECT`, by the rule in §3.7. Temporal
   defaults to `COMPATIBLE`, and a caller who wants what Temporal does
   writes the word.
6. **LDML as the native pattern syntax.** §8.3. The alternative is
   `strftime`, which cannot express a zone identifier or an era.
7. **Nine-digit years** (§3.2) rather than the full `int32_t` range. The
   guard band is what makes the overflow proofs trivial.

---

## 16. Plan

Phases, in dependency order, with the milestone each one unlocks. Sizes
follow `regex`'s `plan.md`: S up to a week, M two to four, L four to eight,
for one engineer who knows the suite.

| Phase | Work | Size | Unlocks |
| --- | --- | --- | --- |
| 0 **(done)** | Scaffold from `model` (`CONVENTIONS.md` §12); `core.h`; `civil.h` with Gregorian; `instant.h`; `offset.h`; `duration.h`'s type and its Appendix A text; RFC 3339 and TOML grammars, both directions; the exhaustive civil tests; the JSON Schema format vectors | M | **M1: `text` can replace its timestamp side-car and pass the JSON Schema `format` vectors** - reached; all 207 vectors pass |
| 1 **(done)** | `zone.h`: TZif, the POSIX TZ footer, `ZoneDb`, `Resolve`, transitions, the local zone (Linux/macOS); `zoned.h`; the `zdump`, `zoneinfo` and `tzset` differentials; RFC 9557 | L | **M2: `ctang` can render a date in a zone** - reached |
| 2 **(done)** | `calendar.h`: Julian, hybrid, tabular, ISO week and ordinal; `duration.h` in full: balance, until, overflow; rounding; the R&D vectors | M | **M3: games; historical dates** - reached |
| 3 **(done)** | `format.h`: the LDML compiler, `strftime` lowering, named formats, the names provider; the ICU and `strftime` differentials; HTTP-date and RFC 5322; `interop.h`; `clock.h` | M | **M4: `ctang` formatting; `compress`/`image` interop** - reached |
| 4 **(done)** | `leap.h`; the embedded tzdata table, `gchron_zonedb_default()`'s version comparison, and the Windows zone mapping; `WINDOWS-TODO.md` entries | M | **M5: Windows; metrology** - reached for metrology; Windows needs a Windows machine, and `WINDOWS-TODO.md` 6b and 6c say what done is |

Each phase ends with `make test`, `test-valgrind`, `test-asan`, `fuzz` and
`check-symbols` clean from an empty build directory, serially and under
`-j`, per `CONVENTIONS.md` §12 item 10.

**What phase 4 built, and what it found.** `leap.h` - the leap-second table,
`GCHRON_TaiInstant`, and the conversions between UTC and TAI - together with
the embedded time-zone database, `gchron_zonedb_default()`'s choice between it
and the system one, and the Windows zone mapping.

`GCHRON_LEAP_TABLE` had been declared since phase 0 and had answered
`GCHRON_ERR_UNSUPPORTED` ever since, because a level that quietly behaved as
`GCHRON_LEAP_MINUTE` would give a caller a strict check that passed for the
wrong reason. It now consults a table, and refuses `1999-06-30T23:59:60Z` -
when no leap second occurred - while accepting `1998-12-31T23:59:60Z`, which
is the whole difference between the two levels. Three things about it are
worth stating:

- **The table is a parameter, not an ambient fact.** `GCHRON_ParseOptions`
  carries it, and `parse.h` forward-declares the type rather than including
  `leap.h`, so a caller who only parses text does not link the module.
  Selecting the level without supplying a table is `GCHRON_ERR_INVALID` - the
  same refusal as before, for the same reason.
- **The question is about the UTC day, not the local one.** The suite's own
  `1998-12-31T15:59:60.123-08:00` is a leap second belonging to the next UTC
  day, so the date the table is asked about is the one after the offset is
  applied.
- **A `full-time` cannot be checked at all.** It has no date, and the table's
  question is about a date, so a `:60` under this level is
  `GCHRON_ERR_UNSUPPORTED` rather than silently settled at `MINUTE`'s
  strictness.

The table is checked against the tzdb's *other* leap-second file. The built-in
one is generated from `leap-seconds.list`, so checking it against that would
prove only that the generator can read back what it wrote; `leapseconds` is
the same facts restated in zic's syntax by zic's maintainers, and agreeing
with it is evidence. The complement is checked too - every day from 1972 to
the table's expiry that is *not* in the list must not be a leap day - because
a `gchron_leap_is_leap_day()` that simply answered "yes" would pass a list of
positives.

**The defect phase 4 found was in phase 0's code.**
`gchron_parse_options_default()` assigned its fields one at a time, so the
`leap_table` field this phase added was left holding whatever was on the
caller's stack: a garbage pointer, returned under the name of a default. The
same shape was in `gchron_write_options_default()` and
`gchron_parse_info_clear()`. All three now zero the struct wholesale, which
works precisely because section 3.7's "zero is strict" is true of every field
- each policy enum's zero is its `REJECT`. Assigning the fields is the kind of
correct that stops being correct when somebody adds a field. The regression
test fills each struct with `0xAB` first, so a forgotten field shows up as
that byte rather than as a zero that happened to be there.

**What the embedded database is for.** There is no `/usr/share/zoneinfo` on
Windows, so `gchron_zonedb_system()` fails there by design and
`gchron_zonedb_default()` falls through - which makes the embedded path the
one every Windows caller uses and the one Linux exercises least. It is 485
zone names over 436 distinct TZif images, generated by `tools/tzdata/embed.py`
and **not committed**: it is 2.3MB of C, it regenerates in under a second, and
a copy in the repository would be one distribution's snapshot in every diff.

It can do one thing a directory-backed database cannot. TZif has nowhere to
record that `America/Atka` is a link to `America/Adak`, so a database that
walks a directory has no way to know - but the generator can see that the file
is a symbolic link, so an embedded database answers
`gchron_zone_canonical_id()` truthfully. Each says what it actually knows
rather than inventing the rest.

`gchron_zonedb_default()` now chooses by currency. tzdata releases are `YYYYx`
and order lexically, so the comparison is `strcmp` and not an approximation of
one; ties and unknown versions both go to the system database, on the
reasoning that the operating system's copy is the one somebody is updating.

**The Windows mapping, and the gap it exposed.** CLDR's `windowsZones.xml`
gives 139 Windows zone names - `"Pacific Standard Time"` and the rest - and
`tools/tzdata/windows_zones.py` turns it into a committed table.
`tools/tzdata/fetch-cldr.sh` downloads it; the build never reaches the
network, which is why this one table is committed while the tzdata one is not.

Checking those 139 identifiers against the embedded database found a real
gap, and then a larger one behind it. Seven of them - `Asia/Calcutta`,
`Europe/Kiev`, `America/Godthab` and four more - are backward-compatibility
names, and Debian ships those as a separate `tzdata-legacy` package that is
not installed by default. So the embedded table, built from the files present
in the zoneinfo directory, lacked precisely the names Windows would hand it.

The larger gap was that **the same was true of every database built from a
directory**, which on Linux is the one `gchron_zonedb_default()` picks. A
caller on a stock Debian could not open `Asia/Calcutta`, `Europe/Kiev` or
`US/Eastern` at all - names that CLDR, Java and a great deal of existing
configuration still use - while the embedded database could. The default
therefore answered differently depending on which source it had chosen, which
is exactly what §6.2 says must not happen.

Both are fixed from the same place: `tzdata.zi`, which ships with base tzdata,
lists every link the tzdb defines, and this library already read that file for
its version string. The generator folds those links into the embedded table
(485 zone names became 598), and a directory-backed database consults them
when a file is not there - for the lookup, for the listing, and for
`gchron_zone_canonical_id()`, which a directory-backed database could not
previously answer at all. The two sources now agree on which names exist and
on what each resolves to, which is what a test asserts.

The test sweeps every row of the Windows mapping rather than sampling,
because the seven missing names were exactly the ones a spot check would not
have thought to include.

The Windows branch of `gchron_zonedb_local()` is written, marked
`TODO(windows):`, and listed in `WINDOWS-TODO.md` as 6b and 6c. Nothing here
has been run on Windows and, per `CONVENTIONS.md` section 11, it is not
claimed to work: "done" is a machine set to Pacific Standard Time returning
`America/Los_Angeles`.

**What phase 3 built, and what found its defects.** `format.h`: the LDML
(TR35) pattern compiler, `strftime` lowered onto the same item list, the
eleven named formats, and the names provider that keeps CLDR data out of the
library. With it, `interop.h`'s thirteen foreign encodings and `clock.h`.

The differential against ICU's `SimpleDateFormat` - 89 patterns across 7
zones and 6 instants, 3,639 comparisons - found four disagreements, all of
them chron's:

1. **Offset seconds were dropped.** `Z`, `ZZ`, `ZZZ`, `O` and `OOOO` wrote
   hours and minutes only. The tzdb records pre-standard local mean time to
   the second, so `Europe/Amsterdam` before 1937 is `+00:19:32`, and every
   one of those instants printed a different time than it named.
2. **`ZZZZZ` did not write `Z` at a zero offset.** It follows `X`'s rule,
   not `Z`'s - which is the one difference that makes it a separate spelling.
3. **The week letters used ISO rules unconditionally.** `w` and `W` are
   locale-dependent: the first day of the week and the minimal days in the
   first week both come from the locale, and only a locale that says Monday
   and four agrees with ISO. The rule is now transcribed from ICU's
   `Calendar::weekNumber` and driven from the provider, so a provider that
   says Sunday-and-one gets Sunday-and-one.
4. **`EEEEEE` had nowhere to read from.** The short weekday is a sixth width,
   distinct from the narrow one; `GCHRON_NAME_SHORT` was added for it.

Three divergences remain and are stated rather than fixed: `z`, `zz` and
`zzz` print the tzdb abbreviation (`EDT`) where CLDR root, having no zone
names at all, falls back to `GMT-4`. A provider with CLDR data would print
what CLDR says. The gate knows about these three and fails on a fourth.

The fifth defect came from `fuzz_format`, which asserts that the bound
`gchron_format_max_length` declares really bounds the output:

5. **The declared bound omitted the sign on a padded year.** `u` and `Y`
   write a negative year's sign *outside* the zero padding, so a count past
   nine digits - `uuuuuuuuuuu` on year -1 - needed one byte more than the
   library had promised. A caller who allocated exactly what it was told got
   `GCHRON_ERR_LIMIT` from a buffer sized to its own contract. `y` was never
   affected: the year of the era is always positive. The regression test
   sweeps every letter at every count the compiler accepts, against the
   values whose fields are widest, and asserts a buffer of exactly the
   declared size is always enough - 2,880 combinations.

**What phase 3 found in its own gates.** Two of them could not do their job,
and neither failure was visible from a passing run:

- **The ASan build had no header dependency tracking.** Its rules were
  written without `-MMD -MP`, and because it builds into its own directory
  nothing in the ordinary build's graph reached it. `GCHRON_Limits` grew a
  field in this phase, and ASan reported a stack-buffer-overflow in
  `gchron_limits_default` writing past a `GCHRON_Limits` local - a genuine
  overflow of a phase-1 struct by a phase-3 function, in objects that should
  have been rebuilt. The gate that exists to find memory errors was the only
  build capable of manufacturing them. The fuzz build had the same hole.
- **`test-valgrind` could not fail.** It was a bare `for` loop, which reports
  the exit status of its *last* iteration, so the target passed whenever the
  alphabetically-last suite passed regardless of the others. `--error-exitcode=1`
  had been set the whole time and had nothing to report to. It now collects
  the failures and names them, and was verified to fail on a failing first
  suite followed by a passing one - the exact shape that used to slip through.

**What phase 3 deliberately did not build.** No CLDR data ships with the
library: `gchron_names_english()` is the root locale and nothing else, and
§8.5's provider is the whole of the localisation story until a consumer needs
more. Leap seconds stay absent - `GCHRON_LEAP_TABLE` still answers
`GCHRON_ERR_UNSUPPORTED` rather than quietly behaving as `GCHRON_LEAP_MINUTE`.

**What phase 2 built, and the two defects its own properties found.** The
Julian, hybrid and tabular calendars; duration arithmetic in full - `add`,
`until`, `balance`, `round` - and the permissive ISO 8601 duration grammar
that phase 0 deferred. `convertdate`, which implements Reingold and
Dershowitz's algorithms, supplies 35,906 Julian vectors and 120 rows around
the three cut-overs.

The two defects were both found by the round-trip property
`from + until(from, to) == to`, which is the whole contract of a difference:

1. **`until` composed its units differently from `add`.** `gchron_datetime_add`
   applies years and months *together*, so `2000-02-29 + P26Y6M` is 2026-08-29
   - the day survives because August has thirty-one. A `until` that found the
   years first, re-anchored on the clamped 2026-02-28, and then found the
   months from there produced `P26Y6M23D`, which adds back up to the wrong
   day. `until` is now a refinement loop in which every probe re-adds the
   **whole accumulated duration** from the start, so the two compose
   identically by construction.
2. **A difference held in nanoseconds spans only 292 years.** `int64_t`
   nanoseconds reach 9.2e18, and three centuries is 9.5e18; every pair further
   apart than that failed with `GCHRON_ERR_RANGE`. Differences and additions
   now carry seconds plus a nanosecond remainder, which spans 292 *billion*
   years - comfortably past what a nine-digit year can express.

Both are the kind of defect no oracle finds, because no oracle is asked
whether a library agrees with itself.

**Where phase 2 departs from this page.** §5.2 says every function taking a
`GCHRON_Date` takes a calendar argument, with `NULL` meaning Gregorian. The
calendar-taking functions are in `calendar.h` instead, and `civil.h`'s keep
their shorter Gregorian-only signatures - `gchron_date_to_epoch_day(date,
&day)` rather than `gchron_date_to_epoch_day(NULL, date, &day)`. `text`,
`compress` and `image` want the Gregorian case and nothing else, and a
parameter they would always pass `NULL` to is noise in the header they
actually read. The rule §5.2 states still holds wherever a calendar is
accepted.

§5.5 sketches a `leap_years_in_cycle` field beside the leap pattern.
`GCHRON_LeapRule` does not have one: it is a popcount of the pattern, two ways
of saying one thing are two ways for them to disagree, and this suite has the
scar from `regex`'s unread table flags.

**What phase 1 built, and what it found.** The TZif reader, the POSIX TZ
footer, the database and its cache, `Resolve`, the local zone, `zoned.h` and
RFC 9557. Three oracles agree with it: `zdump` over 8,540 transition rows of
twenty zones; glibc's `tzset` over 548,960 probes covering **every distinct
footer rule in the system database**, harvested rather than typed; and
Python's `zoneinfo` over 105,948 probes covering **every zone**, run by `make
check-oracle-zoneinfo`.

The fuzzers found three defects the oracles could not, because all three need
input no real database contains:

1. A seventy-four byte file whose header claimed 987,654,144 transitions. The
   reader sized an allocation from the count before checking the file was big
   enough to hold them, and asked for eight gigabytes. A limits field would
   not have caught it - zero means *no limit*, which is a legitimate setting.
   The counts are now bounded by the file's own length, which no header can
   inflate.
2. `BST5CDT,M1.1.0/0,M1.1.0/1`, whose two changeovers land on the same
   instant: the daylight-saving span is empty, so nothing ever changes. The
   offset lookup already said so and the transition search did not.
3. `BSTST5CDT1,M1.1.0/0,M1.1.1`, which runs daylight saving from the first
   Sunday of January to the first *Monday* of January - so which changeover
   comes first flips from year to year and the span wraps a year boundary.
   The transition search deduced the offsets either side from which end of
   the rule produced the candidate, which is only equivalent while the span
   stays inside one year.

The third is the one worth generalising. `gchron_posixtz_next_transition` now
**reads the two sides back out of the offset lookup** rather than deducing
them, so the two agree by construction; and a candidate whose two sides come
out identical is not reported at all, because a transition that changes
nothing is not a transition. The fuzz harness asserts exactly that invariant,
which is why it found the case rather than merely surviving it - a harness
that only checked for crashes would have passed all three of these.

**What phase 1 deliberately did not build.** `gchron_zonedb_embedded()`
returns `GCHRON_ERR_UNSUPPORTED` until phase 4 generates its table, rather
than an empty database that answers every lookup with the same code and hides
the reason; `gchron_zonedb_default()` therefore has only one candidate and
nothing to compare versions of. `gchron_zone_canonical_id()` returns the
identifier a zone was asked for, because TZif has nowhere to record what a
link points at and the canonical-name table arrives with the embedded
database. The Windows branches are written, marked `TODO(windows):` and listed
in `WINDOWS-TODO.md`.

**Where phase 1 departs from this page.** §13's layout puts every grammar in
`parse.h`; RFC 9557 is declared in `zoned.h` instead, and implemented in
`src/zone/rfc9557.c`. Resolving `[Europe/Paris]` needs a zone database, so
putting it in `parse.h` would have meant that header reaching tier 2 - and the
property the tier rule exists to protect is precisely that a consumer parsing
RFC 3339 timestamps never sees a zone type. The `GCHRON_ZoneConflict` policy
stays in `parse.h` beside the other three, so that §3.7's rule is still
checkable in one place.

**What phase 0 built, and what it deliberately did not.** `duration.h` carries
the type, its sign invariant, `GCHRON_Overflow`, and the RFC 3339 Appendix A
grammar in both directions - because M1's wording is "pass the JSON Schema
`format` vectors" and one of the four vector files is `duration.json`. The
arithmetic the plan assigns to phase 2 - `balance`, `until` with a largest
unit, applying a calendar unit, rounding - is absent rather than stubbed, and
so is the permissive ISO 8601 duration grammar. `GCHRON_LEAP_TABLE` is
declared and returns `GCHRON_ERR_UNSUPPORTED` until phase 4 builds the table,
rather than quietly behaving as `GCHRON_LEAP_MINUTE`: a caller who asked for
the strict reading and silently got the loose one has a check that passes for
the wrong reason.

---

## References

- RFC 3339, *Date and Time on the Internet: Timestamps* (2002); §4.3 on
  `-00:00`, §5.6 grammar, §5.7 restrictions.
- RFC 9557, *Date and Time on the Internet: Timestamps with Additional
  Information* (2024) - the `[Zone]` and `[u-ca=]` suffixes.
- RFC 8536, *The Time Zone Information Format (TZif)* (2019); `tzfile(5)`
  for the version-4 additions.
- POSIX.1-2024, *Environment Variables*, `TZ` - including the extended
  −167..167 hour range for transition times.
- RFC 9110 §5.6.7, *Date/Time Formats* (HTTP); RFC 5322 §3.3 and §4.3.
- ISO 8601-1:2019 and ISO 8601-2:2019.
- TOML v1.0.0, *Offset Date-Time*, *Local Date-Time*, *Local Date*, *Local
  Time*.
- Unicode TR35 Part 4, *Dates* - the LDML pattern letters.
- Howard Hinnant, *chrono-Compatible Low-Level Date Algorithms* -
  `days_from_civil` and `civil_from_days`, with their verified ranges.
- Edward M. Reingold and Nachum Dershowitz, *Calendrical Calculations: The
  Ultimate Edition* (2018) - the calendar algorithms and the sample-date
  tables used as vectors.
- 27th CGPM (2022), Resolution 4 - on the future of leap seconds.
- The Temporal proposal (TC39) - `disambiguation`, `overflow`, the unified
  duration, the treatment of `:60`; and its authors' record of what `Date`
  got wrong.
- Stephen Colebourne, *java.time* design notes, and ThreeTen-Extra's
  `UtcInstant`/`TaiInstant` for the leap-second-aware types kept out of the
  core.
- Jon Skeet, *Noda Time* design rationale - on the separation of
  `Instant`, `LocalDateTime` and `ZonedDateTime`.
