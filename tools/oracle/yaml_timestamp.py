#!/usr/bin/env python3
"""Generate the YAML 1.1 `!!timestamp` vectors from PyYAML.

PyYAML is the oracle here, and it is the right one for two reasons.  It is the
reference implementation of YAML 1.1 - the version that *has* a timestamp type
at all; 1.2's core schema dropped it - and it is the parser most existing 1.1
documents were written against, so its reading of a scalar is the reading those
documents assume.  It is also a corpus this library did not write, which is the
property design.md section 12 asks of an oracle and the one a corpus grown from
one's own fixes lacks.

Two different questions are asked of it, and the vector file keeps the answers
in separate columns rather than folding them together:

- **Does the scalar resolve as a timestamp at all?**  That is PyYAML's implicit
  resolver regex, copied out of the installed module rather than retyped, and
  it is the definition of the type: a scalar the regex does not match is a
  `!!str`, and the document means something different.

- **Can PyYAML then build a value from it?**  That is its constructor, and it
  answers a narrower question, because Python's `datetime` cannot hold
  everything the regex matches.  A `:60` second and a fraction finer than a
  microsecond are refused by Python and accepted by this library, and folding
  that into a single verdict would record a limitation of Python as a fact
  about YAML.

`tests/conformance/test_yaml_timestamp.cpp` states the one rule that turns the
two columns into an expectation, and counts every case it puts in each bucket.

Usage:  tools/oracle/yaml_timestamp.py [--out <dir>]

Copyright 2026 by Corey Pennycuff
"""

import argparse
import datetime
import pathlib
import sys

try:
    import yaml
    import yaml.resolver
except ImportError:  # pragma: no cover
    sys.exit("PyYAML is not installed: pip install PyYAML")


def timestamp_regex():
    """PyYAML's own implicit resolver for the timestamp tag.

    Read out of the installed module, never retyped.  A copy in this file
    would be a second implementation that could drift from the one the
    documents of the world were actually written against, and the drift would
    be invisible - the tests would still pass, against the wrong oracle.
    """
    for first, entries in yaml.resolver.Resolver.yaml_implicit_resolvers.items():
        for tag, regexp in entries:
            if tag == "tag:yaml.org,2002:timestamp":
                return regexp
    raise SystemExit("PyYAML has no timestamp resolver; the oracle is gone")


# Reasons PyYAML's constructor refuses something its own regex matched, that
# are limits of Python's `datetime` rather than statements about YAML.  Kept as
# an explicit list so that a new refusal shows up as an unknown reason and
# fails the generator, rather than being quietly classed as a Python limit.
def is_python_limit(text, message):
    """Whether a refusal is Python's limit rather than a fact about YAML.

    One case: a `:60` second. Python's `datetime` has no room for a leap
    second, and refuses the one reading a real clock in a real minute
    produced. `:61` raises the identical message and is *not* in this
    category - it is out of range in any calendar - so the message alone
    cannot classify it and the second field is read to decide.
    """
    if "second must be in 0..59" not in message:
        return False
    match = yaml.constructor.SafeConstructor.timestamp_regexp.match(text)
    return match is not None and match.group("second") == "60"


# The order the field columns are written in.
FIELDS = ("year", "month", "day", "hour", "minute", "second", "fraction",
        "tz", "tz_sign", "tz_hour", "tz_minute")

# What stands in a field column for a group the text did not carry.
#
# Not `-`, which was the first spelling and is also the value of `tz_sign` on
# every negative offset: the reader treated every `-05:00` as an *absent* sign
# and read the offset back the wrong way round. A sentinel has to be a
# character no field can hold.
ABSENT = "~"


def construct(text):
    """Run PyYAML's own timestamp constructor over one scalar.

    Called directly rather than by loading a one-line document, because a
    document puts YAML's *scanner* in the way: a tab inside a plain scalar is
    a document-level error, and routing the corpus through `safe_load` would
    record "YAML forbids a tab here" as "this is not a timestamp". The two are
    different statements and only the second is this file's business.
    """
    node = yaml.nodes.ScalarNode("tag:yaml.org,2002:timestamp", text)
    return yaml.constructor.SafeConstructor().construct_yaml_timestamp(node)


def fields(text):
    """The field values PyYAML reads out of a scalar, as it reads them.

    PyYAML's *constructor* carries a second regular expression with named
    groups, and this borrows it rather than writing a third. The groups are
    what the test compares against, in preference to the `datetime` the
    constructor goes on to build: that object is truncated to microseconds and
    normalises an offset minute of 60 into an extra hour, so comparing against
    it would test Python's arithmetic rather than this library's reading of
    the text.
    """
    match = yaml.constructor.SafeConstructor.timestamp_regexp.match(text)
    if match is None:
        return None
    groups = match.groupdict()
    return "|".join((groups[name] if groups[name] is not None else ABSENT)
            for name in FIELDS)


def build_corpus():
    """Every case, as (text, description) pairs.

    Systematic across each axis the grammar has, rather than a list of things
    that once went wrong: the widths of every field, each separator, each
    fraction shape, each zone spelling, and the boundaries of each range.
    """
    cases = []

    def add(text, description):
        cases.append((text, description))

    # The type repository's own examples.
    add("2001-12-14t21:59:43.10-05:00", "the spec's canonical example")
    add("2001-12-14 21:59:43.10 -5", "the spec's space-separated example")
    add("2001-12-15 2:59:43.10", "the spec's example with no zone")
    add("2002-12-14", "the spec's date-only example")

    # Date-only: the first alternative wants exactly two digits each.
    add("2001-12-14", "a date with two-digit month and day")
    add("2001-12-4", "a date with a one-digit day and no time")
    add("2001-1-14", "a date with a one-digit month and no time")
    add("2001-1-4", "a date with both fields one digit and no time")
    add("201-12-14", "a three-digit year")
    add("20015-12-14", "a five-digit year")

    # The same widths, but carrying a time: the second alternative relaxes them.
    add("2001-12-4T21:59:43Z", "a one-digit day with a time")
    add("2001-1-14T21:59:43Z", "a one-digit month with a time")
    add("2001-1-4T21:59:43Z", "both date fields one digit, with a time")

    # The hour is one or two digits; the minute and second are exactly two.
    add("2001-12-14T2:59:43Z", "a one-digit hour")
    add("2001-12-14T21:5:43Z", "a one-digit minute")
    add("2001-12-14T21:59:4Z", "a one-digit second")
    add("2001-12-14T021:59:43Z", "a three-digit hour")
    add("2001-12-14T21:59Z", "no seconds at all")
    add("2001-12-14T21Z", "no minutes or seconds")

    # Separators.
    add("2001-12-14T21:59:43Z", "an upper-case T")
    add("2001-12-14t21:59:43Z", "a lower-case t")
    add("2001-12-14 21:59:43Z", "a single space")
    add("2001-12-14   21:59:43Z", "three spaces")
    add("2001-12-14\t21:59:43Z", "a tab")
    add("2001-12-14 \t 21:59:43Z", "mixed spaces and tabs")
    add("2001-12-14X21:59:43Z", "some other letter")
    add("2001-12-1421:59:43Z", "no separator at all")

    # Fractions.  YAML's is `*DIGIT` where RFC 3339's is `1*DIGIT`.
    add("2001-12-14T21:59:43.Z", "a decimal point with no digits")
    add("2001-12-14T21:59:43.", "a trailing decimal point and no zone")
    add("2001-12-14T21:59:43.1Z", "one fractional digit")
    add("2001-12-14T21:59:43.123456789Z", "nine fractional digits")
    add("2001-12-14T21:59:43.1234567891Z", "ten fractional digits")
    add("2001-12-14T21:59:43.999999999999999Z", "fifteen fractional digits")
    add("2001-12-14T21:59:43,5Z", "a comma for a decimal point")

    # Zones.
    add("2001-12-14T21:59:43", "no zone")
    add("2001-12-14T21:59:43Z", "Z")
    add("2001-12-14T21:59:43z", "a lower-case z")
    add("2001-12-14T21:59:43+05:00", "a full positive offset")
    add("2001-12-14T21:59:43-05:00", "a full negative offset")
    add("2001-12-14T21:59:43+05", "an offset with no minutes")
    add("2001-12-14T21:59:43-5", "a one-digit offset hour")
    add("2001-12-14T21:59:43+5:30", "a one-digit offset hour with minutes")
    add("2001-12-14T21:59:43+0530", "an offset with no colon")
    add("2001-12-14T21:59:43-00:00", "the unknown-offset spelling")
    add("2001-12-14T21:59:43+00:00", "a written-out zero offset")
    add("2001-12-14T21:59:43+24:00", "an offset hour of 24")
    add("2001-12-14T21:59:43+23:59", "the largest offset")
    add("2001-12-14T21:59:43+05:60", "an offset minute of 60")
    add("2001-12-14T21:59:43+05:5", "a one-digit offset minute")

    # Whitespace around the zone - the deviation this library documents.
    add("2001-12-14T21:59:43 Z", "a space before Z")
    add("2001-12-14T21:59:43   Z", "three spaces before Z")
    add("2001-12-14T21:59:43\tZ", "a tab before Z")
    add("2001-12-14T21:59:43 -05:00", "a space before a numeric offset")
    add("2001-12-14T21:59:43.5 -05:00", "a fraction then a space then an offset")
    add("2001-12-14T21:59:43 ", "trailing whitespace and no zone")
    add("2001-12-14 ", "a date with trailing whitespace")

    # Field ranges.
    add("2001-13-14", "month 13")
    add("2001-00-14", "month 0")
    add("2001-12-32", "day 32")
    add("2001-12-00", "day 0")
    add("2001-02-30", "the 30th of February")
    add("2000-02-29", "the 29th in a leap year divisible by 400")
    add("1900-02-29", "the 29th in a century that is not a leap year")
    add("2004-02-29", "the 29th in an ordinary leap year")
    add("2001-12-14T24:00:00Z", "hour 24")
    add("2001-12-14T23:59:59Z", "the last second of the day")
    add("2001-12-14T21:60:43Z", "minute 60")
    add("2001-12-14T21:59:60Z", "second 60")
    add("2001-12-14T21:59:61Z", "second 61")
    add("1998-12-31T23:59:60Z", "a real leap second")

    # Shapes that are not this grammar at all.
    add("", "the empty string")
    add("not a timestamp", "prose")
    add("2001-12-14T21:59:43Ztrailing", "trailing text after a zone")
    add(" 2001-12-14", "leading whitespace")
    add("2001-12-14T21:59:43+05:00[America/New_York]", "an RFC 9557 annotation")
    add("21:59:43", "a time with no date, which YAML has no production for")

    return cases


def escape(text):
    """A printable ASCII byte stands for itself; everything else is \\xHH."""
    out = []
    for byte in text.encode("utf-8"):
        if byte == 0x5C:
            out.append("\\\\")
        elif 0x20 <= byte < 0x7F:
            out.append(chr(byte))
        else:
            out.append("\\x%02x" % byte)
    return "".join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="tests/data/vectors/parse")
    args = parser.parse_args()

    regexp = timestamp_regex()
    cases = build_corpus()

    lines = []
    counts = {"nomatch": 0, "ok": 0, "pylimit": 0, "badvalue": 0}
    for text, description in cases:
        if regexp.match(text) is None:
            verdict, detail = "nomatch", ABSENT
            counts["nomatch"] += 1
        else:
            detail = fields(text)
            if detail is None:
                raise SystemExit("the resolver matched %r and the constructor"
                        " regex did not; PyYAML's two expressions have"
                        " diverged and this oracle cannot say which is right"
                        % text)
            try:
                built = construct(text)
                if not isinstance(built, (datetime.date, datetime.datetime)):
                    verdict = "badvalue"
                    counts["badvalue"] += 1
                else:
                    verdict = "ok"
                    counts["ok"] += 1
            except Exception as exc:  # noqa: BLE001 - the message is the data
                message = str(exc).strip().splitlines()[-1].strip()
                if is_python_limit(text, message):
                    verdict = "pylimit"
                    counts["pylimit"] += 1
                else:
                    verdict = "badvalue"
                    counts["badvalue"] += 1
        lines.append("%s\t%s\t%s\t%s" % (verdict, escape(text), detail,
                description))

    out_dir = pathlib.Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "yaml_timestamp.vec"
    with path.open("w", encoding="utf-8") as handle:
        handle.write("# YAML 1.1 `!!timestamp`, judged by PyYAML %s.\n"
                % yaml.__version__)
        handle.write("# Generated by tools/oracle/yaml_timestamp.py; do not"
                " edit.\n")
        handle.write("# verdict <TAB> escaped-text <TAB> fields <TAB>"
                " description\n")
        handle.write("# fields are PyYAML's own named groups, in the order\n")
        handle.write("#   %s\n" % "|".join(FIELDS))
        handle.write("#   with `%s` for a group the text did not"
                " carry.\n" % ABSENT)
        handle.write("#   nomatch  - PyYAML's resolver regex does not match;"
                " the scalar is a !!str.\n")
        handle.write("#   ok       - it matches and PyYAML built a value"
                " from it.\n")
        handle.write("#   badvalue - it matches and PyYAML refused it on the"
                " value's own terms.\n")
        handle.write("#   pylimit  - it matches and only Python's datetime"
                " could not hold it\n")
        handle.write("#              (a `:60` second), so this library"
                " accepts it and Python does not.\n")
        for line in lines:
            handle.write(line + "\n")

    print("%s: %d cases (%d nomatch, %d ok, %d badvalue, %d pylimit)"
            % (path, len(lines), counts["nomatch"], counts["ok"],
               counts["badvalue"], counts["pylimit"]))


if __name__ == "__main__":
    main()
