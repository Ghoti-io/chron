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

Running it is idempotent; it clears each directory first, so a corpus that has
grown during a campaign comes back to the seed set.

    tools/fuzz/seed.py

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


def reset(name):
    path = CORPUS / name
    path.mkdir(parents=True, exist_ok=True)
    for entry in path.iterdir():
        if entry.is_file() and entry.name != ".gitignore":
            entry.unlink()
    return path


def write(path, blob):
    (path / hashlib.sha1(blob).hexdigest()).write_bytes(blob)


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

def main():
    parse = seed_parse()
    arith = seed_arith()
    tzif, posix = seed_zones()
    duration = seed_duration()
    fmt = seed_format()
    leap = seed_leap()
    for path in (parse, arith, tzif, posix, duration, fmt, leap):
        count = len([p for p in path.iterdir() if p.is_file()
                     and p.name != ".gitignore"])
        print("%-34s %5d seeds" % (path.relative_to(ROOT), count))
    return 0


if __name__ == "__main__":
    sys.exit(main())
