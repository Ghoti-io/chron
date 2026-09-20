#!/usr/bin/env python3
"""Check every zone in the system database against Python's `zoneinfo`.

design.md section 12 lists this as the *independent* check on the zone
reader: Python reads the same TZif files through entirely different code, so
the two agreeing is evidence about the files rather than about one reader's
opinion of them.  `zdump` is the other oracle and is the tzdb's own
implementation; this one is a second opinion from a third party, and the two
together are what makes a disagreement interpretable.

Where `tools/oracle/zdump.py` commits a vector file for twenty zones, this
sweeps **all of them** - every zone the database holds, at a lattice of
instants - and is therefore run on demand rather than committed:

    make check-oracle-zoneinfo

It drives `tools/oracle/gchron_zone`, which puts the library behind a line
protocol.  A disagreement prints both answers and the input that produced it.

Named `zoneinfo_diff.py` and not `zoneinfo.py`, which is what design.md
section 12's table calls it: a script named after a standard-library module
shadows it for its own interpreter, so `import zoneinfo` inside this file
would find this file.  The oracle would then be checking the library against
nothing at all - and would have said so only through an AttributeError.

Usage:  tools/oracle/zoneinfo_diff.py --driver <path> [--step SECONDS]

Copyright 2026 by Corey Pennycuff
"""

import argparse
import calendar
import datetime
import os
import pathlib
import subprocess
import sys
import zoneinfo

# Instants chosen to land either side of every kind of changeover: a few
# fixed dates that have caught real defects, plus a regular lattice.
FIXED = [
    -2208988800,  # 1900-01-01, before most zones' first transition
    -1,           # the second before the epoch, where truncating division lies
    0,            # the epoch
    1541300400,   # Sao Paulo's missing midnight
    1325282400,   # Samoa's missing day
    1793511000,   # New York's repeated hour, first pass
    1793514600,   # and second
    2145916800,   # 2038-01-01, past every "fat" file's table
    4102444800,   # 2100-01-01, deep in POSIX-footer territory
]


def lattice(step):
    start = calendar.timegm((2024, 1, 1, 0, 0, 0, 0, 0, 0))
    end = calendar.timegm((2028, 1, 1, 0, 0, 0, 0, 0, 0))
    return list(range(start, end, step))


def expected(zone, seconds):
    try:
        tz = zoneinfo.ZoneInfo(zone)
    except Exception:
        return None
    moment = datetime.datetime.fromtimestamp(seconds, tz)
    offset = moment.utcoffset()
    dst = moment.dst()
    if offset is None:
        return None
    return "%d\t%d\t%s\t%s" % (
        int(offset.total_seconds()),
        1 if (dst is not None and dst.total_seconds() != 0) else 0,
        moment.tzname(),
        moment.strftime("%Y-%m-%dT%H:%M:%S"),
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True)
    parser.add_argument("--step", type=int, default=7 * 86400,
                        help="seconds between lattice probes")
    parser.add_argument("--max-report", type=int, default=25)
    args = parser.parse_args()

    tzdir = os.environ.get("TZDIR", "/usr/share/zoneinfo")
    try:
        zones = sorted(zoneinfo.available_timezones())
    except Exception as error:
        sys.stderr.write("zoneinfo.py: %s\n" % error)
        return 1
    if not zones:
        sys.stderr.write("zoneinfo.py: no zones available; nothing checked\n")
        return 1

    instants = FIXED + lattice(args.step)
    queries = []
    for zone in zones:
        for seconds in instants:
            queries.append("%s\t%d" % (zone, seconds))

    stdin = "\n".join(queries) + "\n"
    run = subprocess.run([args.driver], input=stdin, capture_output=True,
                         text=True)
    if run.returncode != 0:
        sys.stderr.write(run.stderr)
        return run.returncode

    lines = run.stdout.splitlines()
    if len(lines) != len(queries):
        sys.stderr.write("zoneinfo.py: driver answered %d of %d queries\n"
                         % (len(lines), len(queries)))
        return 1

    checked = 0
    skipped = 0
    mismatches = []
    for query, line in zip(queries, lines):
        zone, seconds = query.split("\t")
        want = expected(zone, int(seconds))
        if want is None:
            skipped += 1
            continue
        got = line.split("\t", 2)[2] if line.count("\t") >= 2 else ""
        if got != want:
            if len(mismatches) < args.max_report:
                mismatches.append("  %s at %s\n    chron:  %s\n    python: %s"
                                  % (zone, seconds, got, want))
            continue
        checked += 1

    print("tzdir %s: %d zones, %d probes each" % (tzdir, len(zones),
                                                  len(instants)))
    print("%d agreed, %d could not be asked, %d disagreed"
          % (checked, skipped, len(queries) - checked - skipped))
    if mismatches:
        print("\nfirst disagreements:")
        print("\n".join(mismatches))
        return 1
    # Silence is not success: a run that asked nothing must not look like a
    # run that agreed about everything.
    if checked == 0:
        sys.stderr.write("zoneinfo.py: nothing was checked\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
