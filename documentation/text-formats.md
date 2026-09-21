# The text formats `chron` reads and writes

**Status:** describes what phases 0 through 4 shipped, plus the YAML 1.1
timestamp added for `text`. The grammars `documentation/design.md` §8.1 lists
but this page does not - the Unix integer forms - are absent rather than
stubbed.

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
  expanded year belongs to the ISO 8601 profile, below.
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
revisited against `toml-test`; until then the strict default stands,
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

## ISO 8601 durations

`gchron_parse_iso8601_duration()` reads the full grammar, which RFC 3339
Appendix A's is a strict subset of. The two differ in ways worth stating,
because a caller that assumes they are the same grammar will accept text one
of its peers rejects:

- **A sign is permitted**, both on the whole duration and inside a component:
  `-P1D` and `P-1D` (the latter is ISO 8601-2). Appendix A allows neither.
- **Components may be skipped and mixed freely**: `P1Y2D` and `PT1H2S` are
  fine, and `P1W2D` puts weeks alongside the rest, which ISO 8601-1:2019
  §5.5.2 permits and Appendix A does not.
- **A component may exceed its usual range.** `PT36H` is legal and is *not*
  normalised on the way in; `gchron_duration_balance()` is what converts it,
  and only when asked. A parser that silently balanced would destroy the
  distinction between `PT36H` and `P1DT12H`, which across a DST transition
  are different lengths of time.
- **A fraction is accepted on seconds only.** `PT0.5S` is fine; `P1.5Y`,
  `PT1.5H` and the rest are `GCHRON_ERR_UNSUPPORTED` with
  `GCHRON_DIAG_DURATION_FRACTION`. On a calendar unit that is mistake M9 -
  half a year has no length until something says which year. On an hour or a
  minute it would be exact, but representing it means pushing the remainder
  into a lower field, and that is a balance this parser does not perform for
  the reason just given. The refusal is `UNSUPPORTED`, not `FORMAT`: the text
  is valid ISO 8601 and this library declines to represent it, which is a
  different thing from the text being wrong.

The writer emits the shortest correct spelling: no component that is zero
unless the whole duration is (`PT0S`), and no trailing zeros in a fraction -
`PT0.5S`, not `PT0.500S`.

---

## YAML 1.1 `!!timestamp`

Implemented in full: `gchron_parse_yaml_timestamp` and
`gchron_write_yaml_timestamp`, reporting which of three shapes it read in
`GCHRON_YamlValue::kind`.

YAML defines this type as a **regular expression**, not as a grammar built on
RFC 3339, and the expression is the definition: a scalar it does not match is
a `!!str`, and a reader that disagrees resolves a different tag than every
other YAML 1.1 reader - which changes what the document *says*, not merely how
it is spelled.

```
 [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]
|[0-9][0-9][0-9][0-9]
 -[0-9][0-9]?
 -[0-9][0-9]?
 ([Tt]|[ \t]+)[0-9][0-9]?
 :[0-9][0-9]
 :[0-9][0-9]
 (\.[0-9]*)?
 (([ \t]*)Z|[-+][0-9][0-9]?(:[0-9][0-9])?)?
```

| Shape | Example | `kind` |
| --- | --- | --- |
| Date | `2001-12-14` | `GCHRON_YAML_DATE` |
| Date-time, no zone | `2001-12-14T21:59:43` | `GCHRON_YAML_DATE_TIME` |
| Date-time with a zone | `2001-12-14T21:59:43Z` | `GCHRON_YAML_OFFSET_DATE_TIME` |

There is no time-only shape. Every YAML timestamp begins with a date, which is
the difference design §11 records between this grammar and TOML's - TOML's
*Local Time* is the one shape a YAML timestamp cannot hold.

### How it differs from RFC 3339

Enough that it is its own production rather than RFC 3339's with flags. Five
independent switches on one scanner is the arrangement that makes a caller
enable four things to get the one they wanted.

| | RFC 3339 | YAML 1.1 |
| --- | --- | --- |
| Month, day | `2DIGIT` | `1*2DIGIT` - **but only with a time**, below |
| Hour | `2DIGIT` | `1*2DIGIT` |
| Minute, second | `2DIGIT` | `2DIGIT` - unchanged |
| Separator | `T` | `T`, `t`, or **one or more** spaces or tabs |
| Fraction | `"." 1*DIGIT` | `"." *DIGIT` - `21:59:43.` is a time |
| Offset | required, `±HH:MM` | optional; `±H`, `±HH`, `±H:MM`, `±HH:MM` |
| `z` | accepted | **refused**; see below |

### The date-only form is the strict one

YAML's *first* alternative is `YYYY-MM-DD` with both fields exactly two digits.
Only the second - the one carrying a time - relaxes them. So:

| Text | |
| --- | --- |
| `2001-12-14` | a timestamp |
| `2001-12-4` | **a string** |
| `2001-12-4T21:59:43Z` | a timestamp |

That reads as an inconsistency and is what the type repository says. A parser
that smoothed it over would resolve as `!!timestamp` a scalar every other
YAML 1.1 reader resolves as `!!str`. The refusal carries
`GCHRON_DIAG_YAML_DATE_WIDTH` rather than "expected a digit", because the
field is present and the *form* is wrong.

### `t` yes, `z` no

The expression writes `[Tt]` for the separator and a bare `Z` for the zone.
PyYAML's copy agrees. So `2001-12-14t21:59:43Z` is a timestamp and
`2001-12-14T21:59:43z` is a string.

This is the reverse of RFC 3339, where RFC 5234 §2.3 makes every ABNF literal
case-insensitive and `z` is as conformant as `Z`. A parser that carried the
habit from one grammar to the other would accept a scalar YAML treats as a
string; the refusal has its own diagnostic, `GCHRON_DIAG_YAML_LOWERCASE_Z`,
because "expected `Z` or an offset" reads as though nothing were there at all.

### Deviation: whitespace before a numeric offset

**This library accepts `2001-12-14T21:59:43 -05:00`. The published expression
does not.**

The expression places `[ \t]*` inside the `Z` branch alone - `(([ \t]*)Z|[-+]…)`
- so a space may precede `Z` and may not precede `+05:00`. PyYAML hoists the
same `[ \t]*` outside the whole group, and so accepts both. The two disagree,
and something has to be accepted or refused.

Accepting is the decision here, for three reasons. PyYAML is the reference
implementation of YAML 1.1 and the parser most existing 1.1 documents were
written against, so its reading is the one those documents assume. The
published expression offers no reason why a space should be readable before
`Z` and not before an offset, which reads as an oversight in a regex rather
than a statement about the language. And accepting means this library reads
every document either of them reads, where refusing would mean rejecting
documents PyYAML has been accepting for twenty years.

It is a deviation all the same, which is why it is written down here rather
than left as a quiet permissiveness. **The writer never emits one**, so text
normalised through this library is conformant under either reading.

### Deviation: an offset minute of 60

`2001-12-14T21:59:43+05:60` is `GCHRON_ERR_FORMAT` here, and PyYAML reads it
as `+06:00`.

The expression's `(:[0-9][0-9])?` does not check the field's range, exactly as
it does not check that February has thirty days. This library refuses the
sixtieth minute as it refuses the thirtieth of February, rather than carrying
it into the hour and producing a value the document did not write. The case is
named in `tests/conformance/test_yaml_timestamp.cpp`'s `DEVIATIONS` table, so
it fails the differential unless it stays deliberate.

### A zoneless timestamp is not UTC

The type repository's canonical form is UTC, and the tempting shortcut is to
fold a zoneless reading into it on the way in. `GCHRON_YAML_DATE_TIME` keeps
the civil reading instead, because converting it would assert a zone the
document did not write - design.md's mistake M1, in the one place a document
format makes it easy to commit. A caller that knows which zone the document
meant resolves it through `zoned.h`, where the conversion can say that the
reading names no instant, or two.

### `:60` and a long fraction

YAML states nothing about either, and the grammar permits both:
`[0-9][0-9]` matches `60`, and `\.[0-9]*` has no length limit. The preset
`gchron_parse_options_yaml()` therefore reads them rather than refusing -
`GCHRON_LEAP_CLAMP` and `GCHRON_FRACTION_TRUNCATE` - with
`GCHRON_ParseInfo::leap_second` and `::fraction_truncated` as the evidence.
This is the one grammar here whose preset is looser than design §3.7's strict
zero, and the reason is the one above: a refusal would resolve a conformant
`!!timestamp` as a `!!str`, which reports nothing and changes the document.
A caller who has decided the question passes the level they want.

Note that `GCHRON_LEAP_MINUTE` and `GCHRON_LEAP_TABLE` **refuse a `:60` that
carries no offset**. YAML is the one grammar here whose timestamp may omit the
zone, so it is the one where "is this 23:59 in UTC?" can have no answer, and
assuming UTC to get one would be inventing the missing half.

### What the writer emits

The canonical spelling of whichever shape the value carries: four-digit year,
two-digit everything else, `T` between the date and the time, and the shortest
fraction that loses nothing at any width - `.1`, not `.100`, which is what the
type repository's own canonical example shows and what
`GCHRON_FRACTION_DIGITS_SHORTEST` means. None of the relaxed input spellings
survives a round trip, deliberately: a writer that reproduced them would emit
documents a strict reader is entitled to read as strings.

`GCHRON_YamlValue::offset_is_z` decides `Z` against `+00:00` for a zero
offset, so a document normalised through this library keeps the spelling it
arrived with rather than being silently restyled; `-00:00` is written back as
itself, because that spelling is the only one that says the offset is unknown.

### The oracle

`tools/oracle/yaml_timestamp.py` runs a corpus through **PyYAML** and commits
the verdicts to `tests/data/vectors/parse/yaml_timestamp.vec`; `make
vectors-yaml` regenerates it. Both of PyYAML's regular expressions are read
out of the installed module rather than retyped - the resolver's, which says
whether a scalar is a timestamp at all, and the constructor's named groups,
which say what its fields are.

The value comparison uses those groups rather than the `datetime` PyYAML goes
on to build, because that object is truncated to microseconds and normalises
an offset minute of 60 into an extra hour; comparing against it would test
Python's arithmetic rather than this library's reading of the text. The two
cases Python cannot represent at all - a `:60` second - are counted and
printed rather than dropped.

---

## HTTP-date (RFC 9110 §5.6.7)

Three formats, because HTTP has accumulated three. A recipient must read all
of them; a sender must write only the first:

| Form | Example |
| --- | --- |
| IMF-fixdate | `Sun, 20 Sep 2026 17:30:00 GMT` |
| RFC 850 (obsolete) | `Sunday, 20-Sep-26 17:30:00 GMT` |
| `asctime()` (obsolete) | `Sun Sep 20 17:30:00 2026` |

`gchron_write_http_date()` always writes IMF-fixdate and always GMT, and
converts a value with any offset first.

**RFC 850's two-digit year needs to know what year it is now.** The RFC's rule
- a timestamp more than fifty years in the future means the most recent past
year with those two digits - is not a fixed mapping, so this parser takes a
`GCHRON_Clock`. Nothing in this library reads the system clock on its own
(design.md mistake M18); a rule that depended on an untestable ambient value
would be a rule nobody could check until the year it started mattering. A
`GCHRON_FixedClock` makes the boundary an ordinary test.

**The day-of-week is parsed and ignored.** `Mon, 20 Sep 2026` is accepted even
though that date is a Sunday: the date fields decide, and the name is
redundant. RFC 9110 does not ask a recipient to check it, and a sender that
got it wrong has still said unambiguously which day it meant. A caller that
wants to be stricter than HTTP can call `gchron_date_day_of_week()` on the
result - though it would have to re-read the name out of the input itself,
because `GCHRON_ParseInfo` does not carry it.

---

## RFC 5322 (2008)

The `Date:` header of an email: section 3.3's grammar plus section 4.3's
obsolete forms - two-digit years, folding whitespace, parenthesised comments
(which nest), and the alphabetic zones.

- **`-0000` means the offset is unknown**, exactly as RFC 3339's `-00:00`
  does, and sets `offset_unknown` (mistake M14). The time is UTC; what is
  missing is the sender's relationship to it.
- **The obsolete zone names follow section 4.3's own table.** `UT` and `GMT`
  are +0000, `EST` -0500, `EDT` -0400, and so on. **Every other single letter
  is +0000 with the offset marked unknown** - which is what the RFC instructs,
  because the military zones were so widely implemented backwards that the
  letter carries no information.
- The two-digit year takes a clock, for the same reason HTTP-date's does.

---

## LDML (Unicode TR35) and `strftime` patterns

Two pattern languages, one compiled representation. `gchron_format_compile()`
turns either into a list of items, so `strftime`'s `%Y` and LDML's `uuuu` are
the same item and are emitted by the same code.

LDML is checked against ICU's `SimpleDateFormat` - 89 patterns across 7 zones
and 6 instants - and agrees on all 3,639 comparisons with three stated
exceptions: `z`, `zz` and `zzz` print the tzdb abbreviation (`EDT`) where
CLDR's root locale, having no zone names, writes `GMT-4`. A names provider
carrying CLDR data would print what CLDR says; §8.5 is where that plugs in.
`strftime` is checked against glibc's under the C locale.

Two things about patterns are worth stating because they surprise callers:

- **A pattern is text and a length, like everything else here.** It may
  contain a NUL, which becomes a literal NUL in the output - so the length
  the writer reports is the authority, and `strlen` on the result is not.
- **`gchron_format_max_length()` is a promise.** A buffer of exactly the
  size it declares is always enough for any value, and the test sweeps every
  letter at every count to say so.

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
