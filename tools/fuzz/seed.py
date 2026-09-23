#!/usr/bin/env python3
"""Rebuild the seed corpora the fuzzers start from.

A coverage-guided fuzzer that starts from nothing spends its first minutes
rediscovering that a timestamp has hyphens in it.  Seeding it with real input
skips that, and design.md section 12.2 says the corpora are seeded from the
vectors.

What is committed is **the seed corpus only** - what this script produces,
deterministically, from the committed vectors and from the machine's own
time-zone database.  The units libFuzzer discovers during a campaign are not
committed: a ninety-second run adds several thousand files, which is a great
many git objects for input that the next run would find again anyway, and
`cutil`'s six megabytes of tracked Doxygen output is the cautionary tale this
suite already has.

Running it is idempotent in the sense that matters: every seed is named by the
SHA-1 of its own bytes, so a second run on the same machine rewrites the same
names.  It is **not** reproducible across machines, and that is the whole
reason for the two modes below.

Two of the seeders - seed_zones() and seed_zonedir() - read the machine's own
time-zone database, so what they produce depends on how much of tzdata is
installed.  This script used to clear every directory before writing, which
meant running it on a machine with a thinner zoneinfo silently deleted the
seeds generated on a fuller one: here that was 2,286 seeds written against
3,759 committed, showing up as 1,545 deletions that looked exactly like the
intended "clear it first" behaviour.

So by default nothing is deleted.  Files this run did not write are kept and
counted, and because the names are content hashes, "did not write" is exact
rather than a guess.  A corpus can only grow, which is the right default for
a fuzzing corpus: an extra input costs a few bytes, and a deleted one may be
the only witness to a branch nobody has reached since.

    tools/fuzz/seed.py              # additive; keeps what it did not write
    tools/fuzz/seed.py --prune      # also delete those, and say how many

`--prune` is the old behaviour and is what sheds the thousands of units a
libFuzzer campaign leaves behind.  Run it only on a machine whose zoneinfo is
at least as complete as the committed corpus was built from, and check
`git status` before staging.

Copyright 2026 by Corey Pennycuff
"""

import hashlib
import os
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
CORPUS = ROOT / "tests" / "fuzz" / "corpus"

# The harness's first byte selects the grammar; the second drives the policies
# and the limits, so that a seed reaches both sides of the leap-second and
# fraction branches rather than only the default.
PARSE_SELECTORS = {"date_time": 0, "date": 1, "time": 2, "duration": 3}
PARSE_OPTIONS = (0x00, 0x05, 0x1B)
TOML_SELECTOR = 4


def unescape(text):
    """Reverse tools/oracle/jsonschema_format.py's escaping."""
    out = bytearray()
    i = 0
    while i < len(text):
        if text[i] == "\\" and i + 1 < len(text):
            if text[i + 1] == "\\":
                out.append(0x5C)
                i += 2
                continue
            if text[i + 1] == "x":
                out.append(int(text[i + 2:i + 4], 16))
                i += 4
                continue
        out.append(ord(text[i]))
        i += 1
    return bytes(out)


WRITTEN = {}


def reset(name):
    """Make sure the directory exists, and start recording what we write.

    It no longer empties the directory.  The name is kept because every
    seeder calls it and what it means - "this run owns this directory from
    here" - has not changed.
    """
    path = CORPUS / name
    path.mkdir(parents=True, exist_ok=True)
    WRITTEN.setdefault(path, set())
    return path


def write(path, blob):
    name = hashlib.sha1(blob).hexdigest()
    WRITTEN.setdefault(path, set()).add(name)
    (path / name).write_bytes(blob)


def sweep(path, prune):
    """Return (written, kept), deleting the kept ones when asked.

    A file is "kept" when this run did not write it: a unit from a campaign,
    or a seed written on a machine with more of tzdata installed than this
    one has.  The two are indistinguishable from here, which is exactly why
    the default is to leave them alone.
    """
    mine = WRITTEN.get(path, set())
    kept = 0
    for entry in sorted(path.iterdir()):
        if not entry.is_file() or entry.name == ".gitignore":
            continue
        if entry.name in mine:
            continue
        if prune:
            entry.unlink()
        else:
            kept += 1
    return len(mine), kept


def seed_parse():
    out = reset("parse")
    vectors = ROOT / "tests" / "data" / "vectors" / "parse"
    for path in sorted(vectors.glob("jsonschema_*.vec")):
        selector = PARSE_SELECTORS[path.stem.replace("jsonschema_", "")]
        for line in path.read_text().splitlines():
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) < 2:
                continue
            text = unescape(fields[1])
            for sel, options in [(selector, o) for o in PARSE_OPTIONS] \
                    + [(TOML_SELECTOR, 0x08)]:
                write(out, bytes([sel, options]) + text)
    return out


def seed_arith():
    out = reset("arith")
    # The values every checked-arithmetic helper has an edge at.
    edges = [0, 1, -1, 86400, -86400, 2 ** 62, -(2 ** 62), 2 ** 63 - 1,
             -(2 ** 63), 365241780471, -365243219162, 1000000000, 999999999]
    for op in range(12):
        for value in edges:
            negated = value if value == -(2 ** 63) else -value
            write(out, bytes([op]) + struct.pack("<q", value) * 2
                  + struct.pack("<q", negated))
    return out


def seed_zones():
    tzif = reset("tzif")
    posix = reset("posix_tz")
    root = pathlib.Path(os.environ.get("TZDIR", "/usr/share/zoneinfo"))
    seen = set()
    if root.is_dir():
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            data = path.read_bytes()
            if not data.startswith(b"TZif"):
                continue
            for options in (0x00, 0x07):
                write(tzif, bytes([options]) + data)
            if data.endswith(b"\n"):
                cut = data.rfind(b"\n", 0, len(data) - 1)
                if cut >= 0:
                    rule = data[cut + 1:-1]
                    if rule and rule not in seen:
                        seen.add(rule)
                        write(posix, rule)

    # Edges no real zone needs, and the two rules the fuzzer found defects
    # with - kept so that a fresh campaign starts from them rather than
    # rediscovering them.
    for rule in (b"UTC0", b"<+05>-5", b"EST5EDT,J1,J365", b"EST5EDT,0,365",
                 b"EST5EDT,M3.2.0/-167,M11.1.0/167",
                 b"<-04>4<-03>,M9.1.6/24,M4.1.6/24",
                 b"BST5CDT,M1.1.0/0,M1.1.0/1",
                 b"BSTST5CDT1,M1.1.0/0,M1.1.1"):
        if rule not in seen:
            seen.add(rule)
            write(posix, rule)

    # The TZif file the fuzzer found: seventy-four bytes claiming nearly a
    # billion transitions.
    found = ROOT / "tests" / "data" / "tzif" / "oversized_counts.tzif"
    if found.is_file():
        write(tzif, bytes([0x00]) + found.read_bytes())
    return tzif, posix


def seed_duration():
    out = reset("duration")
    # The harness reads a date-time pair out of the first bytes and then tries
    # the tail as ISO 8601 duration text, so a seed is a header plus a text.
    header = bytes(range(24))
    texts = [b"PT0S", b"P1Y2M3DT4H5M6S", b"-P1D", b"P-1D", b"PT0.5S",
             b"P3W", b"P1Y2D", b"PT1H2S", b"PT0,5S", b"P1M-1D",
             b"P999999999999999999999999D", b"P1.5Y", b"P1YT"]
    for text in texts:
        write(out, header + text)
        write(out, text)
    return out


def seed_format():
    out = reset("format")
    # One options byte, then the pattern.
    patterns = [
        b"uuuu-MM-dd'T'HH:mm:ssXXX", b"yyyy", b"YYYY-'W'ww-e", b"EEEE",
        b"MMMM", b"GGGG", b"SSSSSSSSS", b"XXXXX", b"xxxxx", b"ZZZZZ",
        b"OOOO", b"VV", b"zzz", b"'unterminated", b"''", b"b",
        b"%Y-%m-%dT%H:%M:%S%z", b"%c", b"%U", b"%", b"%E", b"%Oy",
        b"HHHHHHHHHHHHHHHHHHHH",
        # Two the fuzzer found, kept as named seeds rather than as the opaque
        # hashes libFuzzer names its artifacts. The first is a pattern with an
        # embedded NUL, whose output therefore contains one: the harness had
        # been measuring with strlen, which stops early, and the library was
        # right. The second is a signed year padded past nine digits, where
        # the declared buffer bound omitted the sign and was one byte short.
        b"PX\x00OOOOXXX",
        b"uuuuuuuuuuuuuuu",
        b"YYYYYYYYYYYY",
    ]
    for options in (0x00, 0x04, 0x07):
        for pattern in patterns:
            write(out, bytes([options]) + pattern)
    return out



def seed_leap():
    out = reset("leap")
    # The real file's shape, then the ways it can be wrong. A seed corpus of
    # only valid input never tests rejection - the gate would report zero on a
    # question it never asked.
    real = (b"#$\t3992312697\n"
            b"#@\t4023129600\n"
            b"2272060800\t10\t# 1 Jan 1972\n"
            b"2287785600\t11\t# 1 Jul 1972\n"
            b"3692217600\t37\t# 1 Jan 2017\n")
    cases = [
        real,
        b"",                                    # nothing at all
        b"# only prose\n",                      # no entries
        b"#@\t4023129600\n",                   # an expiry and nothing else
        b"2272060800\t10\n",                   # entries and no expiry
        b"#@\t4023129600\n2287785600\t11\n2272060800\t10\n",  # out of order
        b"#@\t4023129600\n2272060800\t10\n2272060800\t11\n",  # duplicate instant
        b"#@\t4023129600\n2272060800\n",      # a row with no offset
        b"#@\t4023129600\n2272060800\tx\n",  # a non-numeric offset
        b"#@\t4023129600\n2272060800\t10\tjunk\n",  # trailing junk
        b"#@\tx\n2272060800\t10\n",          # a non-numeric expiry
        b"#@\t" + b"9" * 40 + b"\n2272060800\t10\n",  # an expiry that overflows
        b"#@\t4023129600\n" + b"9" * 40 + b"\t10\n",  # an instant that overflows
        b"#@\t4023129600\n2272060800\t" + b"9" * 20 + b"\n",  # an offset that overflows
        # A negative leap second: never issued, expressible, and the reader
        # must not assume it cannot happen (design.md 5.1 item 5).
        b"#@\t4023129600\n2272060800\t10\n2287785600\t11\n2303683200\t10\n",
        real.replace(b"\n", b"\r\n"),          # CRLF, as an HTTP fetch gives
        real + b"#h\ta9bad145 84c31c70\n",     # the hash line
        real + b"#unknown directive\n",        # a directive from the future
        # The three fuzz_leap found, kept as named seeds rather than as the
        # opaque hashes libFuzzer names its artifacts.
        # A digit mutated into a letter, which read_u64 used to stop at -
        # leaving an expiry a hundred times too small and no complaint.
        b"#@\t40231296p0\n2272060800\t10\n",
        # An offset that jumps further than the gap between two rows, which
        # makes the TAI timeline run backwards across the boundary.
        b"#@\t4023129600\n2272060800\t10\n2287785600\t800010\n",
        # A negative leap one second after the row before it: the second it
        # removes used to convert to the same TAI value as its successor.
        b"#@\t40601\n22\t11\n23\t10\n",
    ]
    for case in cases:
        write(out, case)
    return out

def seed_scan():
    out = reset("scan")
    # One options byte, then `<pattern> NUL <text>`. Both halves are untrusted
    # in the real thing - the pattern comes out of a `ctang` template and the
    # text out of a log file - so the seeds pair each pattern with text that
    # fits it, text that nearly fits it, and text that does not fit at all.
    # A corpus of only matching pairs would never reach the rejection paths.
    pairs = [
        (b"uuuu-MM-dd'T'HH:mm:ssXXX", b"2026-09-21T15:30:45Z"),
        (b"uuuu-MM-dd'T'HH:mm:ssXXX", b"2026-09-21T15:30:45+05:45"),
        (b"uuuu-MM-dd'T'HH:mm:ssXXX", b"2026-09-21T15:30:45-00:00"),
        (b"uuuuMMddHHmmss", b"20260921153045"),
        (b"uuuu-MM-dd", b"2026-09-21"),
        (b"uuuu-MM-dd", b"2026-9-21"),        # a width strict mode refuses
        (b"uuuu-MM-dd", b"2026-09-21xyz"),    # trailing junk
        (b"uuuu-MM-dd", b"2026-09-"),         # truncated
        (b"uuuu-M-d", b"2026-9-8"),
        (b"yy-MM-dd", b"26-09-21"),           # needs a clock
        (b"YYYY-'W'ww-e", b"2026-W39-1"),
        (b"EEE, dd MMM uuuu HH:mm:ss Z", b"Mon, 21 Sep 2026 15:30:45 +0000"),
        (b"EEE uuuu-MM-dd", b"Tue 2026-09-21"),   # a weekday that disagrees
        (b"hh:mm a", b"12:00 AM"),
        (b"hh:mm a", b"12:00 PM"),
        (b"hh:mm", b"03:30"),                 # twelve-hour, no meridiem
        (b"KK:mm a", b"00:30 PM"),
        (b"kk:mm", b"24:00"),
        (b"HH:mm:ss.SSSSSSSSS", b"15:30:45.123456789"),
        (b"HH:mm:ss.SSS", b"15:30:45.1"),     # too few fractional digits
        (b"VV", b"America/Argentina/Buenos_Aires"),
        (b"VV", b"../../etc/passwd"),
        (b"VV", b"A" * 200),                  # past GCHRON_ZONE_ID_MAX
        (b"z", b"EST"),                       # refused: cannot be inverted
        (b"OOOO", b"GMT+05:45"),
        (b"g", b"61304"),
        (b"uuuu-DDD", b"2026-264"),
        (b"uuuu-DDD", b"2026-367"),           # no such day of the year
        (b"QQQ uuuu", b"Q3 2026"),
        (b"GGGG uuuu-MM-dd", b"BCE 0044-03-15"),
        (b"%Y-%m-%d %H:%M:%S", b"2026-09-21 15:30:45"),
        (b"%d/%b/%Y:%H:%M:%S", b"21/Sep/2026:15:30:45"),
        (b"%e %k", b" 1  9"),                 # space padding, both sides
        (b"%s", b"1789000000"),
        (b"%C%y", b"2026"),
        (b"", b""),
        (b"uuuu", b""),
        (b"", b"2026"),
        # The six fuzz_scan found, kept as named seeds rather than as the
        # opaque hashes libFuzzer gives its artifacts.
        #   1. The emitter pads the letter count as a *minimum*, so `uuu`
        #      writes four digits; a reader demanding exactly three could not
        #      read its own output. TR35's adjacency rule fixed it.
        (b"uuu-MM-dd", b"2026-09-21"),
        #   2. Two adjacent variable-width numbers, where the count is the
        #      only boundary there is.
        (b"uuAg", b"20265584512361304"),
        #   3. A twelve-hour field that read `31` because nothing checked the
        #      range where the digits were taken.
        (b"uuuu-MM-d.'T'hH:mm", b"2026-09-21.T315:30"),
        #   4. The same field twice, where the second silently overwrote the
        #      first instead of contradicting it.
        (b"DHDu", b"264152642026"),
        #   5. A fraction past the nine digits a nanosecond field holds,
        #      which overflowed the field it was about to be stored in.
        (b"HH:mm:ss.SSSSSSSSSS", b"15:30:45.1234567890"),
        #   6. Nineteen digits, the edge of an int64, reached from text.
        (b"%s", b"99999999999999999999"),
        #   ...and the two that are ambiguous rather than wrong, kept so that
        #   a change in how they are handled is visible.
        (b"g6", b"613046"),
        (b"kug", b"15202661304"),
    ]
    for options in (0x00, 0x02, 0x03):
        for pattern, text in pairs:
            write(out, bytes([options]) + pattern + b"\x00" + text)
    return out



def seed_textfmt():
    """The four text grammars, one selector byte then the text.

    These need real examples more than any other harness here. A coverage
    fuzzer will find `1994` and it will find `GMT`, but the chance of it
    assembling `Sun, 06 Nov 1994 08:49:37 GMT` out of nothing is nil, and
    every round-trip assertion in the harness sits *behind* a successful
    parse. Unseeded, the harness ran three million executions without once
    reaching the property it exists to check - a planted defect in the
    HTTP-date writer survived it. Seeded, the same defect dies in seconds.
    """
    out = reset("textfmt")

    rfc9557 = [
        b"2022-07-08T00:14:07Z[Europe/London]",
        b"2022-07-08T00:14:07+01:00[Europe/London]",
        b"2022-07-08T00:14:07+01:00[!Europe/London]",
        b"2026-01-01T00:00:00Z[UTC][u-ca=iso8601]",
        b"2026-03-08T02:30:00-05:00[America/New_York]",
        b"1970-01-01T00:00:00Z[Etc/GMT+12]",
        # Historical dates, where every named zone is on local mean time and
        # the offset is not a whole number of minutes. This shape is what
        # found the writer truncating a sub-minute offset and moving the
        # instant by fifteen seconds; London was -00:01:15 before 1847 and
        # Amsterdam +00:19:32 until 1937.
        b"0222-07-08T00:14:07Z[Europe/London]",
        b"1222-07-08T00:14:07Z[Europe/London]",
        b"1900-01-01T00:00:00Z[Europe/Amsterdam]",
        b"1800-06-15T12:00:00Z[Europe/Paris]",
    ]
    http = [
        b"Sun, 06 Nov 1994 08:49:37 GMT",       # IMF-fixdate
        b"Sunday, 06-Nov-94 08:49:37 GMT",      # RFC 850, two-digit year
        b"Sun Nov  6 08:49:37 1994",            # asctime
        b"Mon, 29 Feb 2016 12:00:00 GMT",       # a leap day
        b"Tue, 31 Dec 2049 23:59:59 GMT",       # the RFC 850 pivot's far side
    ]
    rfc5322 = [
        b"Fri, 21 Nov 1997 09:55:06 -0600",
        b"Tue, 1 Jul 2003 10:52:37 +0200",
        b"Thu, 13 Feb 1969 23:32:54 -0330",
        b"Mon, 24 Nov 1997 14:22:01 -0000",     # the unknown-offset spelling
        b"21 Nov 97 09:55:06 GMT",              # obsolete: no weekday, 2-digit
        b"Fri, 21 Nov 1997 09:55:06 EST",       # obsolete zone name
    ]
    yaml = [
        b"2001-12-14t21:59:43.10-05:00",
        b"2001-12-14 21:59:43.10 -5",
        b"2002-12-14",
        b"2001-12-15T02:59:43.1Z",
        b"2001-12-14 21:59:43.10",              # no zone: a local timestamp
    ]

    for selector, texts in enumerate((rfc9557, http, rfc5322, yaml)):
        for text in texts:
            write(out, bytes([selector]) + text)
    return out


def seed_zonedir():
    """`tzdata.zi` link tables: one options byte, then the file.

    Both spellings of a link line are seeded on purpose. `zishrink.awk` emits
    only the abbreviated `L`, so a corpus drawn from the machine's own file
    would never contain `Link` - and the two passes over this file disagreeing
    about which of them counts as a link was a real defect (see
    src/zone/zonedb.c). A corpus that only holds what the normal generator
    produces cannot ask about the input the normal generator never makes.
    """
    out = reset("zonedir")

    bodies = [
        b"",                                        # empty: nothing to link
        b"L RealZone AliasZone\n",                  # the abbreviated form
        b"Link RealZone AliasZone\n",               # the long form
        b"Link RealZone LongFormAlias\nL RealZone AliasZone\n",
        b"l RealZone AliasZone\n",                  # lowercase
        b"L\tRealZone\tAliasZone\n",                # tab-separated
        b"L RealZone AliasZone",                    # no trailing newline
        b"L RealZone AliasZone\r\n",                # CRLF
        b"L  RealZone   AliasZone  \n",             # runs of whitespace
        b"L RealZone\n",                            # a name and no target
        b"L\n",                                     # neither
        b"# version 2026c\nL RealZone AliasZone\n",  # a version header
        b"Z RealZone 0 - UTC\nL RealZone AliasZone\n",
        b"L AliasZone AliasZone\n",                 # a link to itself
        b"L Missing AliasZone\n",                   # target that is absent
        b"L RealZone A\n" * 64,                     # many links, one target
    ]

    # A real one, if this machine has it: the shape the parser meets in life.
    system = pathlib.Path("/usr/share/zoneinfo/tzdata.zi")
    if system.is_file():
        links = [line for line in system.read_bytes().splitlines()
                 if line[:2] in (b"L ", b"L\t")]
        if links:
            bodies.append(b"\n".join(links[:200]) + b"\n")

    for options in (0x00, 0x01, 0x02):
        for body in bodies:
            write(out, bytes([options]) + body)
    return out


def main():
    prune = "--prune" in sys.argv[1:]
    unknown = [a for a in sys.argv[1:] if a != "--prune"]
    if unknown:
        sys.stderr.write("seed.py: unrecognised argument %s\n" % unknown[0])
        sys.stderr.write("usage: seed.py [--prune]\n")
        return 2

    parse = seed_parse()
    arith = seed_arith()
    tzif, posix = seed_zones()
    duration = seed_duration()
    fmt = seed_format()
    leap = seed_leap()
    scan = seed_scan()
    textfmt = seed_textfmt()
    zonedir = seed_zonedir()
    total_kept = 0
    for path in (parse, arith, tzif, posix, duration, fmt, leap, scan,
                 textfmt, zonedir):
        written, kept = sweep(path, prune)
        total_kept += kept
        note = ""
        if kept:
            note = ", %d kept" % kept
        print("%-34s %5d written%s" % (path.relative_to(ROOT), written, note))

    if prune:
        print("\n--prune: anything this run did not write has been deleted.")
        print("Check `git status` before staging: on a machine with less of")
        print("tzdata installed than the committed corpus was built from,")
        print("those deletions are seeds, not campaign units.")
    elif total_kept:
        print("\n%d files were already there and this run did not write"
              % total_kept)
        print("them - campaign units, or seeds from a machine with more of")
        print("tzdata installed. Kept. Use --prune to delete them.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
