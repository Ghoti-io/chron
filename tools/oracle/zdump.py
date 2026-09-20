#!/usr/bin/env python3
"""Generate zone transition vectors from `zdump`.

design.md section 12: the oracle is the authority, and every vector is
generated from one.  `zdump -v` prints, for every transition in a zone, the
UTC instant and the civil reading, offset, abbreviation and daylight-saving
flag on *both sides* of it - which is precisely what
``gchron_zone_offset_at`` and ``gchron_zone_next_transition`` are asked, and
is produced by the reference implementation of the tzdb itself.  Nothing here
is written from anybody's memory of what a zone does.

The output is one row per side of one transition:

    zone <TAB> unix_seconds <TAB> utoff <TAB> isdst <TAB> abbr <TAB> local_civil

`local_civil` is `YYYY-MM-DDThh:mm:ss`, so a vector checks the civil
conversion as well as the offset: a reader that got the offset right and the
arithmetic wrong would otherwise pass.

Rows outside 1800-2100 are dropped.  `zdump` brackets its output with the ends
of representable time, whose years run to ten digits and mean nothing to
anybody; every transition any zone has ever had is inside the window.

Usage:  tools/oracle/zdump.py [--out FILE] [zone...]

Copyright 2026 by Corey Pennycuff
"""

import argparse
import calendar
import datetime
import os
import pathlib
import re
import shutil
import subprocess
import sys

# The shapes that break a reader, rather than every zone in the database.
# Several hundred thousand assertions would make `make test` a minute longer
# and would not find anything this set misses: a negative daylight-saving
# offset (Dublin), a southern-hemisphere rule that wraps the year (Sydney,
# Santiago), a half-hour (Kathmandu, St John's) and three-quarter-hour
# (Chatham) offset, a zone that skipped a whole day (Apia), one that abolished
# daylight saving (Moscow), local mean time to the second (Amsterdam), a
# thirty-minute daylight-saving shift (Lord Howe), and the ones the rest of
# the tests use.  tools/oracle/zoneinfo.py sweeps the whole database
# separately and needs no vector file.
DEFAULT_ZONES = [
    "America/New_York",
    "America/Santiago",
    "America/Sao_Paulo",
    "America/St_Johns",
    "Antarctica/Troll",
    "Asia/Kathmandu",
    "Asia/Tehran",
    "Atlantic/Azores",
    "Australia/Lord_Howe",
    "Australia/Sydney",
    "Europe/Amsterdam",
    "Europe/Dublin",
    "Europe/Lisbon",
    "Europe/London",
    "Europe/Moscow",
    "Europe/Paris",
    "Pacific/Apia",
    "Pacific/Chatham",
    "Pacific/Kiritimati",
    "UTC",
]

YEAR_MIN = 1800
YEAR_MAX = 2100

LINE = re.compile(
    r"^(?P<zone>\S+)\s+(?P<utc>.+?)\s+UT\s+=\s+(?P<local>.+?)\s+"
    r"(?P<abbr>\S+)\s+isdst=(?P<isdst>\d+)\s+gmtoff=(?P<gmtoff>-?\d+)\s*$"
)

MONTHS = {name: number for number, name in enumerate(
    ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"], start=1)}


def parse_civil(text):
    """Parse `Sun Nov 18 16:59:59 1883` into its six fields.

    Hand-written rather than `strptime`, because `zdump`'s bracketing rows
    carry ten-digit years that `strptime` refuses and because the day of the
    month is space-padded, which `%d` does not accept on every platform.
    """
    parts = text.split()
    if len(parts) != 5:
        return None
    _weekday, month_name, day, clock, year = parts
    if month_name not in MONTHS:
        return None
    hh, mm, ss = clock.split(":")
    try:
        return (int(year), MONTHS[month_name], int(day),
                int(hh), int(mm), int(ss))
    except ValueError:
        return None


def rows_for(zone, tzdir):
    env = dict(os.environ)
    env["TZ"] = zone
    if tzdir:
        env["TZDIR"] = tzdir
    try:
        output = subprocess.run(["zdump", "-v", zone], env=env,
                                capture_output=True, text=True,
                                check=False).stdout
    except OSError:
        return
    for line in output.splitlines():
        match = LINE.match(line)
        if not match:
            continue
        utc = parse_civil(match.group("utc"))
        local = parse_civil(match.group("local"))
        if utc is None or local is None:
            continue
        if not (YEAR_MIN <= utc[0] <= YEAR_MAX):
            continue
        seconds = calendar.timegm(
            (utc[0], utc[1], utc[2], utc[3], utc[4], utc[5], 0, 0, 0))
        yield "%s\t%d\t%s\t%s\t%s\t%04d-%02d-%02dT%02d:%02d:%02d" % (
            zone, seconds, match.group("gmtoff"), match.group("isdst"),
            match.group("abbr"), local[0], local[1], local[2],
            local[3], local[4], local[5])


def tzdata_version(tzdir):
    for name, prefix in (("tzdata.zi", "# version "), ("+VERSION", "")):
        path = pathlib.Path(tzdir) / name
        try:
            first = path.read_text().splitlines()[0]
        except (OSError, IndexError):
            continue
        if prefix and not first.startswith(prefix):
            continue
        value = first[len(prefix):].strip()
        if value:
            return value
    return "unknown"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="tests/data/vectors/zones/transitions.vec")
    parser.add_argument("zones", nargs="*", default=None)
    args = parser.parse_args()

    if shutil.which("zdump") is None:
        sys.stderr.write("zdump.py: zdump is not installed; cannot "
                         "regenerate\n")
        return 1

    tzdir = os.environ.get("TZDIR", "/usr/share/zoneinfo")
    version = tzdata_version(tzdir)
    zones = args.zones if args.zones else DEFAULT_ZONES

    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with out.open("w") as handle:
        handle.write(
            "# zone\tunix_seconds\tutoff\tisdst\tabbreviation\tlocal_civil\n"
            "# Generated by tools/oracle/zdump.py from zdump, tzdata %s.\n"
            "# Each transition contributes two rows: the last second before\n"
            "# it and the first second of it. Years %d-%d only.\n"
            % (version, YEAR_MIN, YEAR_MAX))
        for zone in zones:
            for row in rows_for(zone, tzdir):
                handle.write(row + "\n")
                count += 1
    print("%s: %d rows from %d zones (tzdata %s)"
          % (out, count, len(zones), version))
    return 0


if __name__ == "__main__":
    sys.exit(main())
