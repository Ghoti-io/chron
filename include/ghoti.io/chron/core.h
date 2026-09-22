/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Chron.
 *
 * Ghoti.io Chron is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Chron is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Result codes, diagnostics, limits and units for the Ghoti.io Chron library.
 *
 * Tier 0 (documentation/design.md section 3): this header needs nothing but
 * the C library, and every other header in the library reaches it.
 */

#ifndef GHOTI_IO_GCHRON_CORE_H
#define GHOTI_IO_GCHRON_CORE_H

#include <ghoti.io/chron/allocator.h>
#include <ghoti.io/chron/macros.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result code for every operation in the library that can fail.
 *
 * The first nine are the suite's vocabulary (CONVENTIONS.md section 5). The
 * last four are this domain's, and each exists because folding it into one of
 * the nine would make it indistinguishable from something the caller has a
 * different remedy for (design.md section 7.2).
 */
typedef enum {
  GCHRON_OK = 0,          ///< Operation succeeded.
  GCHRON_ERR_IO,          ///< I/O error (read/write/seek failed).
  GCHRON_ERR_FORMAT,      ///< The text is well-formed, but not this grammar.
  GCHRON_ERR_UNSUPPORTED, ///< This format, but a feature not implemented.
  GCHRON_ERR_LIMIT,       ///< A GCHRON_Limits field was exceeded.
  GCHRON_ERR_CORRUPT,     ///< This format, but the bytes are wrong.
  GCHRON_ERR_OOM,         ///< The allocator returned NULL.
  GCHRON_ERR_INVALID,     ///< A caller-supplied argument is wrong.
  GCHRON_ERR_INTERNAL,    ///< The library's own invariant failed; a bug.

  /**
   * The result is not representable: an arithmetic overflow, or a year
   * outside GCHRON_YEAR_MIN..GCHRON_YEAR_MAX.
   *
   * Distinct from GCHRON_ERR_INVALID, which is a *wrong argument*. This is a
   * right argument with no answer, and it is the result a caller checking
   * whether some text matches a grammar must treat as "it matched": the
   * duration `P9999999999999999999999D` is a well-formed RFC 3339 duration
   * that no integer can hold.
   */
  GCHRON_ERR_RANGE,

  /**
   * That civil time did not occur: it fell in a daylight-saving gap, or in
   * the days a calendar reform deleted.
   *
   * Separate from GCHRON_ERR_AMBIGUOUS because the caller's remedy differs -
   * a gap has no candidate instants and an overlap has two.
   */
  GCHRON_ERR_GAP,

  /** That civil time occurred twice, at the end of a daylight-saving period. */
  GCHRON_ERR_AMBIGUOUS,

  /**
   * The data that would answer the question does not cover the instant asked
   * about - the leap-second table has passed its expiry, for one. The remedy
   * is to update the data, which is neither GCHRON_ERR_UNSUPPORTED nor
   * GCHRON_ERR_CORRUPT.
   */
  GCHRON_ERR_EXPIRED,

  GCHRON_RESULT_COUNT
} GCHRON_Result;

/**
 * @brief Convert a result code to a human-readable string.
 *
 * The returned string is static and must not be freed.
 *
 * @param result The result code.
 * @return A description of the result code, never NULL.
 */
GCHRON_API const char * gchron_result_string(GCHRON_Result result);

/**
 * @brief What exactly was wrong, for a failure that has more than one cause.
 *
 * An enum rather than a string so that a test asserts
 * `GCHRON_DIAG_HOUR_OUT_OF_RANGE` and not a sentence that a later edit can
 * rephrase without anything noticing.
 */
typedef enum {
  GCHRON_DIAG_NONE = 0, ///< No further detail; the result code says it all.

  /* Text shape. */
  GCHRON_DIAG_EXPECTED_DIGIT,          ///< An ASCII digit was required here.
  GCHRON_DIAG_EXPECTED_HYPHEN,         ///< A `-` was required here.
  GCHRON_DIAG_EXPECTED_COLON,          ///< A `:` was required here.
  GCHRON_DIAG_EXPECTED_SEPARATOR,      ///< `T` (or an enabled alternative).
  GCHRON_DIAG_EXPECTED_OFFSET,         ///< `Z` or `+HH:MM` / `-HH:MM`.
  GCHRON_DIAG_UNEXPECTED_END,          ///< The text ended mid-grammar.
  GCHRON_DIAG_TRAILING_CHARACTERS,     ///< The grammar ended before the text.
  GCHRON_DIAG_LEADING_WHITESPACE,      ///< No grammar here permits it.
  GCHRON_DIAG_NON_ASCII_DIGIT,         ///< A digit outside U+0030..U+0039.

  /* Field ranges. */
  GCHRON_DIAG_YEAR_OUT_OF_RANGE,       ///< Outside GCHRON_YEAR_MIN..MAX.
  GCHRON_DIAG_MONTH_OUT_OF_RANGE,      ///< Not 1..12.
  GCHRON_DIAG_DAY_OUT_OF_RANGE,        ///< Not 1..the length of that month.
  GCHRON_DIAG_HOUR_OUT_OF_RANGE,       ///< Not 0..23.
  GCHRON_DIAG_MINUTE_OUT_OF_RANGE,     ///< Not 0..59.
  GCHRON_DIAG_SECOND_OUT_OF_RANGE,     ///< Not 0..59, leap seconds aside.
  GCHRON_DIAG_OFFSET_OUT_OF_RANGE,     ///< Not within -24:00..+24:00 exclusive.
  GCHRON_DIAG_NANOSECOND_OUT_OF_RANGE, ///< Not 0..999999999.

  /* Policies (design.md section 3.7: the default refuses). */
  GCHRON_DIAG_LEAP_SECOND_REJECTED,    ///< `:60` under GCHRON_LEAP_REJECT.
  GCHRON_DIAG_LEAP_SECOND_WRONG_MINUTE,///< `:60` outside 23:59 UTC.
  GCHRON_DIAG_LEAP_SECOND_NOT_IN_TABLE,///< `:60` on a day that gained none.
  GCHRON_DIAG_FRACTION_TOO_LONG,       ///< >9 digits under _FRACTION_REJECT.
  GCHRON_DIAG_FRACTION_EMPTY,          ///< A `.` with no digit after it.

  /* Durations (RFC 3339 appendix A). */
  GCHRON_DIAG_DURATION_EMPTY,          ///< `P`, `PT`, `P1YT` - no component.
  GCHRON_DIAG_DURATION_MISSING_UNIT,   ///< A number with no unit letter.
  GCHRON_DIAG_DURATION_UNIT_ORDER,     ///< Units out of order, or repeated.
  GCHRON_DIAG_DURATION_WEEK_COMBINED,  ///< `W` mixed with any other unit.
  GCHRON_DIAG_DURATION_SIGN,           ///< A sign; appendix A permits none.
  GCHRON_DIAG_DURATION_FRACTION,       ///< A fraction; appendix A permits none.

  /* RFC 9557 annotations. */
  GCHRON_DIAG_ANNOTATION_KEY,          ///< Not a `suffix-key`.
  GCHRON_DIAG_ANNOTATION_VALUE,        ///< Not a zone name or suffix value.
  GCHRON_DIAG_ANNOTATION_REPEATED,     ///< A second `[Zone]` annotation.
  GCHRON_DIAG_ANNOTATION_CRITICAL,     ///< A `!` annotation not understood.
  GCHRON_DIAG_OFFSET_ZONE_CONFLICT,    ///< The offset and the zone disagree.
  GCHRON_DIAG_ZONE_NOT_FOUND,          ///< The database has no such zone.

  /* YAML 1.1 timestamps. */
  GCHRON_DIAG_YAML_DATE_WIDTH,         ///< A date with no time needs `YYYY-MM-DD`.
  GCHRON_DIAG_YAML_LOWERCASE_Z,        ///< `z`; YAML's zone is upper case only.

  /* Format patterns. */
  GCHRON_DIAG_UNTERMINATED_QUOTE,      ///< A `'` with no closing `'`.
  GCHRON_DIAG_PATTERN_LETTER_UNKNOWN,  ///< A letter this library has no field for.
  GCHRON_DIAG_PATTERN_LETTER_RUN,      ///< More of one letter than any field uses.
  GCHRON_DIAG_WEEK_YEAR_WITHOUT_WEEK,  ///< `YYYY` with no week letter beside it.

  /* Reading text back through a pattern (format.h, design.md section 8.7). */
  GCHRON_DIAG_PATTERN_LITERAL,         ///< The text lacks a literal the pattern requires.
  GCHRON_DIAG_PATTERN_DIGITS,          ///< A numeric field had no digits, or too few.
  GCHRON_DIAG_PATTERN_NAME,            ///< No name the provider gives matches here.
  GCHRON_DIAG_PATTERN_TRAILING,        ///< The pattern ran out before the text did.
  GCHRON_DIAG_PATTERN_NOT_INVERTIBLE,  ///< `z`, `v` or `O`: reading one needs CLDR.
  GCHRON_DIAG_PATTERN_NEEDS_CLOCK,     ///< A two-digit year, and no clock to place it.
  GCHRON_DIAG_PATTERN_FIELD_MISSING,   ///< Resolution wanted a field the text had not.
  GCHRON_DIAG_PATTERN_FIELD_CONFLICT,  ///< Two fields describe the same thing, differently.

  /* Leap seconds (leap.h). */
  GCHRON_DIAG_LEAP_TABLE_MALFORMED,    ///< A `leap-seconds.list` line did not parse.
  GCHRON_DIAG_LEAP_TABLE_ORDER,        ///< Its entries are not in time order.
  GCHRON_DIAG_LEAP_TABLE_STEP,         ///< An offset that moves by more than a second.
  GCHRON_DIAG_LEAP_TABLE_EMPTY,        ///< It carried no entries at all.
  GCHRON_DIAG_LEAP_TABLE_NO_EXPIRY,    ///< It carried no `#@` expiry line.

  /* Resources. */
  GCHRON_DIAG_INPUT_TOO_LONG,          ///< Past GCHRON_Limits::max_parse_length.
  GCHRON_DIAG_BUFFER_TOO_SMALL,        ///< An output buffer could not hold it.

  GCHRON_DIAG_COUNT
} GCHRON_Diag;

/**
 * @brief Convert a diagnostic to a human-readable string.
 *
 * The returned string is static and must not be freed.
 *
 * @param diag The diagnostic.
 * @return A description of the diagnostic, never NULL.
 */
GCHRON_API const char * gchron_diag_string(GCHRON_Diag diag);

/**
 * @brief A failure, with the position in the input that caused it.
 *
 * Nothing here is allocated: `message` is static, and the struct is
 * caller-owned. A caller that does not want the detail passes NULL wherever a
 * `GCHRON_Error *` is accepted.
 */
typedef struct GCHRON_Error {
  GCHRON_Result code; ///< The result the call returned.
  GCHRON_Diag diag;   ///< Which of that code's causes this was.
  size_t offset;      ///< Byte offset into the input where it went wrong.
  size_t length;      ///< Bytes to underline from @ref offset; may be 0.
  const char * message; ///< Static, human-readable; never NULL after a failure.
} GCHRON_Error;

/**
 * @brief Clear an error to "nothing went wrong".
 *
 * @param error The error to clear. NULL is ignored.
 */
GCHRON_API void gchron_error_clear(GCHRON_Error * error);

/**
 * @brief Caps applied while reading, so that hostile input cannot make the
 * library work or allocate without bound.
 *
 * Zero means "no limit" for every field. Pass NULL wherever a
 * `const GCHRON_Limits *` is accepted to use gchron_limits_default().
 *
 * The struct carries only the fields something in the library actually
 * enforces today. design.md section 13.2 lists the rest - the TZif reader's
 * and the format compiler's - and each arrives in the phase that enforces
 * it, because a limit nothing reads is a promise nothing keeps.
 */
typedef struct GCHRON_Limits {
  size_t max_parse_length; ///< Longest text any parser will look at, in bytes.
  size_t max_tzif_bytes;   ///< Largest TZif image the zone loader will read.
  size_t max_transitions;  ///< Transition records in one zone.
  size_t max_zone_types;   ///< Local-time types in one zone.
  size_t max_zones;        ///< Zones one database will load and cache.
  size_t max_format_length;///< Bytes of pattern the compiler will read.
  size_t max_format_items; ///< Items one compiled pattern may hold.
  size_t max_leap_entries; ///< Rows one `leap-seconds.list` may carry.
} GCHRON_Limits;

/**
 * @brief Fill in the default limits.
 *
 * @param limits Structure to populate. NULL is ignored.
 */
GCHRON_API void gchron_limits_default(GCHRON_Limits * limits);

/**
 * @brief The units time is measured in, ordered smallest to largest.
 *
 * The ordering is load-bearing: `a < b` means `a` is the finer unit, which is
 * what a "largest unit" argument compares against.
 *
 * `GCHRON_UNIT_UNSPECIFIED` is zero so that a caller who zero-initialised a
 * struct and forgot the field gets an error rather than nanoseconds.
 */
typedef enum {
  GCHRON_UNIT_UNSPECIFIED = 0, ///< Not set; an error wherever a unit is needed.
  GCHRON_UNIT_NANOSECOND,      ///< Exact.
  GCHRON_UNIT_MICROSECOND,     ///< Exact.
  GCHRON_UNIT_MILLISECOND,     ///< Exact.
  GCHRON_UNIT_SECOND,          ///< Exact.
  GCHRON_UNIT_MINUTE,          ///< Exact.
  GCHRON_UNIT_HOUR,            ///< Exact.
  GCHRON_UNIT_DAY,             ///< Calendar: 23, 24 or 25 hours in a zone.
  GCHRON_UNIT_WEEK,            ///< Calendar.
  GCHRON_UNIT_MONTH,           ///< Calendar: 28 to 31 days.
  GCHRON_UNIT_YEAR,            ///< Calendar: 365 or 366 days.
  GCHRON_UNIT_COUNT
} GCHRON_Unit;

/**
 * @brief Convert a unit to its name, lowercase and singular ("nanosecond").
 *
 * @param unit The unit.
 * @return A static string, never NULL.
 */
GCHRON_API const char * gchron_unit_string(GCHRON_Unit unit);

/**
 * @brief Whether a unit has a fixed length in SI seconds.
 *
 * Hours and below are exact; days and above are calendar units, whose length
 * depends on a calendar and, in a time zone, on the date (design.md section
 * 4.1). The distinction is what makes "tomorrow at this time" and "in
 * twenty-four hours" different questions.
 *
 * @param unit The unit.
 * @return Non-zero when the unit is exact.
 */
GCHRON_API int gchron_unit_is_exact(GCHRON_Unit unit);

/** The earliest supported year, in every calendar. */
#define GCHRON_YEAR_MIN (-999999999)

/** The latest supported year, in every calendar. */
#define GCHRON_YEAR_MAX (999999999)

/** Nanoseconds in one second. */
#define GCHRON_NANOS_PER_SECOND INT64_C(1000000000)

/** Seconds in one hour. */
#define GCHRON_SECONDS_PER_HOUR INT64_C(3600)

/**
 * Seconds in one day, as this library counts them.
 *
 * Every day is 86,400 seconds: GCHRON_Instant is Unix time, and the leap
 * seconds UTC has inserted are not in the count. design.md section 5.1 says
 * why, and what the library does about them instead.
 */
#define GCHRON_SECONDS_PER_DAY INT64_C(86400)

/** The default for GCHRON_Limits::max_parse_length. */
#define GCHRON_DEFAULT_MAX_PARSE_LENGTH ((size_t)4096)

/**
 * The default for GCHRON_Limits::max_tzif_bytes.
 *
 * The largest zone in tzdata 2026c is under 30 KiB; a megabyte is far above
 * anything real and far below anything that matters to a process.
 */
#define GCHRON_DEFAULT_MAX_TZIF_BYTES ((size_t)(1024 * 1024))

/**
 * The default for GCHRON_Limits::max_transitions.
 *
 * A "fat" zone file pre-expands its table to 2037 and the largest runs to a
 * few thousand entries.
 */
#define GCHRON_DEFAULT_MAX_TRANSITIONS ((size_t)65536)

/** The default for GCHRON_Limits::max_zone_types. */
#define GCHRON_DEFAULT_MAX_ZONE_TYPES ((size_t)256)

/**
 * The default for GCHRON_Limits::max_format_length.
 *
 * In `ctang` a pattern comes from a template and a template may come from a
 * user, which is why the compiler is bounded at all.
 */
#define GCHRON_DEFAULT_MAX_FORMAT_LENGTH ((size_t)4096)

/** The default for GCHRON_Limits::max_format_items. */
#define GCHRON_DEFAULT_MAX_FORMAT_ITEMS ((size_t)1024)

/**
 * Default GCHRON_Limits::max_leap_entries.
 *
 * Twenty-eight leap seconds have been issued since 1972 and the practice is
 * being retired by 2035, so this is roughly a factor of thirty of headroom
 * over anything the file will ever hold.
 */
#define GCHRON_DEFAULT_MAX_LEAP_ENTRIES ((size_t)1024)

/**
 * The default for GCHRON_Limits::max_zones.
 *
 * The whole database is about 600 zones plus their backward links, so this
 * caps a caching database that is being walked rather than one being used.
 */
#define GCHRON_DEFAULT_MAX_ZONES ((size_t)4096)

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_CORE_H
