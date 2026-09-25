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
# **78.3, raised from 76.1 on 2026-09-25, and from source rather than from
# Debian.** 76.1 is Debian 13's libicu-dev and an apt install of it was the
# cheap and correct option while that was the pin. It stops being available the
# moment the pin moves: 78.3 is in sid, and notes/suite/CONTAINERS.md section
# 2.6 reasons out why an exact apt pin into sid is the wrong instrument for a
# gate whose disagreement means a defect - sid moves continuously, so such a pin
# expires, while a source release has no expiry. So this now builds what
# `unicode`'s equivalent builds, the same way.
#
# 78.3 is also the newest ICU4C release there is (2026-03-17; the later 79.x
# tags on that repository are icu4x, which is the Rust project and not this
# library's reference), and it is what `unicode` pins - so raising to it also
# ends the suite asking one question of two CLDR versions.
#
# What is pinned, per section 2.6's two-part rule for a built-here image:
#
#   - the base, by digest
#   - the source tarball, by the **SHA-512 the Unicode Consortium publishes**
#     for it in SHASUM512.txt beside the release. Not the git tag: a tag is a
#     name and this is the bytes. Checked against that file rather than copied
#     from the sibling repository that already had it, because a figure taken on
#     trust is not a figure.
#   - and the run-time version check in oracle_env.py, which is the real
#     guarantee, because two builds of one Dockerfile are not two copies of one
#     image.

FROM docker.io/library/debian@sha256:7792b1f7702a86946cd518db72b6a407302c3e9bc1635634368b878189e8221c

ARG ICU_VERSION=78.3
ARG ICU_TARBALL=icu4c-78.3-sources.tgz
ARG ICU_SHA512=04a49455e1489030c520a4bfd2664fa2171e7938d08f2acdbbcb1fda976639fd8b1f0704f2eec89ba59a7b6d118ceaab6ec5a096e40d9085a0895d91ce225245

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl g++ make pkg-config libc6-dev python3 \
 && rm -rf /var/lib/apt/lists/*

RUN cd /tmp \
 && curl -fsSLO "https://github.com/unicode-org/icu/releases/download/release-${ICU_VERSION}/${ICU_TARBALL}" \
 && printf '%s  %s\n' "${ICU_SHA512}" "${ICU_TARBALL}" | sha512sum -c - \
 && tar xzf "${ICU_TARBALL}" \
 && cd icu/source \
 && ./runConfigureICU Linux \
      --prefix=/usr/local \
      --disable-samples --disable-tests --disable-extras --disable-layoutex \
 && make -j"$(nproc)" \
 && make install \
 && ldconfig \
 && cd /tmp && rm -rf /tmp/icu /tmp/"${ICU_TARBALL}"

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
#
# LD_LIBRARY_PATH because this ICU is in /usr/local rather than where the
# distribution puts one, and the driver is compiled at run time against the
# mounted source: the link succeeds either way and without this the binary it
# produces would not start.
ENV LANG=C.UTF-8 LC_ALL=C.UTF-8
ENV LD_LIBRARY_PATH=/usr/local/lib

ENTRYPOINT []
CMD ["icu-version"]
