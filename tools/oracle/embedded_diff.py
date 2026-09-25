#!/usr/bin/env python3
"""Check the embedded table - the one this library ships - against an outside
authority.

`check-oracle-zoneinfo` covers the database built from the operating system's
zoneinfo directory. This covers the other one: the table
`tools/tzdata/embed.py` compiled into `src/zone/tzdata_embedded.c`, which
`gchron_zonedb_embedded()` serves and which is **the only database a Windows
build has** (design.md section 6.2). Until this existed nothing outside the
library had checked it for a single offset; `tests/unit/test_embedded.cpp`
checks that it is self-consistent - links resolve, what it lists it can open,
every Windows name resolves - and every one of those questions is answered from
inside the same table.

Two halves, because two different things can be wrong with a generated table.

**The population.** The reference here is `tzdata.zi`, the tzdb's own
declaration of what it contains, and not the filesystem. A distribution decides
for itself which backward-compatibility names it materialises as files - Debian
splits them into `tzdata-legacy`, which is not installed by default - so the
files present are a packaging decision, while `Z` and `L` lines are the tzdb's
own list. Every `Z` must be in the table; every `L` must be in it *as a link to
the target the file names*, which is the exact thing that would break Windows,
because CLDR's Windows mapping names `Asia/Calcutta` and `Europe/Kiev`. On this
machine the two counts meet exactly: 436 `Z` plus 162 `L` is 598 names, of
which 162 are links.

**The offsets.** Python's `zoneinfo`, over the same host tree the system
differential uses, at the same lattice of instants. A name Python cannot
construct - one of the backward names whose file this distribution does not
ship - is asked under the canonical name `tzdata.zi` gives it, and counted
separately: the bytes of a link and its target are identical, so an answer for
`America/New_York` is the answer `US/Eastern` must give, and a table that
pointed that name at the wrong image would be caught here and nowhere else.

**What this is not.** It is half independent, and the half it is missing is the
data. Both sides are tzdata 2026c and the embedded table was generated from the
very files Python reads, so two *readers* are being compared, not two sources.
That is worth having - it is what catches a transcription bug in `embed.py` or a
defect in chron's TZif reader, which is the pair nothing else here separates -
and it is not evidence that 2026c is right about Tehran.

Because of that, the releases have to match. The embedded table is frozen at
whatever release generated it and the host's moves with the distribution, so on
a host at a later release this comparison has no second reader of one set of
bytes to make and **declines rather than reporting the gap between two tzdb
releases as a defect**. The remedy is in the message: regenerate the table.

Usage:  tools/oracle/embedded_diff.py --driver <path> [--step SECONDS]

Copyright 2026 by Corey Pennycuff
"""

import argparse
import os
import subprocess
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_env
import zoneinfo_diff

ASK = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "zoneinfo_ask.py")

WHERE = "embedded_diff.py"


def declared(tzdir):
    """What `tzdata.zi` says the release contains: (zones, links).

    `zones` is every `Z` name; `links` maps each `L` name to its target. The
    first field decides, which is also how `tools/tzdata/embed.py` reads it: a
    zone's continuation lines begin with an offset, never with `Z` or `L`, so
    there is nothing here that indentation would settle and leading whitespace
    is not required to be present.
    """
    zones = set()
    links = {}
    path = os.path.join(tzdir, "tzdata.zi")
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            parts = line.split()
            if not parts:
                continue
            if parts[0] in ("Z", "Zone") and len(parts) >= 2:
                zones.add(parts[1])
            elif parts[0] in ("L", "Link") and len(parts) >= 3:
                links[parts[2]] = parts[1]
    return zones, links


def resolve(name, links, limit=8):
    """Follow `links` to a name that is not itself a link.

    Today's tzdb has no chains at all - no `L` target is another `L` name - so
    this loop runs once. It is written as a loop anyway because a chain would
    otherwise be followed one hop and the result quietly compared against the
    wrong zone, and the bound is there so that a cycle in a corrupt file cannot
    hang the gate.
    """
    seen = set()
    while name in links and len(seen) < limit:
        seen.add(name)
        name = links[name]
        if name in seen:
            return None
    return None if name in links else name


def table(driver):
    """The embedded table as the library reports it: {id: canonical or None}."""
    run = subprocess.run([driver, "--db", "embedded", "--list"],
                         capture_output=True, text=True)
    if run.returncode != 0:
        sys.stderr.write(run.stderr)
        raise SystemExit(run.returncode)
    held = {}
    for line in run.stdout.splitlines():
        name, _, canonical = line.partition("\t")
        if canonical == "ERR":
            sys.stderr.write("embedded_diff.py: the table lists %r and cannot "
                             "open it\n" % name)
            raise SystemExit(1)
        held[name] = None if canonical == "-" else canonical
    return held


def info(driver):
    """(source, version) for the embedded database."""
    run = subprocess.run([driver, "--db", "embedded", "--info"],
                         capture_output=True, text=True)
    if run.returncode != 0:
        sys.stderr.write(run.stderr)
        raise SystemExit(run.returncode)
    source, _, version = run.stdout.strip().partition("\t")
    return source, version


def reference(tzdir, argv, stdin):
    """Ask the pinned Python, over the host's zone files. See zoneinfo_diff."""
    command = oracle_env.command(
        "python", ["python3", ASK] + list(argv),
        readonly=[tzdir], env={"PYTHONTZPATH": tzdir})
    finished = subprocess.run(command, input=stdin, capture_output=True,
                             text=True)
    if finished.returncode != 0:
        sys.stderr.write(finished.stderr)
        raise SystemExit(finished.returncode)
    return finished.stdout.splitlines()


def population(held, zones, links, out):
    """Compare the table's names against the tzdb's own declaration.

    Returns the number of faults, and appends a line per finding to `out`. A
    missing `L` is the one with a name: it is what `embed.py`'s own docstring
    says it exists to prevent, and the failure mode is a Windows build refusing
    a zone CLDR handed it.
    """
    faults = 0
    expected = zones | set(links)

    missing_zones = sorted(zones - set(held))
    missing_links = sorted(set(links) - set(held))
    invented = sorted(set(held) - expected)
    if missing_zones:
        faults += len(missing_zones)
        out.append("%d zone(s) tzdata.zi declares that the table does not "
                   "hold: %s" % (len(missing_zones),
                                 ", ".join(missing_zones[:10])))
    if missing_links:
        faults += len(missing_links)
        out.append("%d link(s) tzdata.zi declares that the table does not "
                   "hold, which is what a Windows build would refuse: %s"
                   % (len(missing_links), ", ".join(missing_links[:10])))
    if invented:
        faults += len(invented)
        out.append("%d name(s) the table holds that tzdata.zi does not "
                   "declare: %s" % (len(invented), ", ".join(invented[:10])))

    wrong = []
    for name, target in sorted(links.items()):
        if name not in held:
            continue
        if held[name] != target:
            wrong.append("%s -> %s, tzdata.zi says %s"
                         % (name, held[name], target))
    #
    # The other direction: a canonical zone must not claim to be a link. This
    # is the half that a table generated from symlinks alone gets wrong in the
    # opposite direction, by recording a name as a link to whatever the
    # filesystem happened to point it at.
    #
    for name in sorted(zones & set(held)):
        if held[name] is not None:
            wrong.append("%s -> %s, tzdata.zi declares it a zone, not a link"
                         % (name, held[name]))
    if wrong:
        faults += len(wrong)
        out.append("%d name(s) whose canonical name disagrees with tzdata.zi:"
                   % len(wrong))
        out.extend("  " + line for line in wrong[:10])
    return faults


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True)
    parser.add_argument("--step", type=int, default=7 * 86400,
                        help="seconds between lattice probes")
    parser.add_argument("--max-report", type=int, default=25)
    args = parser.parse_args()

    tzdir = os.environ.get("TZDIR", "/usr/share/zoneinfo")
    source, embedded_release = info(args.driver)
    if source != "embedded":
        sys.stderr.write("embedded_diff.py: the driver opened the %s database, "
                         "not the embedded one\n" % source)
        return 1

    if not os.path.exists(os.path.join(tzdir, "tzdata.zi")):
        return oracle_env.decline(WHERE,
            "%s has no tzdata.zi, so the tzdb's own declaration of what the "
            "release contains is not available here and the population half "
            "of this differential has no reference." % tzdir)

    host_release = zoneinfo_diff.tzdata_release(tzdir)
    #
    # Not a defect and not something to report as one: the table is frozen at
    # the release that generated it, and this comparison is between two readers
    # of *one* release's bytes. Two releases is a different comparison, whose
    # every difference is upstream's intent.
    #
    if host_release != embedded_release:
        return oracle_env.decline(WHERE,
            "the embedded table is tzdata %s and %s is tzdata %s.\n"
            "  This differential compares two readers of one release. Across "
            "two releases every\n"
            "  difference is upstream's intent and none of it is a finding, so "
            "there is nothing\n"
            "  here to compare. The table is generated rather than committed "
            "and its only\n"
            "  prerequisite is the generator, so a build tree outlives a "
            "tzdata update:\n"
            "    rm src/zone/tzdata_embedded.c && make"
            % (embedded_release, tzdir, host_release or "unknown"))

    zones, links = declared(tzdir)
    held = table(args.driver)
    if not held:
        sys.stderr.write("embedded_diff.py: the embedded table is empty\n")
        return 1

    findings = []
    faults = population(held, zones, links, findings)

    # Which names the reference can construct at all, asked of the reference
    # rather than of the host: it is a statement about that interpreter reading
    # that tree, and the host's python3 is neither.
    constructible = set(reference(tzdir, ["--list"], ""))

    instants = zoneinfo_diff.FIXED + zoneinfo_diff.lattice(args.step)
    direct = []
    redirected = {}
    unaskable = []
    for name in sorted(held):
        if name in constructible:
            direct.append(name)
            continue
        canonical = resolve(name, links)
        if canonical is not None and canonical in constructible:
            redirected[name] = canonical
            continue
        unaskable.append(name)

    asked = direct + sorted(redirected)
    queries = []
    wants_queries = []
    for name in asked:
        probe = redirected.get(name, name)
        for seconds in instants:
            queries.append("%s\t%d" % (name, seconds))
            wants_queries.append("%s\t%d" % (probe, seconds))

    if not queries:
        sys.stderr.write("embedded_diff.py: no name in the embedded table "
                         "could be asked\n")
        return 1

    run = subprocess.run([args.driver, "--db", "embedded"],
                         input="\n".join(queries) + "\n", capture_output=True,
                         text=True)
    if run.returncode != 0:
        sys.stderr.write(run.stderr)
        return run.returncode
    lines = run.stdout.splitlines()
    if len(lines) != len(queries):
        sys.stderr.write("embedded_diff.py: the driver answered %d of %d "
                         "queries\n" % (len(lines), len(queries)))
        return 1

    wants = reference(tzdir, [], "\n".join(wants_queries) + "\n")
    if len(wants) != len(wants_queries):
        sys.stderr.write("embedded_diff.py: the reference answered %d of %d "
                         "queries\n" % (len(wants), len(wants_queries)))
        return 1

    agreed = 0
    agreed_via = 0
    refused = 0
    mismatches = []
    for query, line, want in zip(queries, lines, wants):
        name, seconds = query.split("\t")
        if want == "-":
            refused += 1
            continue
        got = line.split("\t", 2)[2] if line.count("\t") >= 2 else ""
        if got != want:
            if len(mismatches) < args.max_report:
                via = redirected.get(name)
                mismatches.append(
                    "  %s at %s%s\n    chron:  %s\n    python: %s"
                    % (name, seconds, "" if via is None else " (asked as %s)"
                       % via, got, want))
            continue
        if name in redirected:
            agreed_via += 1
        else:
            agreed += 1

    print("embedded table: tzdata %s, %d names (%d links); reference reads %s "
          "(tzdata %s)" % (embedded_release, len(held),
                           sum(1 for v in held.values() if v is not None),
                           tzdir, host_release))
    print("population: %d Z and %d L declared in tzdata.zi, %d fault%s"
          % (len(zones), len(links), faults, "" if faults == 1 else "s"))
    for line in findings:
        print("  " + line)
    print("%d agreed, %d agreed under the canonical name tzdata.zi gives them, "
          "%d could not be asked, %d disagreed"
          % (agreed, agreed_via, refused,
             len(queries) - agreed - agreed_via - refused))
    if unaskable:
        #
        # Named rather than counted, for the same reason the system
        # differential names its own: these are the rows where a green line
        # means less than it looks like.
        #
        print("%d name(s) in the table that neither this reference nor the "
              "canonical name reaches, so nothing here checks them:"
              % len(unaskable))
        for name in unaskable[:25]:
            print("  %s" % name)
        if len(unaskable) > 25:
            print("  ... and %d more" % (len(unaskable) - 25))

    if mismatches:
        print("\nfirst disagreements:")
        print("\n".join(mismatches))
        return 1
    if faults:
        return 1
    #
    # Silence is not success. Every count above can be zero at once - an empty
    # query set is already refused, but a run in which the reference refused
    # every name would otherwise print three zeroes and exit 0.
    #
    if agreed + agreed_via == 0:
        sys.stderr.write("embedded_diff.py: nothing was compared\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
