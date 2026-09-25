#!/usr/bin/env python3
"""Answer zone questions from Python's `zoneinfo`, one per line.

    <zone>\t<unix seconds>   ->   <offset>\t<dst>\t<abbrev>\t<local ISO>

or a single `-` when this interpreter cannot construct that zone, which the
caller counts rather than ignores.

**This exists so that the reference is a process rather than an import.**
`zoneinfo_diff.py` used to call `zoneinfo.ZoneInfo` in its own interpreter, so
"the reference" was whichever python3 happened to run the tool - the one pin
that cannot be written down. Now the differential runs on the host and this
runs in the pinned image, and the version that answered is printed above the
numbers.

**It reads the zone files it is given, not the ones its image ships with.**
`zoneinfo_diff.py` mounts the host's zone directory and points PYTHONTZPATH at
it, because the comparison is between two *readers* of one set of TZif files.
The image carries tzdata of its own and the digest freezes it, where the
host's moves with the distribution: left alone, the two would drift apart and
the differential would start reporting the gap between two tzdb releases as
though chron had regressed.

One process for the whole batch, because a container start is about 200ms -
fine once per gate, ruinous once per query.
"""

import datetime
import sys
import zoneinfo


def answer(zone, seconds):
    try:
        tz = zoneinfo.ZoneInfo(zone)
    except Exception:
        return "-"
    moment = datetime.datetime.fromtimestamp(seconds, tz)
    offset = moment.utcoffset()
    dst = moment.dst()
    if offset is None:
        return "-"
    return "%d\t%d\t%s\t%s" % (
        int(offset.total_seconds()),
        1 if (dst is not None and dst.total_seconds() != 0) else 0,
        moment.tzname(),
        moment.strftime("%Y-%m-%dT%H:%M:%S"),
    )


def main():
    #
    # `--list` reports what this interpreter can construct, which is the other
    # half of the population and has to come from the *reference* rather than
    # from the host: "zones the database holds that the oracle cannot
    # construct" is a statement about the oracle, and asking the host's python3
    # instead would answer it about a different interpreter reading a different
    # tzdata.
    #
    if "--list" in sys.argv[1:]:
        for name in sorted(zoneinfo.available_timezones()):
            sys.stdout.write(name + "\n")
        return 0

    out = []
    for line in sys.stdin:
        line = line.rstrip("\n")
        if not line:
            continue
        parts = line.split("\t")
        if len(parts) != 2:
            out.append("-")
            continue
        try:
            out.append(answer(parts[0], int(parts[1])))
        except Exception:
            out.append("-")
    sys.stdout.write("\n".join(out) + ("\n" if out else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
