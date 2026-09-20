#!/bin/sh
#
# Fetch the JSON-Schema-Test-Suite files the format vectors are generated
# from.
#
# The suite itself is not committed here: it is somebody else's corpus, it is
# reproducible from a commit and a URL, and a copy in this repository would be
# a snapshot that quietly stops being the thing every other implementation is
# measured against. What *is* committed is the commit hash, in
# tools/corpus/JSON_SCHEMA_COMMIT, and the vectors generated from it under
# tests/data/vectors/parse - so that `make test` never needs the network and a
# published pass rate names the corpus it was measured over.
#
# After fetching, `make vectors` regenerates the committed vectors. A CI with
# the corpus fetched can run that and fail on a diff, which is how an upstream
# change is noticed rather than absorbed.
#
# Usage:  tools/corpus/fetch.sh [commit]
#
# Copyright 2026 by Corey Pennycuff

set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
commit=${1:-$(cat "$root/tools/corpus/JSON_SCHEMA_COMMIT")}
dest="$root/third_party/json-schema-test-suite/$commit"
base="https://raw.githubusercontent.com/json-schema-org/JSON-Schema-Test-Suite/$commit"

# The four `format` keywords design.md section 11 says `text`'s JSON Schema
# side needs. The rest of the suite measures `text`'s schema engine, which is
# that library's business and not this one's.
files="
tests/draft2020-12/optional/format/date-time.json
tests/draft2020-12/optional/format/date.json
tests/draft2020-12/optional/format/time.json
tests/draft2020-12/optional/format/duration.json
"

for path in $files; do
  if [ -s "$dest/$path" ]; then
    printf 'have    %s\n' "$path"
    continue
  fi
  printf 'fetch   %s\n' "$path"
  mkdir -p "$dest/$(dirname "$path")"
  # --fail so that an HTML error page never lands on disk looking like data.
  curl --fail --silent --show-error --location \
      --output "$dest/$path.partial" "$base/$path"
  mv "$dest/$path.partial" "$dest/$path"
done

printf '\nJSON-Schema-Test-Suite %s is in %s\n' "$commit" "$dest"
printf 'Run `make vectors` to regenerate tests/data/vectors/parse from it.\n'
