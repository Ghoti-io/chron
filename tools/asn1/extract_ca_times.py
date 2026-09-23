#!/usr/bin/env python3
"""Regenerate tests/data/asn1/ca_bundle_times.vec from the system CA bundle.

The expected instant on each line is OpenSSL's reading of the ASN.1 string,
not this library's. That is the whole point: a corpus generated from our own
writer would agree with our own reader about any shared mistake, and a
two-digit year is exactly where to make one.

Needs `openssl` on PATH and a PEM bundle; Debian's is the default.

    tools/asn1/extract_ca_times.py [bundle.pem] > tests/data/asn1/ca_bundle_times.vec
"""

import calendar
import datetime
import re
import subprocess
import sys

DEFAULT_BUNDLE = "/etc/ssl/certs/ca-certificates.crt"


def main(argv):
    bundle = argv[1] if len(argv) > 1 else DEFAULT_BUNDLE
    try:
        raw = open(bundle).read()
    except OSError as exc:
        sys.stderr.write("cannot read %s: %s\n" % (bundle, exc))
        return 1

    certs = re.findall(
        r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", raw,
        re.S)
    if not certs:
        sys.stderr.write("no PEM certificates in %s\n" % bundle)
        return 1

    pairs = {}
    for cert in certs:
        parsed = subprocess.run(["openssl", "asn1parse"], input=cert,
                                capture_output=True, text=True).stdout
        dates = subprocess.run(["openssl", "x509", "-noout", "-dates"],
                               input=cert, capture_output=True,
                               text=True).stdout
        times = [m.group(2) for line in parsed.splitlines()
                 for m in [re.search(r"\b(UTCTIME|GENERALIZEDTIME)\s+:(\S+)",
                                     line)] if m]
        human = re.findall(r"not(?:Before|After)=(.+)", dates)
        if len(times) < 2 or len(human) < 2:
            continue
        # The first two of each are notBefore and notAfter, in that order.
        for encoded, rendered in zip(times[:2], human[:2]):
            try:
                when = datetime.datetime.strptime(rendered.strip(),
                                                  "%b %d %H:%M:%S %Y %Z")
            except ValueError:
                continue
            pairs[encoded] = calendar.timegm(when.timetuple())

    print("# ASN.1 time strings taken from real certificates, with the "
          "instant each one")
    print("# names as OpenSSL decodes it.")
    print("#")
    print("# Provenance, and why it matters: these are the notBefore and "
          "notAfter fields")
    print("# of the certificates in %s, read out with" % bundle)
    print("# `openssl asn1parse` and paired with `openssl x509 -noout "
          "-dates`. The")
    print("# expected value on each line is OpenSSL's reading of that "
          "string, not this")
    print("# library's - a corpus generated from our own writer would agree "
          "with our own")
    print("# reader about a shared mistake, and the two-digit year is "
          "exactly the place")
    print("# to make one.")
    print("#")
    print("# Regenerate with tools/asn1/extract_ca_times.py.")
    print("#")
    print("# <ASN.1 string> <Unix seconds, UTC>")
    for encoded, epoch in sorted(pairs.items()):
        print("%s %d" % (encoded, epoch))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
