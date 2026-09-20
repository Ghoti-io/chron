# The text formats `chron` reads and writes

**Status:** describes what phases 0 and 1 shipped. The grammars
`documentation/design.md` §8.1 lists but this page does not - ISO 8601's
profile, YAML 1.1, HTTP-date, RFC 5322, the Unix integer forms - are later
phases and are absent rather than stubbed.

`CONVENTIONS.md` §9 asks a library implementing an external standard to name
the version it implements and its deviations. This page is that, one section
per grammar.

Every parser here takes **the text and its length**. Nothing is NUL-terminated
by assumption: these grammars are read out of documents, and a parser that
trusts a terminator reads past the end of the one document that has none.
Every parser refuses trailing characters unless
`GCHRON_ParseOptions::allow_trailing` says to report where it stopped instead.

---

## RFC 3339 (2002)

Implemented: §5.6's `date-time`, `full-date` and `full-time`, with §5.7's
restrictions, and Appendix A's `duration`.

| Production | Function | Produces |
| --- | --- | --- |
| `date-time` | `gchron_parse_rfc3339_date_time` | `GCHRON_OffsetDateTime` |
| `full-date` | `gchron_parse_rfc3339_full_date` | `GCHRON_Date` |
| `full-time` | `gchron_parse_rfc3339_full_time` | `GCHRON_OffsetTime` |
| `duration` (Appendix A) | `gchron_parse_rfc3339_duration` | `GCHRON_Duration` |

and `gchron_write_rfc3339_*` for each, with the output contract in §8.6 of the
design: the buffer receives a NUL-terminated string, `out_len` is its length
without the NUL, and a buffer too small is `GCHRON_ERR_LIMIT` with `out_len`
still set - so a zero-length buffer is how a caller asks for the length alone.

### What the grammar is strict about, and why each is not a bug

- **The year is exactly four digits, with no sign.** `date-fullyear = 4DIGIT`.
  `+2020-01-01`, `12020-01-01` and `998-01-01` are ISO 8601 questions, and the
  expanded year belongs to the ISO 8601 profile in phase 2.
- **The seconds are required.** `1985-04-12T23:20Z` is not a `date-time`.
- **The offset is required**, and is `Z` or `+HH:MM`. `08:30:06+0130` and
  `08:30:06+01` are both refused: `time-numoffset` has the colon and has the
  minutes.
- **The day is checked against the month and the year, in the parser.**
  `2020-02-30`, `2021-02-29` and `2020-01-32` all fail at the day field. A
  parser that deferred the question to a later validity check would report a
  `format` result that disagreed with its own value.
- **`time-numoffset` is built from `time-hour` and `time-minute`**, which are
  00-23 and 00-59. So `+24:00` and `+00:60` are refused, and `+23:30` is not.

### `t` and `z` are not an extension

RFC 5234 §2.3 makes ABNF string literals case-insensitive, so
`1963-06-19t08:30:06z` is exactly as conformant as the uppercase spelling and
is accepted with no option. The design page said otherwise in an earlier draft
and has been corrected; the JSON-Schema-Test-Suite requires the lowercase form
to be valid, which is how the misreading surfaced.

The **space** separator is different: it comes from §5.6's *note* ("applications
may choose to separate them with a space"), not from the ABNF, so it is
`GCHRON_ParseOptions::allow_space_separator` and is off by default.

### `-00:00` means something

§4.3: `-00:00` says the local offset is **unknown**, which `Z` and `+00:00` do
not say. The parser sets `GCHRON_OffsetDateTime::offset_unknown`, every
operation that keeps the offset preserves it, and the writer writes it back as
`-00:00` whatever the write options say - because that spelling is the only one
that carries the meaning. `gchron_offset_compare` says the two are the same
moment; `gchron_offset_identical` says they are not the same statement.

### `:60`

The grammar permits it (`time-second = 2DIGIT ; 00-58, 00-59, 00-60 based on
leap second rules`), and the rules it defers to are a policy here:
`GCHRON_Leap`, whose zero value refuses. Whatever the policy allows, the
resulting `GCHRON_Time` holds `:59` of the same minute with the same fraction -
where the Linux kernel puts the repeated second - and
`GCHRON_ParseInfo::leap_second` is the evidence that the text said otherwise.
That is the one documented loss in `parse(write(x))`.

`GCHRON_LEAP_MINUTE` judges the second **after applying the offset**, because
`01:29:60+01:30` is a leap second and `23:59:60+01:00` is not, and the digits
before the offset are the same shape in both.

### Fractions

`time-secfrac = "." 1*DIGIT`, so `08:30:06.Z` is not a time. RFC 3339 does not
say what to do with more digits than an implementation holds, so the default
refuses and `GCHRON_FRACTION_TRUNCATE` is asked for by name. Truncation never
rounds: a parser that rounded `23:59:59.9999999999` into the next day would
have changed the date.

### Appendix A's `duration` is not ISO 8601's

This is the deviation most worth writing down, because the two look alike:

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

The productions **nest**, and there is no sign and no fraction. So:

| Text | Appendix A | Why |
| --- | --- | --- |
| `P1Y2M`, `P1M2D` | valid | each unit may be followed by the next one down |
| `P1Y2D` | **invalid** | `dur-year` may be followed only by `dur-month` |
| `PT1M2S` | valid | |
| `PT1H2S` | **invalid** | `dur-hour` may be followed only by `dur-minute` |
| `P2W` | valid | |
| `P1Y2W`, `P1WT1H`, `P0Y1W` | **invalid** | `dur-week` is the whole duration or nothing |
| `PT0.5S`, `PT0,5S` | **invalid** | no fraction |
| `-P1D`, `P-1D` | **invalid** | no sign |
| `P`, `PT`, `P1YT` | **invalid** | a duration has at least one component |
| `P01D` | valid | `1*DIGIT`, so a leading zero is fine |

The writer emits the intermediate zeros the nesting requires: a duration of one
year and two days has no spelling that skips the months, so it writes
`P1Y0M2D`. A negative duration, a duration with a nanosecond part, and a
duration mixing weeks with anything else are `GCHRON_ERR_UNSUPPORTED` - phase
2's ISO 8601 writer spells the first two, and the third has no Appendix A form
at all. A duration of zero writes `PT0S`, the shortest spelling the grammar
permits.

**A component too large for `int64_t` is `GCHRON_ERR_RANGE`, not
`GCHRON_ERR_FORMAT`.** `P999999999999999999999999D` *is* a duration; no integer
holds it. A caller performing a JSON Schema `format` check treats
`GCHRON_ERR_RANGE` as a pass and a caller wanting the value does not, which is
why the two codes are different.

---

## TOML v1.0.0

Implemented: all four of *Offset Date-Time*, *Local Date-Time*, *Local Date*
and *Local Time*, through one function, `gchron_parse_toml`, which reports
which of the four it read in `GCHRON_TomlValue::kind`. TOML distinguishes them
by which fields are present rather than by a tag, so a parser that reads any of
them has to say which.

TOML's Local Time is the shape a YAML timestamp cannot hold, and the reason a
library with only "date-time with an offset" cannot read TOML.

| TOML type | Example | `kind` |
| --- | --- | --- |
| Offset Date-Time | `1979-05-27T07:32:00Z` | `GCHRON_TOML_OFFSET_DATE_TIME` |
| Local Date-Time | `1979-05-27T07:32:00` | `GCHRON_TOML_LOCAL_DATE_TIME` |
| Local Date | `1979-05-27` | `GCHRON_TOML_LOCAL_DATE` |
| Local Time | `07:32:00` | `GCHRON_TOML_LOCAL_TIME` |

### Deviations from RFC 3339, as TOML states them

- **A space may replace `T`.** TOML permits it outright, so
  `gchron_parse_options_toml()` turns it on.
- **A long fraction is truncated, not rounded.** TOML: "If the value contains
  greater precision than the implementation can support, the additional
  precision must be truncated, not rounded." So the preset passes
  `GCHRON_FRACTION_TRUNCATE`. `chron` holds nanoseconds, which is three digits
  more than the millisecond precision TOML requires an implementation to
  support.

`gchron_parse_toml` is the one grammar here whose `NULL` options mean the
preset rather than the strict default, because TOML states what its own options
are and a caller who named the grammar has already chosen them.

### The one thing TOML does not say

TOML defines its Offset Date-Time as "an RFC 3339 formatted date-time", whose
grammar permits `:60`, but says nothing about leap seconds itself. The preset
therefore leaves `GCHRON_Leap` at `GCHRON_LEAP_REJECT`, and a caller who has
decided the question passes the level they want. The setting is due to be
revisited against `toml-test` in phase 2; until then the strict default stands,
which is the rule in design §3.7 rather than a guess dressed as a default.

---

## RFC 9557 (2024)

Implemented in full: `gchron_parse_rfc9557` and `gchron_write_rfc9557`, in
`zoned.h` rather than `parse.h` because resolving a zone name needs a zone
database and `parse.h` is tier 1.

RFC 9557 is RFC 3339 plus annotations in square brackets:

```
2026-09-20T17:30:00+02:00[Europe/Paris]
2026-09-20T17:30:00+02:00[Europe/Paris][u-ca=iso8601]
2026-09-20T15:30:00Z[!Etc/UTC]
```

**This is the grammar that fixes mistake M13.** An RFC 3339 timestamp carries
an offset and not a zone, so `2026-03-08T01:30-05:00` cannot say it meant New
York - and next summer that same place is on `-04:00`, which the stored string
gives no way to work out. The annotation carries the name, and this is the
only text format in the library that round-trips a `GCHRON_ZonedDateTime`
whole.

### The three things the RFC asks of a reader

- **A `!` makes an annotation critical.** The writer is saying that ignoring
  it would change what the timestamp means, so a reader that does not
  understand it must refuse. `[!u-unknown=x]` is `GCHRON_ERR_UNSUPPORTED`;
  `[u-unknown=x]` is ignored.
- **The offset and the zone may disagree**, and which half to believe is the
  application's decision. `GCHRON_ParseOptions::zone_conflict` is the policy
  and its zero value refuses; `GCHRON_ZONECONFLICT_PREFER_OFFSET` keeps the
  instant the offset named, and `GCHRON_ZONECONFLICT_PREFER_ZONE` re-resolves
  the civil reading through the zone. Either way,
  `GCHRON_ParseInfo::offset_disagreed_with_zone` records that the text
  contradicted itself.
- **The annotation may be an offset rather than a name** - `[-05:00]`, `[Z]` -
  and then the zone is a fixed-offset one.

`[u-ca=...]` is copied into `GCHRON_ParseInfo::calendar`; acting on it is
phase 2's, and carrying it rather than dropping it is phase 1's.

### What the writer does with a zone that has no name

An anonymous zone - one built from a `TZ` rule, or read from an
`/etc/localtime` that is a plain file rather than a symlink - has no name to
annotate, and neither does a fixed-offset zone whose name would say nothing
its offset does not. Both write plain RFC 3339. Inventing a name would be the
alternative, and it would be a lie the next reader could not detect.

---

## The POSIX `TZ` rule string

Not a timestamp format, but a grammar this library reads: it is the footer of
every version-2 TZif file, the `TZ` environment variable, and - on a system
built with `zic -b slim`, which has been the default since 2020b - the thing
that answers every question about next year. `gchron_zonedb_posix()` builds a
zone from one.

Implemented per POSIX.1-2024, **including the extended -167..167 hour range**
for transition times, which means a changeover may be named in one week and
land in another. Both abbreviation spellings are accepted: three or more
letters, and anything alphanumeric inside angle brackets (`<+05>`), which is
how a zone whose abbreviation begins with a sign writes it.

The one deviation worth stating: **a daylight-saving name with no rule -
`EST5EDT` - is refused.** POSIX leaves the transition dates
implementation-defined there, and glibc falls back to a United States rule.
That is a guess about geography, and this library has no business making it.

---

## JSON Schema `format`

Not a grammar of its own: `format: date-time`, `date` and `time` are RFC 3339's
`date-time`, `full-date` and `full-time`, and `format: duration` is Appendix A's
`duration`. `gchron_parse_options_json_schema()` fills in the two policies the
keyword needs - `GCHRON_FRACTION_TRUNCATE` and `GCHRON_LEAP_MINUTE` - which is
what the JSON-Schema-Test-Suite's optional vectors require.

A `format` check asks whether text matches a grammar, **not** whether the value
fits in an integer, so a caller performing one treats `GCHRON_ERR_RANGE` as a
pass. `tests/conformance/test_jsonschema_format.cpp` says so in one place and
runs all 207 string cases of the suite's four optional format files through it.
