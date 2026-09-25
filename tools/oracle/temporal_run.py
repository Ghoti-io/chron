#!/usr/bin/env python3
"""Drive the Temporal differential: reference in its image, library on the host.

    temporal_run.py --driver <path to gchron_iso>

`temporal_diff.js` holds the corpus and the comparison and runs inside the
pinned node image. It cannot spawn the library's driver from in there, and
should not be able to: an oracle image containing the implementation it
answers for is no longer ignorant of it. So the work is split three ways, and
this is the only piece that needs to see both sides.

  1. ask the reference for the corpus (`--emit-corpus`)
  2. run the library's driver over it, here on the host
  3. hand the driver's verdicts back to the reference on stdin

The corpus is built from fixed tables with no randomness and no clock, so the
two invocations of the reference construct the identical list and the verdicts
line up by position. `temporal_diff.js` checks that count before comparing
anything, because a corpus that had drifted between the two calls would
silently compare each string against its neighbour's answer.
"""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_env

DIFF = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    "temporal_diff.js")


def reference(argv, stdin):
    command = oracle_env.command(
        "node", ["node", "--harmony-temporal", DIFF] + list(argv))
    return subprocess.run(command, input=stdin, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True)
    args = parser.parse_args()

    emitted = reference(["--emit-corpus"], "")
    if emitted.returncode != 0:
        sys.stderr.write(emitted.stderr)
        return emitted.returncode
    corpus = emitted.stdout
    if not corpus.strip():
        sys.stderr.write("temporal_run: the reference emitted no corpus\n")
        return 1

    driven = subprocess.run([args.driver], input=corpus, capture_output=True,
                            text=True)
    if driven.returncode != 0:
        sys.stderr.write(driven.stderr)
        return driven.returncode

    compared = reference([], driven.stdout)
    sys.stdout.write(compared.stdout)
    sys.stderr.write(compared.stderr)
    return compared.returncode


if __name__ == "__main__":
    sys.exit(main())
