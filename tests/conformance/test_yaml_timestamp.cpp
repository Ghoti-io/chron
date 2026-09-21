/**
 * @file
 *
 * The YAML 1.1 `!!timestamp` grammar, against PyYAML.
 *
 * PyYAML is the reference implementation of YAML 1.1 - the version that has a
 * timestamp type at all - and the parser most existing 1.1 documents were
 * written against. `tools/oracle/yaml_timestamp.py` runs a corpus through it
 * and commits the verdicts; this reads them back, so a run needs neither
 * Python nor the network.
 *
 * The vector file keeps two answers in separate columns, and this file states
 * the one rule that turns them into an expectation:
 *
 * - **`nomatch`** - PyYAML's resolver regex does not match, so the scalar is
 *   a `!!str` and not a timestamp at all. This library must refuse it. This
 *   is the column that matters most: a parser that accepts what the regex
 *   rejects does not merely mis-parse a value, it resolves a *different tag*
 *   than every other YAML 1.1 reader, and the document then means something
 *   else.
 * - **`ok`** - it matches and PyYAML built a value. This library must accept
 *   it and agree field for field.
 * - **`badvalue`** - it matches and PyYAML refused it on the value's own
 *   terms: month 13, the 30th of February, hour 24. This library must refuse
 *   it too, because the regex does not check field ranges and a parser that
 *   returned a `format` success alongside an impossible date would be
 *   disagreeing with its own output.
 * - **`pylimit`** - it matches and only Python's `datetime` could not hold
 *   it: a `:60` second. This library **accepts** these, and the count is
 *   printed rather than the cases being dropped, because a bucket nobody
 *   counts is a bucket that can quietly grow.
 *
 * Every deviation from PyYAML is named in DEVIATIONS below, with the reason.
 * A case that lands in neither the rule nor that list fails, so a new
 * disagreement cannot arrive unnoticed.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

/** One case out of the vector file. */
struct Vector {
  std::string verdict;
  std::string text;
  std::string fields;
  std::string description;
  int line;
};

/**
 * The cases where this library and PyYAML disagree on purpose.
 *
 * Both are about PyYAML accepting something and this library refusing it, and
 * both are documented in documentation/text-formats.md. Spelled as exact
 * input text so that a deviation cannot silently widen to cover a case nobody
 * considered.
 */
struct Deviation {
  const char * text;
  const char * why;
};

const Deviation DEVIATIONS[] = {
  { "2001-12-14T21:59:43+05:60",
    "an offset minute of 60. The regex permits it and PyYAML carries the "
    "sixtieth minute into the hour, turning +05:60 into +06:00 - a value the "
    "document did not write. This library refuses the field, as it refuses "
    "the 30th of February, rather than silently correcting it." },
};

/** Whether a case is a named deviation, and why. */
const char * deviation_reason(const std::string & text) {
  for (const Deviation & d : DEVIATIONS) {
    if (text == d.text) {
      return d.why;
    }
  }
  return nullptr;
}

/** Reverse the generator's escaping: a printable byte, or `\xHH`. */
bool unescape(const std::string & in, std::string * out) {
  out->clear();
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] != '\\') {
      out->push_back(in[i]);
      continue;
    }
    if (i + 1 >= in.size()) {
      return false;
    }
    if (in[i + 1] == '\\') {
      out->push_back('\\');
      i += 1;
      continue;
    }
    if (in[i + 1] != 'x' || i + 3 >= in.size()) {
      return false;
    }
    char digits[3] = { in[i + 2], in[i + 3], '\0' };
    char * end = nullptr;
    long value = std::strtol(digits, &end, 16);
    if (end != digits + 2) {
      return false;
    }
    out->push_back(static_cast<char>(value));
    i += 3;
  }
  return true;
}

/** Split a line on tabs. */
std::vector<std::string> split(const std::string & line, char sep) {
  std::vector<std::string> parts;
  size_t start = 0;
  for (;;) {
    size_t at = line.find(sep, start);
    if (at == std::string::npos) {
      parts.push_back(line.substr(start));
      return parts;
    }
    parts.push_back(line.substr(start, at - start));
    start = at + 1;
  }
}

/** Read the committed vector file, or fail the test saying which one. */
std::vector<Vector> load() {
  std::vector<Vector> cases;
  std::string path =
      gchrontest::data_dir() + "/vectors/parse/yaml_timestamp.vec";
  std::ifstream in(path);
  /*
   * Not a skip. The vectors are committed so that a run never needs PyYAML;
   * a run that cannot find them has a broken harness, not no work to do
   * (design.md section 12.3).
   */
  EXPECT_TRUE(in.is_open())
      << "missing vector file: " << path
      << " (regenerate with tools/oracle/yaml_timestamp.py)";

  std::string line;
  int number = 0;
  while (std::getline(in, line)) {
    ++number;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::vector<std::string> parts = split(line, '\t');
    if (parts.size() != 4) {
      ADD_FAILURE() << path << ":" << number << ": malformed vector";
      continue;
    }
    Vector v;
    v.verdict = parts[0];
    if (!unescape(parts[1], &v.text)) {
      ADD_FAILURE() << path << ":" << number << ": bad escape";
      continue;
    }
    v.fields = parts[2];
    v.description = parts[3];
    v.line = number;
    cases.push_back(v);
  }
  return cases;
}

/**
 * One of PyYAML's named groups, or an empty string when it was absent.
 *
 * The generator writes `~` for an absent group, not `-`: `-` is also the
 * value of `tz_sign` on every negative offset, and the first spelling of this
 * file read every `-05:00` as a *missing* sign and so as `+05:00`.
 */
std::string field(const std::string & fields, size_t index) {
  std::vector<std::string> parts = split(fields, '|');
  if (index >= parts.size() || parts[index] == "~") {
    return std::string();
  }
  return parts[index];
}

/* The order tools/oracle/yaml_timestamp.py writes the groups in. */
enum Field {
  F_YEAR = 0, F_MONTH, F_DAY, F_HOUR, F_MINUTE, F_SECOND, F_FRACTION,
  F_TZ, F_TZ_SIGN, F_TZ_HOUR, F_TZ_MINUTE
};

/** The nanoseconds a fractional-digit string stands for, truncated at nine. */
int32_t nanoseconds(const std::string & digits) {
  int32_t value = 0;
  int32_t scale = 100000000;
  for (size_t i = 0; i < digits.size() && i < 9; ++i) {
    value += static_cast<int32_t>(digits[i] - '0') * scale;
    scale /= 10;
  }
  return value;
}

TEST(YamlTimestamp, MatchesPyYaml) {
  std::vector<Vector> cases = load();
  ASSERT_FALSE(cases.empty()) << "the vector file carried no cases";

  int accepted = 0;
  int rejected = 0;
  int leap = 0;
  int deviations = 0;

  for (const Vector & v : cases) {
    GCHRON_YamlValue value;
    GCHRON_ParseInfo info;
    GCHRON_Error err;
    GCHRON_Result result = gchron_parse_yaml_timestamp(v.text.data(),
        v.text.size(), nullptr, &value, &info, &err);
    const std::string where = "line " + std::to_string(v.line) + ": \""
        + v.text + "\" (" + v.description + ")";

    const char * why = deviation_reason(v.text);
    if (why != nullptr) {
      ++deviations;
      EXPECT_NE(result, GCHRON_OK)
          << where << "\nis a documented deviation and should be refused: "
          << why;
      continue;
    }

    if (v.verdict == "nomatch" || v.verdict == "badvalue") {
      ++rejected;
      EXPECT_NE(result, GCHRON_OK)
          << where << "\nPyYAML says " << v.verdict
          << "; this library accepted it";
      continue;
    }

    ASSERT_TRUE(v.verdict == "ok" || v.verdict == "pylimit")
        << where << "\nunknown verdict \"" << v.verdict << "\"";
    ++accepted;
    ASSERT_EQ(result, GCHRON_OK)
        << where << "\nPyYAML accepts it; this library said "
        << gchron_result_string(result) << " - "
        << gchron_diag_string(err.diag);

    /* The date, which every shape carries. */
    EXPECT_EQ(value.civil.date.year,
        std::atoi(field(v.fields, F_YEAR).c_str())) << where;
    EXPECT_EQ(static_cast<int>(value.civil.date.month),
        std::atoi(field(v.fields, F_MONTH).c_str())) << where;
    EXPECT_EQ(static_cast<int>(value.civil.date.day),
        std::atoi(field(v.fields, F_DAY).c_str())) << where;

    const std::string hour = field(v.fields, F_HOUR);
    if (hour.empty()) {
      EXPECT_EQ(value.kind, GCHRON_YAML_DATE) << where;
      continue;
    }

    EXPECT_EQ(static_cast<int>(value.civil.time.hour), std::atoi(hour.c_str()))
        << where;
    EXPECT_EQ(static_cast<int>(value.civil.time.minute),
        std::atoi(field(v.fields, F_MINUTE).c_str())) << where;

    const int second = std::atoi(field(v.fields, F_SECOND).c_str());
    if (second == 60) {
      /*
       * The one reading Python has no room for. Under this grammar's preset -
       * GCHRON_LEAP_CLAMP - the value holds `:59` of the same minute, where
       * the Linux kernel puts the repeated second, and the flag is the
       * evidence that the text said otherwise.
       */
      ++leap;
      EXPECT_EQ(v.verdict, "pylimit") << where;
      EXPECT_EQ(static_cast<int>(value.civil.time.second), 59) << where;
      EXPECT_TRUE(info.leap_second) << where;
    }
    else {
      EXPECT_EQ(static_cast<int>(value.civil.time.second), second) << where;
      EXPECT_FALSE(info.leap_second) << where;
    }

    const std::string fraction = field(v.fields, F_FRACTION);
    EXPECT_EQ(value.civil.time.nsec, nanoseconds(fraction)) << where;
    EXPECT_EQ(static_cast<int>(info.fraction_digits),
        static_cast<int>(fraction.size() > 255 ? 255 : fraction.size()))
        << where;
    EXPECT_EQ(info.fraction_truncated, fraction.size() > 9) << where;

    const std::string tz = field(v.fields, F_TZ);
    if (tz.empty()) {
      EXPECT_EQ(value.kind, GCHRON_YAML_DATE_TIME) << where;
      EXPECT_EQ(value.offset_sec, 0) << where;
      EXPECT_FALSE(value.offset_is_z) << where;
      continue;
    }

    EXPECT_EQ(value.kind, GCHRON_YAML_OFFSET_DATE_TIME) << where;
    if (tz == "Z") {
      EXPECT_TRUE(value.offset_is_z) << where;
      EXPECT_EQ(value.offset_sec, 0) << where;
      EXPECT_FALSE(value.offset_unknown) << where;
      continue;
    }

    EXPECT_FALSE(value.offset_is_z) << where;
    const std::string sign = field(v.fields, F_TZ_SIGN);
    const int tz_hour = std::atoi(field(v.fields, F_TZ_HOUR).c_str());
    const std::string tz_minute_text = field(v.fields, F_TZ_MINUTE);
    const int tz_minute =
        tz_minute_text.empty() ? 0 : std::atoi(tz_minute_text.c_str());
    int32_t expected = static_cast<int32_t>(tz_hour * 3600 + tz_minute * 60);
    if (sign == "-") {
      expected = -expected;
    }
    EXPECT_EQ(value.offset_sec, expected) << where;
    /* RFC 3339 section 4.3's *unknown*, which YAML inherits by spelling it
       the same way. PyYAML folds it into +00:00 and loses the statement. */
    EXPECT_EQ(value.offset_unknown, sign == "-" && expected == 0) << where;
  }

  /*
   * Printed, not assumed. A differential whose buckets nobody reports is one
   * that can stop exercising a whole column without the run changing colour
   * (design.md section 12.3).
   */
  std::printf("[          ] %d accepted, %d refused, %d leap seconds Python "
      "cannot hold, %d documented deviations\n", accepted, rejected, leap,
      deviations);
  EXPECT_GT(accepted, 0);
  EXPECT_GT(rejected, 0);
  EXPECT_GT(leap, 0);
}

/**
 * Every case PyYAML accepts, written back out and read again.
 *
 * The writer emits the canonical spelling rather than the input's, so the
 * text is allowed to change; the *value* is not. This is the identity
 * design.md section 8.6 asks of every grammar, and the one loss it permits is
 * the leap second, which parse.h documents.
 */
TEST(YamlTimestamp, RoundTrips) {
  std::vector<Vector> cases = load();
  int checked = 0;

  for (const Vector & v : cases) {
    if (v.verdict != "ok" && v.verdict != "pylimit") {
      continue;
    }
    if (deviation_reason(v.text) != nullptr) {
      continue;
    }
    GCHRON_YamlValue first;
    if (gchron_parse_yaml_timestamp(v.text.data(), v.text.size(), nullptr,
            &first, nullptr, nullptr) != GCHRON_OK) {
      continue;  /* MatchesPyYaml has already failed for this one. */
    }

    char buf[GCHRON_YAML_TIMESTAMP_MAX];
    size_t length = 0;
    ASSERT_EQ(gchron_write_yaml_timestamp(&first, nullptr, buf, sizeof(buf),
            &length), GCHRON_OK) << v.text;
    ASSERT_EQ(std::strlen(buf), length) << v.text;

    GCHRON_YamlValue again;
    ASSERT_EQ(gchron_parse_yaml_timestamp(buf, length, nullptr, &again,
            nullptr, nullptr), GCHRON_OK)
        << "wrote \"" << buf << "\" from \"" << v.text
        << "\" and could not read it back";
    EXPECT_TRUE(gchron_yaml_timestamp_identical(&first, &again))
        << "\"" << v.text << "\" -> \"" << buf << "\" -> a different value";
    ++checked;
  }

  std::printf("[          ] %d values round-tripped\n", checked);
  EXPECT_GT(checked, 0);
}

}  // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
