#!/usr/bin/env python3
"""Can this library read back the text ICU wrote?

design.md section 8.7. The formatting differential (``ldml_diff.py``) compares
two writers; this compares a *reader* against the only writer whose output is
the definition.

ICU formats an instant; then *both* sides are handed ICU's own bytes and
asked which instant they name.  Nothing this library wrote is involved, so a
disagreement is this library failing to read what the reference implementation
emits - which is the thing a log reader actually has to do, and a thing that
comparing chron against chron could never measure.

Comparing against ICU's reading rather than against the original instant is
what makes it fair.  A pattern carries only what it carries: ``HH:mm:ss`` has
nowhere to put a millisecond, and ``XXX`` truncates Europe/Paris's 1900 offset
of +00:09:21 to +00:09.  Both sides lose the same information from the same
bytes, so they still agree - whereas comparing against the instant that went
in would report 109 "failures" that are nothing but the pattern doing its
job.

Patterns that cannot name an instant on their own are not failures and are
counted separately: ``MMMM`` alone is a month, and no reader recovers a
timestamp from it.  ICU answers such a pattern by filling the gaps from a
default calendar - 1970, January, midnight - which section 8.7 refuses to do,
so the honest report is "incomplete", not "disagreed".

    make check-oracle-ldml-parse

Usage:  tools/oracle/ldml_parse_diff.py --chron <path> --icu <path>

Copyright 2026 by Corey Pennycuff
"""

import argparse
import itertools
import os
import subprocess
import sys

# Patterns that name a whole instant. Everything here should round-trip, and
# anything that does not is a finding.
COMPLETE = [
    "uuuu-MM-dd'T'HH:mm:ssXXX",
    "uuuu-MM-dd'T'HH:mm:ssXXXXX",
    "uuuu-MM-dd HH:mm:ss.SSSXXX",
    "uuuu-MM-dd HH:mm:ss.SSSSSSSSSXXX",
    "uuuuMMdd'T'HHmmssXX",
    "EEE, dd MMM uuuu HH:mm:ss Z",
    "EEEE d MMMM uuuu HH:mm:ss Z",
    "dd/MM/uuuu hh:mm:ss a XXX",
    "uuuu-DDD'T'HH:mm:ssXXX",
    "uuuu-MM-dd KK:mm:ss a XXX",
    "uuuu-MM-dd kk:mm:ssXXX",
    "GGGG uuuu-MM-dd HH:mm:ssXXX",
    "QQQ uuuu-MM-dd HH:mm:ssXXX",
    "uuuu-MM-dd'T'HH:mm:ss'['VV']'",
    "'at' HH:mm:ssXXX 'on' uuuu-MM-dd",
    "'it''s' uuuu-MM-dd HH:mm:ssXXX",
    #
    # The localised GMT format, in all three of its spellings. Absent until
    # 2026-09-25, and absent for a reason that had stopped being true: this
    # library refused to *read* `O`, `OOOO` and `ZZZZ` at all, so there was
    # nothing to compare. It refused them while carrying a complete reader for
    # them that no call site reached, and it wrote them the whole time - which
    # made this the one gate that could have said "you emit text you cannot
    # read" and had been excused from asking.
    #
    # Worth knowing what these add over the `X` and `Z` rows: ICU writes the
    # short form without a leading zero and drops zero minutes (`GMT-4`), keeps
    # them when they are not zero (`GMT+5:45`), and writes the seconds the tzdb
    # records for local mean time (`GMT+00:09:21` for Paris in 1900). Three
    # shapes one pattern, decided by the value, which is exactly the class of
    # reader bug a fixed-shape corpus does not reach.
    #
    "uuuu-MM-dd'T'HH:mm:ss O",
    "uuuu-MM-dd'T'HH:mm:ss OOOO",
    "uuuu-MM-dd'T'HH:mm:ss ZZZZ",
]

# Patterns that name only part of one. Reported, never failed: this library
# says so rather than inventing the rest, which is the whole of section 8.7.
PARTIAL = [
    "uuuu", "MMMM", "EEEE", "HH:mm", "uuuu-MM-dd", "QQQ", "VV",
]

ZONES = [
    "UTC",
    "America/New_York",
    "Europe/Paris",
    "Asia/Kathmandu",       # +05:45, a quarter-hour offset
    "Australia/Lord_Howe",  # +10:30/+11, a half-hour DST step
    "Pacific/Chatham",      # +12:45/+13:45
]

MILLIS = [
    1781539200000,   # 2026-06-15T16:00:00Z, northern summer
    1768478400000,   # 2026-01-15T12:00:00Z, northern winter
    1772953800000,   # 2026-03-08T06:30:00Z, just after a US spring forward
    1793514600000,   # 2026-11-01T06:30:00Z, inside a US fall back
    -2208988800000,  # 1900-01-01T00:00:00Z
    1000000000123,   # a millisecond that is not zero
]


def run(argv, lines):
    result = subprocess.run(list(argv), input="\n".join(lines) + "\n",
                            capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit(result.returncode)
    return result.stdout.splitlines()




sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_env


def icu_command(mode=None):
    """The reference, compiled inside its image against that image's ICU.

    Compiled at run time rather than shipped in the image, because the source
    lives here and the image is deliberately ignorant of this library: what it
    contains is an ICU and a compiler, and the driver it builds links only
    ICU. That is the property that keeps the reference from being able to
    reach the implementation it answers for.

    In host mode the same `sh -c` runs on the host and compiles against
    whatever libicu is installed, which is the old behaviour and now says so
    in the line the gate prints.
    """
    inner = ("g++ -O1 -o /tmp/icu_format"
             " %s/tools/oracle/icu_format.cpp -licui18n -licuuc"
             " && exec /tmp/icu_format" % oracle_env.ROOT)
    if mode:
        inner += " " + mode
    return oracle_env.command("icu", ["sh", "-c", inner])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chron", required=True)
    parser.add_argument("--max-report", type=int, default=25)
    args = parser.parse_args()

    patterns = COMPLETE + PARTIAL
    queries = ["%s\t%s\t%d" % (p, z, m)
               for p, z, m in itertools.product(patterns, ZONES, MILLIS)]

    # ICU writes the text; both sides are then handed those exact bytes.
    written = run(icu_command(), queries)
    if len(written) != len(queries):
        sys.stderr.write("ldml_parse_diff.py: %d queries, %d icu lines\n"
                         % (len(queries), len(written)))
        return 1
    recovered = run([args.chron], written)
    theirs = run(icu_command("parse"), written)
    if len(recovered) != len(written) or len(theirs) != len(written):
        sys.stderr.write("ldml_parse_diff.py: %d icu lines, %d chron, %d icu "
                         "parse\n" % (len(written), len(recovered), len(theirs)))
        return 1

    agreed = 0
    incomplete = 0
    icu_refused = 0
    failures = []

    for query, icu_line, ours, icu_back in zip(queries, written, recovered,
                                               theirs):
        pattern = query.split("\t", 1)[0]
        text = icu_line.split("\t", 3)[3] if icu_line.count("\t") >= 3 else "ERR"
        got = ours.rsplit("\t", 1)[-1]
        want = icu_back.rsplit("\t", 1)[-1]

        if text == "ERR" or want == "ERR":
            icu_refused += 1
            continue
        if pattern in PARTIAL:
            # Expected not to name an instant on its own. ICU answers anyway,
            # filling the gaps from a default calendar; that this library
            # refuses instead is the point of section 8.7, so a refusal here
            # is the pass and an answer would be the finding.
            if got == "ERR":
                incomplete += 1
            else:
                failures.append((pattern, text, got, want))
            continue
        if got == "ERR":
            failures.append((pattern, text, "refused", want))
            continue
        if int(got) == int(want):
            agreed += 1
        else:
            failures.append((pattern, text, got, want))

    total = agreed + len(failures)
    print("ldml_parse_diff: %d of %d readings agreed with ICU's own"
          % (agreed, total))
    #
    # Zero comparisons is a failure. See the note in ldml_diff.py: this gate
    # printed "0 of 0 readings agreed" and exited 0 while the reference was
    # refusing every pattern it was handed.
    #
    if total == 0:
        sys.stderr.write(
            "ldml_parse_diff: nothing was compared. The reference answered "
            "but read none of the %d patterns, which points at the question "
            "rather than at the library.\n" % len(queries))
        return 1
    print("  %d partial patterns refused, as section 8.7 says they must"
          % incomplete)
    if icu_refused:
        print("  %d ICU itself refused" % icu_refused)

    if failures:
        print("\n%d disagreed:" % len(failures))
        for pattern, text, got, want in failures[:args.max_report]:
            print("  %-34s %-32s chron %s  icu %s"
                  % (pattern, text, got, want))
        if len(failures) > args.max_report:
            print("  ... and %d more" % (len(failures) - args.max_report))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
