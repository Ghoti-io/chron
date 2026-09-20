/**
 * @file
 *
 * The JSON-Schema-Test-Suite's optional `format` vectors, run through this
 * library's grammars.
 *
 * This is milestone M1 of design.md section 16: the check that says `text`
 * can replace its timestamp side-car and pass the JSON Schema `format`
 * vectors.
 *
 * Two things about how it judges a case are worth stating, because both are
 * decisions rather than mechanics:
 *
 * - **A `format` check asks whether text matches a grammar, not whether the
 *   value fits in an integer.** `P9999999999999999999999D` is a well-formed
 *   RFC 3339 duration that no `int64_t` can hold, and the suite says it is
 *   valid. So GCHRON_ERR_RANGE counts as a pass here, and only
 *   GCHRON_ERR_FORMAT, GCHRON_ERR_UNSUPPORTED and GCHRON_ERR_LIMIT count as
 *   a rejection. A caller wanting the *value* checks the result code and gets
 *   a different answer, which is the point of the two codes being different.
 *
 * - **A missing vector file fails rather than skips.** A gate that turns a
 *   broken harness into a green run is not measuring anything (design.md
 *   section 12.3), and the vectors are committed precisely so that a run
 *   never needs the network or the oracle.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

/** One case out of a vector file. */
struct Vector {
  bool valid;
  std::string text;
  std::string description;
  int line;
};

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

/** Read a committed vector file, or fail the test saying which one. */
std::vector<Vector> load(const std::string & name) {
  std::vector<Vector> cases;
  std::string path = gchrontest::data_dir() + "/vectors/parse/" + name;
  std::ifstream in(path);
  // Not a skip. The vectors are committed so that a run never needs the
  // oracle; a run that cannot find them has a broken harness, not no work.
  EXPECT_TRUE(in.is_open())
      << "missing vector file: " << path
      << " (run tools/corpus/fetch.sh then `make vectors`)";

  std::string line;
  int number = 0;
  while (std::getline(in, line)) {
    ++number;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    size_t first = line.find('\t');
    size_t second = line.find('\t', first + 1);
    if (first == std::string::npos || second == std::string::npos) {
      ADD_FAILURE() << path << ":" << number << ": malformed vector";
      continue;
    }
    Vector v;
    v.valid = line.substr(0, first) == "valid";
    if (!unescape(line.substr(first + 1, second - first - 1), &v.text)) {
      ADD_FAILURE() << path << ":" << number << ": bad escape";
      continue;
    }
    v.description = line.substr(second + 1);
    v.line = number;
    cases.push_back(v);
  }
  return cases;
}

/** Which of the library's grammars a `format` name maps to. */
enum class Grammar { DateTime, Date, Time, Duration };

/**
 * Whether the library accepts this text as that format.
 *
 * GCHRON_ERR_RANGE is an acceptance: the text is the grammar, and no integer
 * holds the value. Everything else the parsers return for bad text -
 * GCHRON_ERR_FORMAT, GCHRON_ERR_UNSUPPORTED, GCHRON_ERR_LIMIT - is a
 * rejection.
 */
bool accepts(Grammar grammar, const std::string & text) {
  GCHRON_ParseOptions opts;
  gchron_parse_options_json_schema(&opts);

  GCHRON_Result result = GCHRON_ERR_INTERNAL;
  switch (grammar) {
    case Grammar::DateTime: {
      GCHRON_OffsetDateTime out;
      result = gchron_parse_rfc3339_date_time(text.data(), text.size(), &opts,
          &out, nullptr, nullptr);
      break;
    }
    case Grammar::Date: {
      GCHRON_Date out;
      result = gchron_parse_rfc3339_full_date(text.data(), text.size(), &opts,
          &out, nullptr, nullptr);
      break;
    }
    case Grammar::Time: {
      GCHRON_OffsetTime out;
      result = gchron_parse_rfc3339_full_time(text.data(), text.size(), &opts,
          &out, nullptr, nullptr);
      break;
    }
    case Grammar::Duration: {
      GCHRON_Duration out;
      result = gchron_parse_rfc3339_duration(text.data(), text.size(), &opts,
          &out, nullptr, nullptr);
      break;
    }
  }
  return result == GCHRON_OK || result == GCHRON_ERR_RANGE;
}

/** Run one file's worth of vectors, and say how many were checked. */
void run(const std::string & file, Grammar grammar, size_t expected_cases) {
  std::vector<Vector> cases = load(file);
  ASSERT_EQ(expected_cases, cases.size())
      << file << ": the committed corpus changed size. Regenerate it with "
      << "`make vectors` and update the count here, so that a corpus that "
      << "shrank cannot pass by checking less.";

  for (const Vector & v : cases) {
    EXPECT_EQ(v.valid, accepts(grammar, v.text))
        << file << ":" << v.line << ": " << v.description;
  }
}

} // namespace

TEST(JsonSchemaFormat, DateTime) {
  run("jsonschema_date_time.vec", Grammar::DateTime, 37);
}

TEST(JsonSchemaFormat, Date) {
  run("jsonschema_date.vec", Grammar::Date, 75);
}

TEST(JsonSchemaFormat, Time) {
  run("jsonschema_time.vec", Grammar::Time, 49);
}

TEST(JsonSchemaFormat, Duration) {
  run("jsonschema_duration.vec", Grammar::Duration, 46);
}

// design.md section 12.3: a conformance runner that cannot be made to fail is
// not measuring anything. These check the two ways this one could silently
// stop measuring - an expectation that no longer matches, and an escape the
// reader mangles.
TEST(JsonSchemaFormat, TheRunnerFailsWhenAnExpectationIsWrong) {
  EXPECT_TRUE(accepts(Grammar::Date, "1963-06-19"));
  EXPECT_FALSE(accepts(Grammar::Date, "1963-06-31"));
  // If `accepts` ever returned a constant, one of the two above would break.
}

TEST(JsonSchemaFormat, TheEscapeReaderRoundTripsWhatTheGeneratorWrites) {
  std::string out;

  ASSERT_TRUE(unescape("2020-01-01", &out));
  EXPECT_EQ("2020-01-01", out);

  // The suite's trailing newline case, its NUL case, and its Bengali digits.
  ASSERT_TRUE(unescape("P1D\\x0a", &out));
  EXPECT_EQ(std::string("P1D\n"), out);
  ASSERT_TRUE(unescape("2020-01-01\\x00", &out));
  EXPECT_EQ(std::string("2020-01-01\0", 11), out);
  ASSERT_TRUE(unescape("1963-06-1\\xe0\\xa7\\xaa", &out));
  EXPECT_EQ("1963-06-1\xE0\xA7\xAA", out);
  ASSERT_TRUE(unescape("a\\\\b", &out));
  EXPECT_EQ("a\\b", out);

  // A mangled escape is reported rather than read as something else.
  EXPECT_FALSE(unescape("\\", &out));
  EXPECT_FALSE(unescape("\\x0", &out));
  EXPECT_FALSE(unescape("\\q00", &out));
  EXPECT_FALSE(unescape("\\xzz", &out));
}

// The rule that lets a `format` check and a value conversion give different
// answers about the same text, stated here so that changing it breaks a test.
TEST(JsonSchemaFormat, ARangeErrorIsAFormatPassAndAValueFailure) {
  const char * text = "P999999999999999999999999999999D";
  GCHRON_ParseOptions opts;
  GCHRON_Duration out;
  gchron_parse_options_json_schema(&opts);

  EXPECT_TRUE(accepts(Grammar::Duration, text));
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_parse_rfc3339_duration(text, std::strlen(text), &opts, &out,
          nullptr, nullptr));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
