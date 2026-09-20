#!/usr/bin/env python3
"""Compare this library's LDML formatter against ICU's.

design.md section 8.3: a pattern's meaning is *defined* as what
``icu::SimpleDateFormat`` does with it in the root locale.  So this is not a
sanity check on two implementations that might both be wrong - one of them is
the definition.

Both sides speak the same line protocol, so a disagreement names the pattern,
the zone and the instant that produced it.

    make check-oracle-ldml

Usage:  tools/oracle/ldml_diff.py --chron <path> --icu <path>

Copyright 2026 by Corey Pennycuff
"""

import argparse
import itertools
import subprocess
import sys

# The letters and counts worth checking, one pattern each, plus a handful of
# realistic compound patterns. Every letter this library implements appears,
# and every count at which its behaviour changes.
PATTERNS = [
    "G", "GG", "GGG", "GGGG", "GGGGG",
    "y", "yy", "yyy", "yyyy", "yyyyy",
    "u", "uu", "uuuu",
    "Y", "YY", "YYYY",
    "Q", "QQ",
    "M", "MM", "MMM", "MMMM", "MMMMM",
    "L", "LL", "LLL", "LLLL",
    "w", "ww", "W",
    "d", "dd", "D", "DD", "DDD", "F",
    "E", "EE", "EEE", "EEEE", "EEEEE", "EEEEEE",
    "a",
    "h", "hh", "H", "HH", "K", "KK", "k", "kk",
    "m", "mm", "s", "ss",
    "S", "SS", "SSS", "SSSSSS", "SSSSSSSSS",
    "A",
    "X", "XX", "XXX", "XXXX", "XXXXX",
    "x", "xx", "xxx", "xxxx", "xxxxx",
    "Z", "ZZ", "ZZZ", "ZZZZZ",
    "O", "OOOO",
    "VV",
    "z", "zz", "zzz",
    # Compound patterns of the shape somebody actually writes.
    "uuuu-MM-dd'T'HH:mm:ssXXX",
    "uuuu-MM-dd HH:mm:ss.SSS",
    "EEE, dd MMM uuuu HH:mm:ss Z",
    "YYYY-'W'ww-e",
    "uuuu-DDD",
    "dd/MM/yy hh:mm a",
    "'at' HH:mm 'on' EEEE",
    "'it''s' HH:mm",
]

# Letters where this library deliberately says something ICU's *root locale*
# does not, with the reason. They are listed rather than dropped, so that a
# divergence which quietly disappears - or a new one which quietly appears -
# is visible either way.
KNOWN_DIVERGENCES = {
    "z": "chron prints the tzdb's own abbreviation (EST, EDT); CLDR root has "
         "no zone names and falls back to localised GMT. The abbreviation is "
         "what a log line wants and what strftime %Z gives.",
    "zz": "as z.",
    "zzz": "as z.",
}

ZONES = [
    "UTC",
    "America/New_York",
    "Europe/Paris",
    "Asia/Kathmandu",
    "Australia/Lord_Howe",
    "Pacific/Chatham",
    "Europe/Dublin",
]

# Instants spread across the year, either side of both daylight-saving
# changes, and one deep in the past.
MILLIS = [
    1781539200000,   # 2026-06-15T16:00:00Z, northern summer
    1768478400000,   # 2026-01-15T12:00:00Z, northern winter
    1772953800000,   # 2026-03-08T06:30:00Z, just after a US spring forward
    1793514600000,   # 2026-11-01T06:30:00Z, inside a US fall back
    -2208988800000,  # 1900-01-01T00:00:00Z
    1000000000123,   # a millisecond that is not zero
]


def run(path, queries):
    stdin = "\n".join(queries) + "\n"
    result = subprocess.run([path], input=stdin, capture_output=True,
                            text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit(result.returncode)
    return result.stdout.splitlines()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chron", required=True)
    parser.add_argument("--icu", required=True)
    parser.add_argument("--max-report", type=int, default=30)
    args = parser.parse_args()

    queries = ["%s\t%s\t%d" % (p, z, m)
               for p, z, m in itertools.product(PATTERNS, ZONES, MILLIS)]

    ours = run(args.chron, queries)
    theirs = run(args.icu, queries)
    if len(ours) != len(queries) or len(theirs) != len(queries):
        sys.stderr.write("ldml_diff.py: %d queries, %d chron lines, %d icu "
                         "lines\n" % (len(queries), len(ours), len(theirs)))
        return 1

    agreed = 0
    known = {}
    both_refused = 0
    only_we_refused = []
    only_they_refused = []
    disagreed = []

    for query, a, b in zip(queries, ours, theirs):
        ours_out = a.rsplit("\t", 1)[-1]
        theirs_out = b.rsplit("\t", 1)[-1]
        if ours_out == "ERR" and theirs_out == "ERR":
            both_refused += 1
        elif ours_out == "ERR":
            only_we_refused.append(query)
        elif theirs_out == "ERR":
            only_they_refused.append(query)
        elif ours_out == theirs_out:
            agreed += 1
        elif query.split("\t")[0] in KNOWN_DIVERGENCES:
            known.setdefault(query.split("\t")[0], []).append(
                (ours_out, theirs_out))
        else:
            disagreed.append("  %s\n    chron: %r\n    icu:   %r"
                             % (query.replace("\t", " | "), ours_out,
                                theirs_out))

    print("%d patterns x %d zones x %d instants = %d queries"
          % (len(PATTERNS), len(ZONES), len(MILLIS), len(queries)))
    print("%d agreed, %d both refused, %d only chron refused, "
          "%d only icu refused, %d disagreed"
          % (agreed, both_refused, len(only_we_refused),
             len(only_they_refused), len(disagreed)))

    for pattern, reason in sorted(KNOWN_DIVERGENCES.items()):
        cases = known.get(pattern)
        if not cases:
            print("\nKNOWN DIVERGENCE %r no longer diverges - remove it from "
                  "KNOWN_DIVERGENCES, or find out what changed." % pattern)
            return 1
        print("known divergence %r: %d cases, e.g. chron %r vs icu %r"
              % (pattern, len(cases), cases[0][0], cases[0][1]))

    if disagreed:
        print("\ndisagreements:")
        print("\n".join(disagreed[:args.max_report]))
    if only_we_refused:
        print("\nchron refused what ICU formatted (first few):")
        for query in only_we_refused[:args.max_report]:
            print("  " + query.replace("\t", " | "))

    # Silence is not success: a run that asked nothing must not look like a
    # run that agreed about everything.
    if agreed == 0:
        sys.stderr.write("ldml_diff.py: nothing agreed; did either side "
                         "run?\n")
        return 1
    return 1 if (disagreed or only_we_refused) else 0


if __name__ == "__main__":
    sys.exit(main())
