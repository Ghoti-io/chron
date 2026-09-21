#!/usr/bin/env python3
"""Generate src/zone/tzdata_embedded.c from an IANA zoneinfo tree.

This is the database `gchron_zonedb_embedded()` serves, and the only one a
machine with no zoneinfo directory has - which on Windows is every machine
(design.md section 6.2).

It is generated, never edited, and **fails rather than writing a placeholder**
(CONVENTIONS.md section 6). An embedded database with no zones in it would
answer every lookup with GCHRON_ERR_UNSUPPORTED and look exactly like a
working one that had been asked for a zone it did not have.

Two things shrink it. Identical TZif images are stored once - 487 zone names
share 436 distinct images, because a symbolic link and its target are the same
bytes. And the names that are links are recorded as links, which is also what
gives `gchron_zone_canonical_id()` something true to say: TZif has nowhere to
record the name a link points at, so without this table the canonical name of
`US/Eastern` is `US/Eastern`.

Usage:
    tools/tzdata/embed.py [tzdir] [-o output]

`tzdir` defaults to $TZDIR, then /usr/share/zoneinfo.
"""

import argparse
import hashlib
import os
import pathlib
import re
import sys

DEFAULT_DIRS = [os.environ.get("TZDIR"), "/usr/share/zoneinfo"]

# Files in a zoneinfo directory that are not zones. Checked by content as
# well - the TZif magic decides - but skipping them by name keeps the
# generator from reading a few megabytes it will only discard.
SKIP_NAMES = {
    "iso3166.tab", "zone.tab", "zone1970.tab", "zonenow.tab",
    "leapseconds", "leap-seconds.list", "tzdata.zi", "posixrules",
    "localtime", "SECURITY", "+VERSION",
}
SKIP_DIRS = {"right", "posix"}


def find_version(tzdir):
    """The tzdata release, e.g. "2026c", or None.

    Two places carry it: a `+VERSION` file, which is what a zic install
    writes, and the `# version` line of `tzdata.zi`.
    """
    version_file = os.path.join(tzdir, "+VERSION")
    if os.path.exists(version_file):
        text = pathlib.Path(version_file).read_text().strip()
        if text:
            return text.split()[0]

    zi = os.path.join(tzdir, "tzdata.zi")
    if os.path.exists(zi):
        with open(zi, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                found = re.match(r"#\s*version\s+(\S+)", line)
                if found:
                    return found.group(1)
                if not line.startswith("#"):
                    break
    return None


def read_links(tzdir):
    """The tzdb's own link table, from `tzdata.zi`: {link name: target}.

    Symbolic links in the directory are not enough. Distributions differ in
    how they materialise backward-compatibility names: Debian splits them into
    a `tzdata-legacy` package that is not installed by default, so on a stock
    Debian there is no `Asia/Calcutta` file at all - while `tzdata.zi`, which
    ships with base tzdata, lists every one of them.

    That gap matters more here than anywhere else. The embedded table is what
    Windows uses, and CLDR's Windows mapping names `Asia/Calcutta`,
    `Europe/Kiev` and five more like them - so a table built only from the
    files present would fail to resolve exactly the names Windows hands it.
    """
    links = {}
    path = os.path.join(tzdir, "tzdata.zi")
    if not os.path.exists(path):
        return links
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            parts = line.split()
            # `L <target> <link name>`, or the long spelling `Link`.
            if len(parts) >= 3 and parts[0] in ("L", "Link"):
                links[parts[2]] = parts[1]
    return links


def collect(tzdir):
    """Return (names, blobs) where names maps zone id -> (sha, link_target)."""
    names = {}
    blobs = {}

    for root, dirs, files in os.walk(tzdir):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if name in SKIP_NAMES:
                continue
            path = os.path.join(root, name)
            zone = os.path.relpath(path, tzdir).replace(os.sep, "/")
            try:
                data = open(path, "rb").read()
            except OSError:
                continue
            if not data.startswith(b"TZif"):
                continue

            digest = hashlib.sha1(data).hexdigest()
            blobs.setdefault(digest, data)

            target = None
            if os.path.islink(path):
                resolved = os.path.realpath(path)
                try:
                    target = os.path.relpath(resolved, tzdir) \
                        .replace(os.sep, "/")
                except ValueError:
                    target = None
                if target is not None and target.startswith(".."):
                    target = None
            names[zone] = (digest, target)

    # Now fold in the tzdb's own link table. A name it lists that has no file
    # here is added, pointing at its target's image; a name that does have a
    # file keeps that file but takes its canonical name from here, because
    # tzdata.zi states the link and a symlink only implies it.
    for link, target in read_links(tzdir).items():
        if target not in names:
            continue  # a link to something this tree does not carry
        if link in names:
            names[link] = (names[link][0], target)
        else:
            names[link] = (names[target][0], target)

    return names, blobs


def c_bytes(data):
    """The blob as an array initialiser, 20 bytes to the line.

    A string literal would be a quarter of the source bytes and much less work
    for the compiler - but C99 only requires a string literal of 4095
    characters to be supported, this blob is a hundred times that, and the
    build runs with -pedantic-errors. An array has no such limit. Splitting
    the blob into literals short enough to be portable would mean a zone's
    image could straddle two of them, which is a reassembly step at load time
    to save build time, and that is the wrong trade for a library.
    """
    lines = []
    for start in range(0, len(data), 20):
        chunk = data[start:start + 20]
        lines.append("  " + "".join("0x%02x," % b for b in chunk))
    return "\n".join(lines) if lines else "  0x00"


def render(version, names, blobs, tzdir):
    # Lay the distinct images out in one blob, in a stable order.
    order = sorted(blobs)
    offsets = {}
    cursor = 0
    parts = []
    for digest in order:
        offsets[digest] = cursor
        parts.append(blobs[digest])
        cursor += len(blobs[digest])
    blob = b"".join(parts)

    rows = []
    for zone in sorted(names):
        digest, target = names[zone]
        # A link whose target is not itself a zone we hold is recorded as an
        # ordinary zone: the bytes are right either way, and claiming a
        # canonical name we cannot resolve would be worse than claiming none.
        if target is not None and target not in names:
            target = None
        canonical = "NULL" if target is None else '"%s"' % target
        rows.append('  { "%s", %s, %d, %d },'
                    % (zone, canonical, offsets[digest], len(blobs[digest])))

    linked = sum(1 for zone in names if names[zone][1] in names)

    return TEMPLATE % {
        "tzdir": tzdir,
        "version": version,
        "zone_count": len(names),
        "blob_count": len(order),
        "blob_bytes": len(blob),
        "link_count": linked,
        "blob": c_bytes(blob),
        "rows": "\n".join(rows),
    }


# Every generated source carries the same licence notice as a hand-written
# one. It is emitted here rather than added afterwards, so that regenerating
# does not quietly drop it.
LICENSE_NOTICE = """\
/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Chron.
 *
 * Ghoti.io Chron is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Chron is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
"""


TEMPLATE = LICENSE_NOTICE + "\n" + '''/**
 * @file
 *
 * The time-zone database compiled into this library.
 *
 * GENERATED by tools/tzdata/embed.py from %(tzdir)s (tzdata %(version)s).
 * Do not edit: regenerate it.
 *
 * %(zone_count)d zone names, of which %(link_count)d are links, sharing
 * %(blob_count)d distinct TZif images totalling %(blob_bytes)d bytes. A link
 * and its target are the same bytes, so they are stored once; what the link
 * adds is a canonical name, which TZif itself has nowhere to record.
 */

#include <ghoti.io/chron/macros.h>
#include <stddef.h>
#include <string.h>

#include "zone_internal.h"

/** The tzdata release these images were built from. */
static const char VERSION[] = "%(version)s";

/**
 * Every distinct TZif image, end to end.
 *
 * An array rather than a string literal: C99 requires only 4095 characters of
 * literal to be supported, this is a hundred times that, and the build runs
 * with -pedantic-errors.
 */
static const unsigned char BLOB[] = {
%(blob)s
};

/** The zone names, sorted, so that a lookup is a binary search. */
static const GCHRON_EmbeddedZone ZONES[] = {
%(rows)s
};

const char * gchron_tzdata_embedded_version(void) {
  return VERSION;
}

size_t gchron_tzdata_embedded_count(void) {
  return sizeof(ZONES) / sizeof(ZONES[0]);
}

const GCHRON_EmbeddedZone * gchron_tzdata_embedded_at(size_t index) {
  if (index >= gchron_tzdata_embedded_count()) {
    return NULL;
  }
  return &ZONES[index];
}

const GCHRON_EmbeddedZone * gchron_tzdata_embedded_find(const char * id) {
  size_t low = 0;
  size_t high = gchron_tzdata_embedded_count();

  if (id == NULL) {
    return NULL;
  }
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    int order = strcmp(id, ZONES[mid].id);
    if (order == 0) {
      return &ZONES[mid];
    }
    if (order < 0) {
      high = mid;
    }
    else {
      low = mid + 1;
    }
  }
  return NULL;
}

const void * gchron_tzdata_embedded_bytes(const GCHRON_EmbeddedZone * zone) {
  if (zone == NULL) {
    return NULL;
  }
  return BLOB + zone->offset;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tzdir", nargs="?", default=None)
    parser.add_argument("-o", "--output", default="src/zone/tzdata_embedded.c")
    args = parser.parse_args()

    tzdir = args.tzdir
    if tzdir is not None:
        if not os.path.isdir(tzdir):
            raise SystemExit("not a directory: %s" % tzdir)
    else:
        for candidate in DEFAULT_DIRS:
            if candidate and os.path.isdir(candidate):
                tzdir = candidate
                break
        if tzdir is None:
            raise SystemExit(
                "no zoneinfo directory found (looked in %s).\n"
                "Pass one explicitly, or unpack one from\n"
                "  https://data.iana.org/time-zones/releases/"
                % ", ".join(d for d in DEFAULT_DIRS if d))

    version = find_version(tzdir)
    if version is None:
        raise SystemExit(
            "%s carries no tzdata version (+VERSION or tzdata.zi's "
            "'# version' line).\nThe embedded table must be able to say which "
            "release it is, because gchron_zonedb_default() chooses between it "
            "and the system database by comparing the two." % tzdir)

    names, blobs = collect(tzdir)
    if not names:
        raise SystemExit("no TZif files under %s" % tzdir)

    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render(version, names, blobs, tzdir))

    print("%s: tzdata %s, %d zones, %d distinct images, %d bytes"
          % (args.output, version, len(names), len(blobs),
             sum(len(b) for b in blobs.values())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
