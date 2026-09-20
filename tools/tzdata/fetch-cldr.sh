#!/bin/sh
#
# Fetch CLDR's windowsZones.xml, which tools/tzdata/windows_zones.py turns
# into the table gchron_zonedb_local() needs on Windows.
#
# The XML is not committed, for the reason tools/corpus/fetch.sh gives about
# the JSON Schema suite: it is somebody else's data, it is reproducible from a
# URL and a tag, and a copy here would be a snapshot that quietly stops
# matching what Windows and CLDR agree on. What is committed is the tag, in
# tools/tzdata/CLDR_TAG.
#
# Nothing in the build runs this. `make` does not reach the network, and a
# machine that has never run this simply has no Windows zone table - which is
# absent rather than empty, because an empty mapping would answer every lookup
# "unknown" and look exactly like one asked about a zone Windows added last
# year.
#
# Usage:  tools/tzdata/fetch-cldr.sh [tag]
#
# Copyright 2026 by Corey Pennycuff

set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
tag=${1:-$(cat "$root/tools/tzdata/CLDR_TAG")}
dest="$root/third_party/cldr"
url="https://raw.githubusercontent.com/unicode-org/cldr/$tag/common/supplemental/windowsZones.xml"

mkdir -p "$dest"

if command -v curl >/dev/null 2>&1; then
  curl -fsSL "$url" -o "$dest/windowsZones.xml"
elif command -v wget >/dev/null 2>&1; then
  wget -q "$url" -O "$dest/windowsZones.xml"
else
  echo "need curl or wget to fetch $url" >&2
  exit 1
fi

echo "fetched CLDR $tag windowsZones.xml to $dest"
echo "now run: python3 tools/tzdata/windows_zones.py"
