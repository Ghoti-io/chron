#!/usr/bin/env python3
"""Run one oracle gate, having first proved its reference is reachable.

    oracle_run.py <name>[,<name>...] -- <command> [args...]

Two jobs, from the pattern in `notes/suite/CONTAINERS.md`.

**Prove it, then print it.** The reference is resolved and asked its version
*before* the gate runs, and that version is printed on the line above the
gate's numbers. A clean LDML differential against ICU 76.1 and one against
78.3 are different statements - CLDR ships inside ICU - and a run that does not
say which it made cannot be read a week later.

**Fail closed.** With GHOTI_ORACLE_REQUIRED=1 an unreachable reference is an
error naming what is missing. Without it the gate still declines to run, but
declines *loudly*, with the word SKIPPED and a reason, having actually tried
rather than having read `command -v`.

That distinction is the point of the exercise. `command -v python3` answers
"is something called python3 on PATH", which is not the question; the question
is "can this gate reach the reference it names", and the only honest way to
answer it is to reach. chron's own history is the case in point in the other
direction: its five `command -v` guards were already hard failures rather than
skips, so nothing here was silently green - but `icu_format` still linked
whatever libicu the host had, and that is a reference nobody had written down.
"""

import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_env


def main(argv):
    if "--" not in argv:
        sys.stderr.write("usage: oracle_run.py <name>[,<name>] -- <command>\n")
        return 2
    cut = argv.index("--")
    names = [n for n in argv[1:cut][0].split(",") if n]
    command = argv[cut + 1:]
    required = os.environ.get("GHOTI_ORACLE_REQUIRED", "0") == "1"

    try:
        line = oracle_env.provenance(names)
    except oracle_env.OracleUnavailable as why:
        where = " ".join(command[:3])
        if required:
            sys.stderr.write(
                "### %s: the reference is not reachable ###\n%s\n"
                % (where, why))
            return 1
        sys.stderr.write("SKIPPED %s\n  %s\n" % (where, why))
        return 0
    print(line, flush=True)
    return subprocess.run(command).returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv))
