/**
 * @file
 *
 * Result codes, diagnostics, units and limits: the strings and the defaults.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>

#include "core_internal.h"

/*
 * Indexed by the enum, so a new code without a string here is caught by
 * tests/unit/test_core.cpp walking 0..GCHRON_RESULT_COUNT rather than by a
 * consumer printing "unknown".
 */
static const char * const RESULT_STRINGS[GCHRON_RESULT_COUNT] = {
  [GCHRON_OK] = "ok",
  [GCHRON_ERR_IO] = "I/O error",
  [GCHRON_ERR_FORMAT] = "not this grammar",
  [GCHRON_ERR_UNSUPPORTED] = "unsupported feature",
  [GCHRON_ERR_LIMIT] = "limit exceeded",
  [GCHRON_ERR_CORRUPT] = "corrupt data",
  [GCHRON_ERR_OOM] = "out of memory",
  [GCHRON_ERR_INVALID] = "invalid argument",
  [GCHRON_ERR_INTERNAL] = "internal error",
  [GCHRON_ERR_RANGE] = "result not representable",
  [GCHRON_ERR_GAP] = "that civil time did not occur",
  [GCHRON_ERR_AMBIGUOUS] = "that civil time occurred twice",
  [GCHRON_ERR_EXPIRED] = "the data does not cover that instant",
};

static const char * const DIAG_STRINGS[GCHRON_DIAG_COUNT] = {
  [GCHRON_DIAG_NONE] = "no further detail",
  [GCHRON_DIAG_EXPECTED_DIGIT] = "expected a digit",
  [GCHRON_DIAG_EXPECTED_HYPHEN] = "expected '-'",
  [GCHRON_DIAG_EXPECTED_COLON] = "expected ':'",
  [GCHRON_DIAG_EXPECTED_SEPARATOR] = "expected the date-time separator",
  [GCHRON_DIAG_EXPECTED_OFFSET] = "expected 'Z' or an offset",
  [GCHRON_DIAG_UNEXPECTED_END] = "the text ended too soon",
  [GCHRON_DIAG_TRAILING_CHARACTERS] = "trailing characters",
  [GCHRON_DIAG_LEADING_WHITESPACE] = "leading whitespace",
  [GCHRON_DIAG_NON_ASCII_DIGIT] = "not an ASCII digit",
  [GCHRON_DIAG_YEAR_OUT_OF_RANGE] = "year out of range",
  [GCHRON_DIAG_MONTH_OUT_OF_RANGE] = "month out of range",
  [GCHRON_DIAG_DAY_OUT_OF_RANGE] = "day out of range for that month",
  [GCHRON_DIAG_HOUR_OUT_OF_RANGE] = "hour out of range",
  [GCHRON_DIAG_MINUTE_OUT_OF_RANGE] = "minute out of range",
  [GCHRON_DIAG_SECOND_OUT_OF_RANGE] = "second out of range",
  [GCHRON_DIAG_OFFSET_OUT_OF_RANGE] = "offset out of range",
  [GCHRON_DIAG_NANOSECOND_OUT_OF_RANGE] = "nanosecond out of range",
  [GCHRON_DIAG_LEAP_SECOND_REJECTED] = "a leap second, and the policy refuses",
  [GCHRON_DIAG_LEAP_SECOND_WRONG_MINUTE] = "a leap second outside 23:59 UTC",
  [GCHRON_DIAG_LEAP_SECOND_NOT_IN_TABLE] =
      "a leap second on a day the table says gained none",
  [GCHRON_DIAG_FRACTION_TOO_LONG] = "more than nine fractional digits",
  [GCHRON_DIAG_FRACTION_EMPTY] = "a decimal point with no digits after it",
  [GCHRON_DIAG_DURATION_EMPTY] = "a duration with no components",
  [GCHRON_DIAG_DURATION_MISSING_UNIT] = "a number with no unit",
  [GCHRON_DIAG_DURATION_UNIT_ORDER] = "duration units out of order",
  [GCHRON_DIAG_DURATION_WEEK_COMBINED] = "weeks combined with another unit",
  [GCHRON_DIAG_DURATION_SIGN] = "a sign, which this grammar has no place for",
  [GCHRON_DIAG_DURATION_FRACTION] =
      "a fraction, which this grammar has no place for",
  [GCHRON_DIAG_ANNOTATION_KEY] = "not an annotation key",
  [GCHRON_DIAG_ANNOTATION_VALUE] = "not an annotation value",
  [GCHRON_DIAG_ANNOTATION_REPEATED] = "a second zone annotation",
  [GCHRON_DIAG_ANNOTATION_CRITICAL] =
      "a critical annotation this library does not understand",
  [GCHRON_DIAG_OFFSET_ZONE_CONFLICT] =
      "the offset and the zone annotation disagree",
  [GCHRON_DIAG_YAML_DATE_WIDTH] =
      "a YAML timestamp with no time needs a two-digit month and day",
  [GCHRON_DIAG_YAML_LOWERCASE_Z] =
      "a lower-case 'z'; YAML 1.1 writes its zone in upper case only",
  [GCHRON_DIAG_UNTERMINATED_QUOTE] = "an unterminated quote in the pattern",
  [GCHRON_DIAG_PATTERN_LETTER_UNKNOWN] =
      "a pattern letter this library has no field for",
  [GCHRON_DIAG_PATTERN_LETTER_RUN] =
      "more of one pattern letter than any field uses",
  [GCHRON_DIAG_WEEK_YEAR_WITHOUT_WEEK] =
      "a week-based year with no week letter beside it",
  [GCHRON_DIAG_PATTERN_LITERAL] =
      "the text does not carry a literal the pattern requires",
  [GCHRON_DIAG_PATTERN_DIGITS] =
      "a numeric field had no digits, or fewer than the pattern requires",
  [GCHRON_DIAG_PATTERN_NAME] =
      "no name this names provider gives matches the text here",
  [GCHRON_DIAG_PATTERN_TRAILING] =
      "the pattern was satisfied before the text ran out",
  [GCHRON_DIAG_PATTERN_NOT_INVERTIBLE] =
      "this pattern letter names a zone loosely and cannot be read back",
  [GCHRON_DIAG_PATTERN_NEEDS_CLOCK] =
      "a two-digit year needs a clock to say which century it is in",
  [GCHRON_DIAG_PATTERN_FIELD_MISSING] =
      "the text did not supply a field this value needs",
  [GCHRON_DIAG_PATTERN_FIELD_CONFLICT] =
      "two fields describe the same thing and disagree",
  [GCHRON_DIAG_LEAP_TABLE_MALFORMED] =
      "a leap-seconds.list line that does not parse",
  [GCHRON_DIAG_LEAP_TABLE_ORDER] =
      "leap-seconds.list entries out of time order",
  [GCHRON_DIAG_LEAP_TABLE_STEP] =
      "a leap-second offset that moves by more than one second",
  [GCHRON_DIAG_LEAP_TABLE_EMPTY] = "a leap-seconds.list with no entries",
  [GCHRON_DIAG_LEAP_TABLE_NO_EXPIRY] =
      "a leap-seconds.list with no expiry line",
  [GCHRON_DIAG_INPUT_TOO_LONG] = "input longer than the limit",
  [GCHRON_DIAG_BUFFER_TOO_SMALL] = "the output buffer is too small",
};

static const char * const UNIT_STRINGS[GCHRON_UNIT_COUNT] = {
  [GCHRON_UNIT_UNSPECIFIED] = "unspecified",
  [GCHRON_UNIT_NANOSECOND] = "nanosecond",
  [GCHRON_UNIT_MICROSECOND] = "microsecond",
  [GCHRON_UNIT_MILLISECOND] = "millisecond",
  [GCHRON_UNIT_SECOND] = "second",
  [GCHRON_UNIT_MINUTE] = "minute",
  [GCHRON_UNIT_HOUR] = "hour",
  [GCHRON_UNIT_DAY] = "day",
  [GCHRON_UNIT_WEEK] = "week",
  [GCHRON_UNIT_MONTH] = "month",
  [GCHRON_UNIT_YEAR] = "year",
};

const char * gchron_result_string(GCHRON_Result result) {
  if (result < 0 || result >= GCHRON_RESULT_COUNT
      || RESULT_STRINGS[result] == NULL) {
    return "unknown result";
  }
  return RESULT_STRINGS[result];
}

const char * gchron_diag_string(GCHRON_Diag diag) {
  if (diag < 0 || diag >= GCHRON_DIAG_COUNT || DIAG_STRINGS[diag] == NULL) {
    return "unknown diagnostic";
  }
  return DIAG_STRINGS[diag];
}

const char * gchron_unit_string(GCHRON_Unit unit) {
  if (unit < 0 || unit >= GCHRON_UNIT_COUNT || UNIT_STRINGS[unit] == NULL) {
    return "unknown unit";
  }
  return UNIT_STRINGS[unit];
}

int gchron_unit_is_exact(GCHRON_Unit unit) {
  return unit >= GCHRON_UNIT_NANOSECOND && unit <= GCHRON_UNIT_HOUR;
}

void gchron_error_clear(GCHRON_Error * error) {
  if (error == NULL) {
    return;
  }
  error->code = GCHRON_OK;
  error->diag = GCHRON_DIAG_NONE;
  error->offset = 0;
  error->length = 0;
  error->message = NULL;
}

void gchron_limits_default(GCHRON_Limits * limits) {
  if (limits == NULL) {
    return;
  }
  limits->max_parse_length = GCHRON_DEFAULT_MAX_PARSE_LENGTH;
  limits->max_tzif_bytes = GCHRON_DEFAULT_MAX_TZIF_BYTES;
  limits->max_transitions = GCHRON_DEFAULT_MAX_TRANSITIONS;
  limits->max_zone_types = GCHRON_DEFAULT_MAX_ZONE_TYPES;
  limits->max_zones = GCHRON_DEFAULT_MAX_ZONES;
  limits->max_format_length = GCHRON_DEFAULT_MAX_FORMAT_LENGTH;
  limits->max_format_items = GCHRON_DEFAULT_MAX_FORMAT_ITEMS;
  limits->max_leap_entries = GCHRON_DEFAULT_MAX_LEAP_ENTRIES;
}

GCHRON_Result gchron_fail(GCHRON_Error * error, GCHRON_Result code,
    GCHRON_Diag diag, size_t offset, size_t length) {
  if (error != NULL) {
    error->code = code;
    error->diag = diag;
    error->offset = offset;
    error->length = length;
    /*
     * The diagnostic is the more specific of the two, so it is the one worth
     * reading; the result code's string is what a caller who did not pass an
     * error gets anyway.
     */
    error->message =
        (diag == GCHRON_DIAG_NONE) ? gchron_result_string(code)
                                   : gchron_diag_string(diag);
  }
  return code;
}
