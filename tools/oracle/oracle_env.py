#!/usr/bin/env python3
"""How an oracle is spelled, so that no tool here spells one itself.

The pattern comes from the same exploration the other libraries adopted, and
`unicode` landed it first. What it fixes here
is that three of this library's four differentials used to *be* their own
reference: `zoneinfo_diff.py` called `zoneinfo.ZoneInfo` in its own process,
`temporal_diff.js` ran under whichever node was on PATH, and `icu_format`
linked whatever libicu pkg-config found. None of those is a pin that can be
written down, and the ICU one was measurably wrong - this machine answers LDML
questions with ICU 76.1 while `unicode` pins 78.3, and until 2026-09-24
neither repository said so.

`command(name)` returns the argv prefix that runs a reference, which is either
this machine's own tool or a `docker run` into an image pinned in
`containers/IMAGES`.

Three properties, in the order they matter:

  1. **It does not fail open.** A missing image, a missing engine, or a version
     that does not match its pin each raises. Host tools have to be asked for
     by name. A gate that cannot reach its reference must say so and fail;
     "skipped" printed where a comparison should be is what this directory
     exists to prevent.

  2. **It says which instrument answered.** `provenance()` returns the line
     every gate prints above its numbers.

  3. **Paths mean the same thing on both sides.** The repository is mounted at
     its own host path, so a path a caller already built resolves unchanged.

**Where this library departs from `unicode`, and why.** There the reference
*carries* the data - a CPython is a UCD version - so `IMAGES` pins the data
version and the interpreter is incidental. Here the data is mostly the subject
rather than the reference. The zone differential compares two readers of one
set of TZif files, and those files are the host's: pinning the container's
tzdata would mean comparing chron at the host's release against Python at the
image's, which measures the gap between two tzdb releases and calls it a
defect. So the zone oracle mounts the host's zone directory and reads it, the
pin names the *reader*, and the differential prints the tzdb release beside its
numbers. ICU is the other way round - CLDR ships inside ICU - so that pin does
name the data.

Modes, from GHOTI_ORACLE_MODE:

  container  (default) run the reference in its pinned image
  host                 run this machine's own tool, and print what it is
"""

import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# Two directories up, so this file has to live at <repo>/tools/oracle/.
ROOT = os.path.dirname(os.path.dirname(HERE))
IMAGES = os.path.join(HERE, "containers", "IMAGES")

MODE = os.environ.get("GHOTI_ORACLE_MODE", "container")
ENGINE = os.environ.get("GHOTI_CONTAINER_ENGINE", "docker")


class OracleUnavailable(Exception):
    """The reference cannot be reached. Never caught into a skip."""


# How to ask each reference what it is, and what the answer must contain.
#
# Checked in container mode as well as host mode, because IMAGES is maintained
# by hand: a version field that has drifted from the image it names is a lie
# nothing else would catch.
PROBE = {
    # CPython's own release is the pin here, not a data version - see the
    # module docstring. The tzdb release is the host's and the differential
    # reports it separately.
    "python": ([
        "python3", "-c",
        "import sys; print('Python %s' % sys.version.split()[0])",
    ], "Python "),
    # Temporal is V8's, so the pin that matters is node's V8, which node
    # reports alongside its own version.
    "node": ([
        "node", "-e",
        "console.log('Node ' + process.versions.node"
        " + ', V8 ' + process.versions.v8)",
    ], "Node "),
    # Compiled *into* the image, reporting U_ICU_VERSION from the headers the
    # driver will be compiled against - so the pin is checked against the same
    # tables that answer the questions. Parsing `icuinfo`'s XML would put a sed
    # script between the check and the fact it checks.
    "icu": (["icu-version"], "ICU "),
}

_pins = None
_cache = {}


def pins():
    """The IMAGES table: name -> (image, version, description)."""
    global _pins
    if _pins is not None:
        return _pins
    _pins = {}
    if not os.path.exists(IMAGES):
        return _pins
    with open(IMAGES, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 3:
                raise OracleUnavailable(
                    "containers/IMAGES: not three tab-separated fields: %r"
                    % line)
            _pins[parts[0]] = (parts[1], parts[2],
                               parts[3] if len(parts) > 3 else "")
    return _pins


def _engine_ok():
    if shutil.which(ENGINE) is None:
        raise OracleUnavailable(
            "%s is not on PATH, and GHOTI_ORACLE_MODE is 'container'.\n"
            "Install it, or run with GHOTI_ORACLE_MODE=host to use this "
            "machine's own tools - which answers a different question, and "
            "says so in the line it prints." % ENGINE)


def _have_image(image):
    finished = subprocess.run([ENGINE, "image", "exists", image],
                              capture_output=True)
    if finished.returncode == 0:
        return True
    # `image exists` is podman's. Fall back to a docker-portable spelling.
    finished = subprocess.run([ENGINE, "image", "inspect", image],
                              capture_output=True)
    return finished.returncode == 0


def ensure(name):
    """Make the reference runnable, or raise saying what is missing."""
    if MODE == "host":
        binary = PROBE.get(name, ([name], ""))[0][0]
        if shutil.which(binary) is None:
            raise OracleUnavailable(
                "GHOTI_ORACLE_MODE=host and %s is not on PATH" % binary)
        return
    if MODE != "container":
        raise OracleUnavailable("GHOTI_ORACLE_MODE=%r is not a mode" % MODE)
    _engine_ok()
    table = pins()
    if name not in table:
        raise OracleUnavailable(
            "no pin for %r in tools/oracle/containers/IMAGES" % name)
    image = table[name][0]
    if _have_image(image):
        return
    #
    # A built-here image cannot be pulled, and saying "pull failed" for one
    # would send the reader to the registry instead of to the Dockerfile that
    # makes it.
    #
    if image.startswith("localhost/"):
        raise OracleUnavailable(
            "the %s image is built here and is not present: %s\n"
            "Build it with: make oracle-images" % (name, image))
    if os.environ.get("GHOTI_ORACLE_PULL", "1") != "1":
        raise OracleUnavailable(
            "image for %s is not present and GHOTI_ORACLE_PULL is off: %s"
            % (name, image))
    sys.stderr.write("oracle: pulling %s\n" % image)
    finished = subprocess.run([ENGINE, "pull", image], capture_output=True,
                              text=True)
    if finished.returncode != 0:
        raise OracleUnavailable(
            "could not pull the pinned image for %s.\n  %s\n%s"
            % (name, image, finished.stderr.strip()))


def command(name, argv=None, scratch=None, readonly=None, env=None):
    """The argv prefix that runs `name`'s reference.

    `argv` is what to run inside, defaulting to the reference's own tool. The
    repository is bind-mounted at its own path, so any path a caller has
    already built resolves without translation.

    `readonly` names further paths to mount read-only at the same path. The
    zone oracle needs this and it is the whole reason the parameter exists:
    the reference has to read *the host's* TZif files, not the image's, or the
    differential compares two tzdb releases rather than two readers.

    `scratch` is a directory the reference must be able to write, named the
    same way. Passing it explicitly rather than mounting /tmp means a tool that
    forgets to declare one fails on a missing path instead of writing where
    nobody looks.

    Everything else is closed: `--network none`, because no reference here has
    business reaching the network, and the tree read-only, because a corpus
    quietly edited by the thing being compared against it is not a comparison.
    """
    ensure(name)
    inner = argv if argv is not None else [PROBE.get(name, ([name],))[0][0]]
    if MODE == "host":
        return list(inner)
    image = pins()[name][0]
    out = [ENGINE, "run", "--rm", "-i",
           "--network", "none",
           "--volume", "%s:%s:ro" % (ROOT, ROOT)]
    for path in ([readonly] if isinstance(readonly, str) else (readonly or [])):
        out += ["--volume", "%s:%s:ro" % (path, path)]
    for path in ([scratch] if isinstance(scratch, str) else (scratch or [])):
        out += ["--volume", "%s:%s:rw" % (path, path)]
    for key, value in (env or {}).items():
        out += ["--env", "%s=%s" % (key, value)]
    return out + ["--workdir", ROOT, image] + list(inner)


def version(name):
    """What the reference says it is. Runs it; the answer is cached."""
    key = ("version", name)
    if key in _cache:
        return _cache[key]
    probe, expect = PROBE.get(name, ([name, "--version"], ""))
    finished = subprocess.run(command(name, probe), capture_output=True,
                              text=True)
    #
    # stdout only. `docker` on this machine is a podman shim that prints a
    # banner to stderr on every invocation, and a probe reading both streams
    # reads the banner. The same trap waits for any driver that merges them:
    # the reference's answers and the engine's chatter would interleave on one
    # stream and the extra line would be scored as a disagreement.
    #
    text = finished.stdout.strip().splitlines()
    text = text[0] if text else ""
    if expect and expect not in text:
        raise OracleUnavailable(
            "%s answered %r, which does not look like a version"
            % (name, text))
    _cache[key] = text
    return text


def check_pin(name):
    """Raise unless the reference's version matches containers/IMAGES.

    Container mode only. Host mode reports rather than asserts and says
    `unpinned` in the line it prints, which is the claim it is entitled to
    make: the escape hatch exists so that a machine without an engine can still
    run the differential and know it has not run the pinned one.
    """
    table = pins()
    if name not in table:
        return version(name)
    said = table[name][1]
    got = version(name)
    if MODE != "host" and said not in got:
        raise OracleUnavailable(
            "%s: IMAGES says %s and it answers %r" % (name, said, got))
    return got


def decline(where, why):
    """Report a reference this gate cannot reach, and say what that means.

    Returns the exit status the gate should use: 1 under
    GHOTI_ORACLE_REQUIRED=1, otherwise 0 with the word SKIPPED and a reason.

    `oracle_run.py` calls this for a reference it could not resolve *before*
    the gate ran. A gate calls it directly for the other case, which
    `embedded_diff.py` is the first of: the reference resolved, answered its
    version, and only then turned out not to hold the data this particular
    comparison needs. One spelling of the protocol rather than two, because a
    second one would drift - and the shape of the line is what a reader greps
    for.
    """
    if os.environ.get("GHOTI_ORACLE_REQUIRED", "0") == "1":
        sys.stderr.write(
            "### %s: the reference this gate needs is not available ###\n%s\n"
            % (where, why))
        return 1
    sys.stderr.write("SKIPPED %s\n  %s\n" % (where, why))
    return 0


def provenance(names):
    """One line naming every reference that answered, and how."""
    where = "container" if MODE == "container" else "host, unpinned"
    parts = ["%s %s" % (name, check_pin(name)) for name in names]
    return "oracle(%s): %s" % (where, ", ".join(parts))
