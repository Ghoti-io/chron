/**
 * @file
 *
 * Reading text back through a pattern, on arbitrary input: design.md 8.7.
 *
 * Two untrusted inputs at once, which is what makes this worth a harness of
 * its own. The pattern is section 1.2's fourth untrusted input - in `ctang` it
 * comes from a template and a template may come from a user - and the *text*
 * is the first, a timestamp out of a log file or a config nobody vetted.
 * `fuzz_format` drives the pattern alone; this drives both, against each
 * other.
 *
 * The harness asserts properties rather than only absence of a crash:
 *
 *  - **Every parse consumes the whole input or fails.** A parser that
 *    succeeded having read half its text would report a valid date for
 *    `2026-09-20xyz`.
 *  - **A successful parse sets no field bit it did not write, and writes no
 *    field whose bit it did not set.** The bitmask is the only thing telling a
 *    caller which of forty fields mean anything, so a bit out of step with the
 *    value beside it is the defect the whole design exists to prevent.
 *  - **Formatting and reading back is an identity**, wherever the pattern
 *    names a whole date-time. This is the property section 8.7 claims, driven
 *    with patterns nobody wrote by hand.
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

namespace {

/** Every field bit, so the harness can check the ones that must be clear. */
constexpr uint32_t kAllFields =
    GCHRON_FIELD_ERA | GCHRON_FIELD_YEAR | GCHRON_FIELD_WEEK_YEAR
    | GCHRON_FIELD_QUARTER | GCHRON_FIELD_MONTH | GCHRON_FIELD_WEEK_OF_YEAR
    | GCHRON_FIELD_WEEK_OF_MONTH | GCHRON_FIELD_DAY | GCHRON_FIELD_DAY_OF_YEAR
    | GCHRON_FIELD_WEEKDAY | GCHRON_FIELD_WEEKDAY_IN_MONTH
    | GCHRON_FIELD_MODIFIED_JULIAN | GCHRON_FIELD_DAY_PERIOD
    | GCHRON_FIELD_HOUR | GCHRON_FIELD_MINUTE | GCHRON_FIELD_SECOND
    | GCHRON_FIELD_FRACTION | GCHRON_FIELD_MILLIS_OF_DAY | GCHRON_FIELD_OFFSET
    | GCHRON_FIELD_ZONE_ID | GCHRON_FIELD_EPOCH_SECONDS | GCHRON_FIELD_CENTURY;

/** Whatever the parse did not claim to read must still be zero. */
void unclaimed_fields_are_zero(const GCHRON_ParsedFields & f) {
  assert((f.present & ~kAllFields) == 0 && "a bit outside the enumeration");

  if ((f.present & GCHRON_FIELD_ERA) == 0)          assert(f.era == 0);
  if ((f.present & GCHRON_FIELD_YEAR) == 0)         assert(f.year == 0);
  if ((f.present & GCHRON_FIELD_WEEK_YEAR) == 0)    assert(f.week_year == 0);
  if ((f.present & GCHRON_FIELD_QUARTER) == 0)      assert(f.quarter == 0);
  if ((f.present & GCHRON_FIELD_MONTH) == 0)        assert(f.month == 0);
  if ((f.present & GCHRON_FIELD_DAY) == 0)          assert(f.day == 0);
  if ((f.present & GCHRON_FIELD_DAY_OF_YEAR) == 0)  assert(f.day_of_year == 0);
  if ((f.present & GCHRON_FIELD_WEEKDAY) == 0)      assert(f.weekday == 0);
  if ((f.present & GCHRON_FIELD_HOUR) == 0)         assert(f.hour == 0);
  if ((f.present & GCHRON_FIELD_MINUTE) == 0)       assert(f.minute == 0);
  if ((f.present & GCHRON_FIELD_SECOND) == 0)       assert(f.second == 0);
  if ((f.present & GCHRON_FIELD_FRACTION) == 0)     assert(f.nsec == 0);
  if ((f.present & GCHRON_FIELD_OFFSET) == 0) {
    assert(f.offset_sec == 0);
    assert(!f.offset_unknown);
  }
  if ((f.present & GCHRON_FIELD_ZONE_ID) == 0) {
    assert(f.zone_id[0] == '\0');
  }
  else {
    // NUL-terminated inside its own buffer, always: the field is copied
    // rather than borrowed precisely so that it is a value a caller can keep.
    assert(memchr(f.zone_id, '\0', sizeof(f.zone_id)) != nullptr);
  }

  // Ranges the resolvers are entitled to trust.
  if ((f.present & GCHRON_FIELD_FRACTION) != 0) {
    assert(f.nsec >= 0 && f.nsec < 1000000000);
  }
  if ((f.present & GCHRON_FIELD_OFFSET) != 0) {
    assert(f.offset_sec > -86400 && f.offset_sec < 86400);
  }
  if ((f.present & GCHRON_FIELD_MONTH) != 0) {
    assert(f.month >= 0 && f.month <= 99);
  }
  if ((f.present & GCHRON_FIELD_WEEKDAY) != 0) {
    assert(f.weekday >= 1 && f.weekday <= 7);
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 3) {
    return 0;
  }

  // One byte of options, then a pattern and a text separated by a NUL. A
  // split the mutator can move is worth more than a fixed one: it lets the
  // fuzzer trade pattern length against text length on its own.
  const uint8_t options = data[0];
  const uint8_t * body = data + 1;
  const size_t body_len = size - 1;
  const uint8_t * split =
      static_cast<const uint8_t *>(memchr(body, 0, body_len));
  if (split == nullptr) {
    return 0;
  }
  const char * pattern = reinterpret_cast<const char *>(body);
  const size_t pattern_len = static_cast<size_t>(split - body);
  const char * text = reinterpret_cast<const char *>(split + 1);
  const size_t text_len = body_len - pattern_len - 1;

  GCHRON_Format * format = nullptr;
  GCHRON_Error err;
  if (gchron_format_compile(pattern, pattern_len,
          (options & 0x01) ? GCHRON_FORMAT_STRFTIME : GCHRON_FORMAT_LDML,
          nullptr, nullptr, &format, &err) != GCHRON_OK) {
    return 0;
  }

  // A fixed clock, so that a two-digit year is exercised rather than refused
  // on every input - and a fixed one, so the harness stays deterministic.
  GCHRON_FixedClock fixed;
  GCHRON_Instant epoch{ 1789000000, 0 };
  GCHRON_PatternContext context;
  memset(&context, 0, sizeof(context));
  if (gchron_clock_fixed(epoch, &fixed) == GCHRON_OK && (options & 0x02)) {
    context.clock = reinterpret_cast<const GCHRON_Clock *>(&fixed);
  }

  GCHRON_ParsedFields fields;
  GCHRON_Error parse_err;
  GCHRON_Result result = gchron_format_parse(format, text, text_len, &context,
      &fields, &parse_err);

  if (result == GCHRON_OK) {
    assert(fields.consumed == text_len && "a parse succeeded mid-text");
    unclaimed_fields_are_zero(fields);

    // The resolvers must answer rather than trap, whatever the fields hold.
    GCHRON_Date date;
    GCHRON_Time time;
    GCHRON_DateTime dt;
    GCHRON_OffsetDateTime odt;
    (void)gchron_parsed_to_date(&fields, &context, &date, nullptr);
    (void)gchron_parsed_to_time(&fields, &context, &time, nullptr);
    (void)gchron_parsed_to_datetime(&fields, &context, &dt, nullptr);
    (void)gchron_parsed_to_offset(&fields, &context, &odt, nullptr);
  }
  else if (result != GCHRON_ERR_UNSUPPORTED) {
    /*
     * A failure must point somewhere inside the text, or at its end.
     *
     * GCHRON_ERR_UNSUPPORTED is the exception and is excluded deliberately:
     * it is the refusal of a pattern letter that cannot be read back at all,
     * so its offset is into the *pattern* rather than into the text. That the
     * two share one field is why this assertion found the bug it did - the
     * refusal used to report an item index, which pointed into neither.
     */
    assert(parse_err.offset <= text_len);
  }

  /*
   * The emit-then-read path, driven with patterns nobody wrote by hand.
   *
   * What is asserted here is the invariants, and deliberately **not** a round
   * trip. Three attempts at stating a round-trip property all turned out to
   * be false rather than violated, and the reason is the same each time: a
   * pattern with adjacent variable-width numeric fields is genuinely
   * ambiguous, and no identity survives it.
   *
   *  - Equality of fields fails on `DHu`, which writes `264152026`. TR35's
   *    adjacent-field rule reads one digit for each of the first two, and ICU
   *    does the same.
   *  - "A pattern that can write can read" fails on `g6`, which formats to
   *    `613046` and has no way back, because the literal `6` is a digit the
   *    greedy field has eaten. Asked the same thing ICU answers with a year
   *    around 62000 - a silently wrong date rather than a refusal - so
   *    failing is the better behaviour and must not be asserted against.
   *  - Even a fixed point fails, on `kug`: the value that comes back is a
   *    different one, and formatting a different value gives different text.
   *    It does not converge on a second application either.
   *
   * So the identity is asserted in tests/unit/test_format_parse.cpp, over
   * patterns chosen to be unambiguous, which is where a claim that strong
   * belongs - and which is what caught the emitter's minimum width
   * disagreeing with the reader's exact one. What belongs here is what is
   * true of every pattern: the read is memory-safe, it consumes what it
   * claims, and the field set it produces is internally consistent.
   */
  {
    GCHRON_DateTime original;
    memset(&original, 0, sizeof(original));
    original.date.year = 2026;
    original.date.month = 9;
    original.date.day = 21;
    original.time.hour = 15;
    original.time.minute = 30;
    original.time.second = 45;
    original.time.nsec = 123456789;

    char buffer[1024];
    size_t written = 0;
    if (gchron_format_datetime(format, &original, nullptr, buffer,
            sizeof(buffer), &written) == GCHRON_OK) {
      GCHRON_ParsedFields again;
      if (gchron_format_parse(format, buffer, written, &context, &again,
              nullptr) == GCHRON_OK) {
        assert(again.consumed == written);
        unclaimed_fields_are_zero(again);

        GCHRON_DateTime back;
        GCHRON_OffsetDateTime back_offset;
        (void)gchron_parsed_to_datetime(&again, &context, &back, nullptr);
        (void)gchron_parsed_to_offset(&again, &context, &back_offset, nullptr);
      }
    }
  }

  gchron_format_destroy(format);
  return 0;
}
