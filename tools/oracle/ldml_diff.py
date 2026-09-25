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
import os
import subprocess
import textwrap
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

# Divergences that a letter cannot name, because they depend on the value
# rather than on the pattern.
#
# Keyed by pattern, `O` here would stop comparing the localised GMT format
# altogether - 45 rows at non-zero offsets that agree today and would stop
# being asked. An exclusion wide enough to absorb a future defect is worse
# than the divergence it was written for, so these match the answers.
#
# Each entry is (name, predicate, reason). The predicate is given the pattern,
# the zone, the instant and both answers.
CONDITIONAL_DIVERGENCES = [
    (
        "localised GMT at a zero offset",
        lambda pattern, zone, millis, ours, theirs: (
            ours == "GMT" and theirs in ("GMT+0", "GMT+00:00")),
        "chron writes CLDR's `gmtZeroFormat`, which root spells `GMT` and "
        "which TR35's own table of fallback elements still describes as how "
        "\"GMT/UTC with an offset of zero should be represented\". ICU 78.3 "
        "writes an explicit `GMT+0`/`GMT+00:00` instead, while continuing to "
        "carry that element and return it from getGMTZeroFormat(). ICU 76.1 "
        "wrote `GMT` and agreed. The change is CLDR's, not a regression: TR35 "
        "revision 76 (CLDR 48) added `\"GMT+00:00\" (long)` and `\"UTC+0\" "
        "(short)` to the localized-GMT examples, where revision 75 and every "
        "revision before it had none - so the specification now says both "
        "things and ICU picked the newer one. Whether this library follows it "
        "is a decision about output users read, not a defect: see "
        "notes/chron/ORACLES-OPEN.md.",
    ),
]

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


def matched(query, ours, theirs):
    """The conditional divergence this row falls in, or None."""
    pattern, zone, millis = query.split("\t")
    for name, predicate, _ in CONDITIONAL_DIVERGENCES:
        if predicate(pattern, zone, millis, ours, theirs):
            return name
    return None


def run(argv, queries):
    stdin = "\n".join(queries) + "\n"
    result = subprocess.run(list(argv), input=stdin, capture_output=True,
                            text=True)
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
    parser.add_argument("--max-report", type=int, default=30)
    args = parser.parse_args()

    queries = ["%s\t%s\t%d" % (p, z, m)
               for p, z, m in itertools.product(PATTERNS, ZONES, MILLIS)]

    ours = run([args.chron], queries)
    theirs = run(icu_command(), queries)
    if len(ours) != len(queries) or len(theirs) != len(queries):
        sys.stderr.write("ldml_diff.py: %d queries, %d chron lines, %d icu "
                         "lines\n" % (len(queries), len(ours), len(theirs)))
        return 1

    agreed = 0
    known = {}
    conditional = {}
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
        elif matched(query, ours_out, theirs_out) is not None:
            #
            # Before the letter table, deliberately. Three of the `z` rows
            # diverge under ICU 78.3 for the zero-offset reason rather than
            # for the no-zone-names reason, and filing them under `z` would
            # make that letter's count move for a cause it does not name.
            # Attributing a row to what actually caused it is what keeps the
            # other counts comparable across a raise.
            #
            name = matched(query, ours_out, theirs_out)
            conditional.setdefault(name, []).append((query, ours_out,
                                                     theirs_out))
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
    #
    # A differential that compared nothing is a failure, not a pass. This is
    # not hypothetical: converting this gate to a containerised reference, the
    # driver was briefly handed its queries in the wrong mode, ICU refused all
    # 828 of them, and the tool printed a summary and exited 0. It was caught
    # only because the previous run's numbers were known - which is not a
    # property a fresh machine has.
    #
    if agreed == 0:
        sys.stderr.write(
            "ldml_diff: nothing was compared - %d queries, %d agreed. The\n"
            "reference answered but agreed with nothing, which means it was\n"
            "asked the wrong question rather than that the library is wrong.\n"
            % (len(queries), agreed))
        return 1

    for name, _, reason in CONDITIONAL_DIVERGENCES:
        cases = conditional.get(name)
        if not cases:
            print("\nKNOWN DIVERGENCE %r no longer diverges, against %s.\n"
                  "Re-triage it and remove it, or find out what changed."
                  % (name, oracle_env.version("icu")))
            return 1
        query, ours_out, theirs_out = cases[0]
        print("known divergence, %s: %d cases, e.g. %s -> chron %r vs icu %r"
              % (name, len(cases), query.replace("\t", " | "), ours_out,
                 theirs_out))
        for line in textwrap.wrap(reason, 72):
            print("    " + line)

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
