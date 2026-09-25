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
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_env

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


def tzdata_release(tzdir):
    """The tzdb release the files under `tzdir` came from, or None."""
    try:
        with open(os.path.join(tzdir, "tzdata.zi"), "r") as handle:
            for line in handle:
                if line.startswith("# version "):
                    return line.split(None, 2)[2].strip()
    except OSError:
        pass
    return None


# Not zones, though they are TZif files under the tree.
#
# `posixrules` is the POSIX fallback rule set, not a name anyone asks for.
#
# `localtime` is a pointer to whichever zone this machine is set to, so
# comparing it asks about the machine's configuration rather than about the
# tzdb - and the zone it points at is already in the population under its own
# name. It is also the one entry that cannot survive being read through a
# mount: it is a symlink to /etc/localtime, which is *outside* the zone tree,
# so a reference reading the tree from a container follows it to that
# container's own /etc/localtime and answers UTC. That produced 218
# disagreements the first time this gate ran containerised, all of them this
# one name, and every one of them looking like a defect in chron.
NOT_ZONES = ("posixrules", "localtime")


def system_zones(tzdir):
    """Every zone the database holds, named the way it names them.

    A zone is a file whose first four bytes are `TZif`, which is what the
    library's own reader requires.

    Returns (zones, escapes). An escape is a symlink whose immediate target
    leaves the tree: it resolves to different bytes for a reader that reaches
    the tree through a mount, so it is excluded and named rather than compared.
    Checking the immediate hop rather than the fully resolved path is the whole
    trick - on this machine `localtime` points at `/etc/localtime`, which
    points back at `America/Chicago`, so a check that resolves the whole chain
    sees a target inside the tree and finds nothing wrong.
    """
    found = set()
    escapes = set()
    root = pathlib.Path(tzdir)
    if not root.is_dir():
        return found, escapes
    prefix = str(root.resolve()) + os.sep
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        try:
            with open(path, "rb") as handle:
                if handle.read(4) != b"TZif":
                    continue
        except OSError:
            continue
        name = str(path.relative_to(root))
        if name in NOT_ZONES:
            continue
        if path.is_symlink():
            target = os.readlink(str(path))
            if os.path.isabs(target) and not target.startswith(prefix):
                escapes.add(name)
                continue
        found.add(name)
    return found, escapes


def lattice(step):
    start = calendar.timegm((2024, 1, 1, 0, 0, 0, 0, 0, 0))
    end = calendar.timegm((2028, 1, 1, 0, 0, 0, 0, 0, 0))
    return list(range(start, end, step))


ASK = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "zoneinfo_ask.py")


def reference(tzdir, argv, stdin):
    """Run the reference over the host's zone files.

    The mount is the whole point. The image ships tzdata of its own, frozen by
    the digest that pins it, and the host's moves with the distribution. A
    differential between two *readers* has to hand both of them the same bytes,
    or the first time the host updates it starts reporting the gap between two
    tzdb releases as though this library had regressed.
    """
    command = oracle_env.command(
        "python", ["python3", ASK] + list(argv),
        readonly=[tzdir], env={"PYTHONTZPATH": tzdir})
    finished = subprocess.run(command, input=stdin, capture_output=True,
                              text=True)
    if finished.returncode != 0:
        sys.stderr.write(finished.stderr)
        raise SystemExit(finished.returncode)
    return finished.stdout.splitlines()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True)
    parser.add_argument("--step", type=int, default=7 * 86400,
                        help="seconds between lattice probes")
    parser.add_argument("--max-report", type=int, default=25)
    args = parser.parse_args()

    tzdir = os.environ.get("TZDIR", "/usr/share/zoneinfo")
    listed = set(reference(tzdir, ["--list"], ""))
    #
    # The population is the *database's*, which is what the docstring above
    # claims and what this did not do: it swept
    # zoneinfo.available_timezones(), so the set being checked was the
    # oracle's and a zone the database holds that the oracle cannot construct
    # was not skipped, not reported, and not in any denominator - it simply
    # was not a question. On this machine the two sets differ by one file and
    # that file is not a zone, so the narrowing was invisible; on a machine
    # whose tzdata omits the backward-compatibility links while the database
    # carries them, it would not be.
    #
    held, escapes = system_zones(tzdir)
    zones = sorted((held | listed) - set(NOT_ZONES) - escapes)
    if not zones:
        sys.stderr.write("zoneinfo.py: no zones available; nothing checked\n")
        return 1
    unaskable = sorted(held - listed)

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

    wants = reference(tzdir, [], stdin)
    if len(wants) != len(queries):
        sys.stderr.write("zoneinfo.py: the reference answered %d of %d "
                         "queries\n" % (len(wants), len(queries)))
        return 1

    checked = 0
    skipped = 0
    mismatches = []
    for query, line, want in zip(queries, lines, wants):
        zone, seconds = query.split("\t")
        if want == "-":
            skipped += 1
            continue
        got = line.split("\t", 2)[2] if line.count("\t") >= 2 else ""
        if got != want:
            if len(mismatches) < args.max_report:
                mismatches.append("  %s at %s\n    chron:  %s\n    python: %s"
                                  % (zone, seconds, got, want))
            continue
        checked += 1

    release = tzdata_release(tzdir)
    print("tzdir %s (tzdata %s): %d zones, %d probes each"
          % (tzdir, release or "unknown", len(zones), len(instants)))
    print("%d agreed, %d could not be asked, %d disagreed"
          % (checked, skipped, len(queries) - checked - skipped))
    #
    # Named rather than merely counted: a zone the database holds and the
    # oracle cannot construct is the one case where a green line means less
    # than it looks like, so it is worth reading rather than inferring from
    # two totals.
    #
    if escapes:
        print("%d name(s) excluded as symlinks leaving the zone tree, which a "
              "reference reading it through a mount would resolve differently:"
              % len(escapes))
        for name in sorted(escapes):
            print("  %s -> %s" % (name, os.readlink(os.path.join(tzdir, name))))
    if unaskable:
        print("%d zone(s) the database holds that this oracle cannot "
              "construct, so nothing here checks them:" % len(unaskable))
        for name in unaskable[:25]:
            print("  %s" % name)
        if len(unaskable) > 25:
            print("  ... and %d more" % (len(unaskable) - 25))
    #
    # This is the *system* database, which gchron_zone drives through
    # gchron_zonedb_system(). The embedded table is a different population and
    # no differential reaches it; notes/chron/ORACLES-OPEN.md has the detail.
    #
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
