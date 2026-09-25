# ICU, the reference for what an LDML pattern means.
#
# design.md section 8.3 makes ICU the *definition* of the pattern language, so
# unlike the other two oracles here a disagreement is a defect rather than a
# reading. That is the reason this one is pinned most carefully, and the reason
# the driver compiled against it links **only ICU** - as `regex`'s pcre2 driver
# links only pcre2 - so the reference cannot reach the implementation it is
# answering for. chron never enters this image; there is no library built in
# here and nothing to keep isolated by convention.
#
# **Built here rather than taken from the host**, because "whatever pkg-config
# finds" is not a pin. Until 2026-09-24 neither this repository nor `unicode`
# recorded which ICU answered its questions, and they were not the same one:
# 76.1 here, 78.3 there. Two libraries in one suite asking different ICUs is
# fine; not saying so is not.
#
# **From Debian stable rather than from source**, which is where this departs
# from `unicode`'s equivalent. That one builds ICU 78.3 from the Consortium's
# tarball because 78.3 exists only in sid, and notes/suite/CONTAINERS.md
# section 2.6 is right that an exact apt pin into sid expires sooner than a
# stable one does. This gate wants 76.1, which *is* Debian 13's libicu-dev, and
# Debian 13 is stable - so the cheap option and the correct one coincide here
# in a way they did not there.
#
# What is pinned:
#
#   - the base, by digest, which fixes the apt suite and therefore the ICU
#     package version inside it
#   - the run-time version check in oracle_env.py, which is the real guarantee,
#     because two builds of one Dockerfile are not two copies of one image
#
# Why 76.1 and not the newest: it is what the seven known divergences in
# ldml_diff.py were measured against. Raising it is new CLDR data and therefore
# new answers, and those arrive as divergences needing triage - a separate
# change with its own commit, not a side effect of containerising.

FROM docker.io/library/debian@sha256:7792b1f7702a86946cd518db72b6a407302c3e9bc1635634368b878189e8221c

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      g++ libc6-dev libicu-dev pkg-config \
 && rm -rf /var/lib/apt/lists/*

# A version probe compiled into the image, so oracle_env.py can ask what this
# reference is without first compiling the driver.
#
# It reports U_ICU_VERSION from the headers the driver itself will be compiled
# against, which is the right source for the claim: `icuinfo` prints the same
# number wrapped in XML, and parsing that would put a sed script between the
# pin check and the fact it is checking.
RUN printf '%s\n' \
      '#include <unicode/uchar.h>' \
      '#include <unicode/uvernum.h>' \
      '#include <unicode/uversion.h>' \
      '#include <stdio.h>' \
      'int main(void) {' \
      '  printf("ICU %s, Unicode %s", U_ICU_VERSION, U_UNICODE_VERSION);' \
      '  puts("");' \
      '  return 0;' \
      '}' \
    > /tmp/icu-version.c \
 && gcc -O1 -o /usr/local/bin/icu-version /tmp/icu-version.c -licuuc \
 && rm /tmp/icu-version.c \
 && /usr/local/bin/icu-version

# The root locale is what the differential asks in, and a formatter that reads
# the environment's locale would answer a different question on a machine whose
# LANG is set. notes/suite/CONTAINERS.md records this costing regex 1,855 rows
# of false disagreement when its vim oracle inherited $LANG.
ENV LANG=C.UTF-8 LC_ALL=C.UTF-8

ENTRYPOINT []
CMD ["icu-version"]
