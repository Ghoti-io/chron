/**
 * @file
 *
 * The pattern compiler and the formatter, on arbitrary text.
 *
 * design.md section 1.2's fourth untrusted input: a format pattern is usually
 * written by the programmer, but in `ctang` it comes from a template and a
 * template may come from a user. The compiler is bounded for the same reason
 * `regex` bounds its pattern parser, and this is what checks that the bound
 * holds.
 *
 * The harness asserts the property the output contract promises, not just
 * absence of a crash: **a format that compiles reports a maximum length, and
 * the formatter never exceeds it**. A bound that is not a bound turns every
 * caller's fixed buffer into an overflow, and it is the kind of arithmetic
 * error a differential would never look for.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <ghoti.io/chron/chron.h>
#include <ghoti.io/chron/format.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) {
    return 0;
  }

  const uint8_t options = data[0];
  const char * pattern = reinterpret_cast<const char *>(data + 1);
  const size_t pattern_len = size - 1;

  GCHRON_Limits limits;
  gchron_limits_default(&limits);
  // The options byte drives the limits, so both sides of the ERR_LIMIT
  // branches are reachable rather than only the default.
  if (options & 0x01) {
    limits.max_format_length = (size_t)(options >> 4) * 8 + 1;
  }
  if (options & 0x02) {
    limits.max_format_items = (size_t)(options >> 5) + 1;
  }

  GCHRON_Format * format = nullptr;
  GCHRON_Error err;
  if (gchron_format_compile(pattern, pattern_len,
          (options & 0x04) ? GCHRON_FORMAT_STRFTIME : GCHRON_FORMAT_LDML,
          &limits, nullptr, &format, &err) != GCHRON_OK) {
    return 0;
  }

  size_t bound = 0;
  if (gchron_format_max_length(format, &bound) != GCHRON_OK) {
    gchron_format_destroy(format);
    return 0;
  }

  // A spread of values, including the ones whose fields are widest: a
  // nine-digit negative year, a full nanosecond fraction, and a
  // second-resolution offset.
  static const struct {
    int32_t year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int32_t nsec;
    int32_t offset;
  } kValues[] = {
    { 2026, 9, 20, 15, 30, 45, 123456789, 2 * 3600 },
    { -999999999, 1, 1, 0, 0, 0, 0, -86399 },
    { 999999999, 12, 31, 23, 59, 59, 999999999, 86399 },
    { 1, 1, 1, 0, 0, 0, 0, 0 },
    { 0, 2, 29, 12, 0, 0, 1, 1171 },
  };

  for (const auto & v : kValues) {
    GCHRON_DateTime civil{};
    if (gchron_date_create(v.year, v.month, v.day, &civil.date) != GCHRON_OK
        || gchron_time_create(v.hour, v.minute, v.second, v.nsec, &civil.time)
            != GCHRON_OK) {
      continue;
    }
    GCHRON_OffsetDateTime odt{};
    if (gchron_offset_create(&civil, v.offset, false, &odt) != GCHRON_OK) {
      continue;
    }

    // Ask for the length first, with no buffer at all.
    size_t needed = 0;
    GCHRON_Result result =
        gchron_format_offset(format, &odt, nullptr, nullptr, 0, &needed);
    if (result != GCHRON_OK && result != GCHRON_ERR_LIMIT) {
      continue; /* a letter this value cannot supply */
    }

    // The property: the declared bound really bounds the output.
    assert(needed + 1 <= bound);

    std::vector<char> buffer(bound + 8, '\xAB');
    size_t written = 0;
    if (gchron_format_offset(format, &odt, nullptr, buffer.data(), bound,
            &written) == GCHRON_OK) {
      assert(written == needed);
      assert(buffer[written] == '\0');
      // And nothing was written past the terminator it said it wrote.
      for (size_t i = written + 1; i < buffer.size(); ++i) {
        assert(buffer[i] == '\xAB');
      }
      if (std::memchr(pattern, '\0', pattern_len) == nullptr) {
        /*
         * `strlen` only measures the output when the *pattern* had no NUL in
         * it. A pattern is a pointer and a length, like every other piece of
         * text this library takes, so it may contain one - and then the
         * output contains one too and stops `strlen` early. The harness
         * found exactly that, and the library was right: `written` is the
         * length and the terminator is where it says.
         */
        assert(written == std::strlen(buffer.data()));
      }
    }
  }

  gchron_format_destroy(format);
  return 0;
}
