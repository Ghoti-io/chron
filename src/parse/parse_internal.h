/**
 * @file
 *
 * The scanner every text grammar in this library shares, and the RFC 3339
 * productions the other grammars are built out of.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_SRC_PARSE_PARSE_INTERNAL_H
#define GHOTI_IO_GCHRON_SRC_PARSE_PARSE_INTERNAL_H

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/parse.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A cursor over the input, plus everything a production needs to decide and
 * to report.
 *
 * The text is never assumed to be NUL-terminated: every grammar here reads
 * out of a document `text` handed the library, and a parser that trusts a
 * terminator reads past the end of the one document that has none.
 */
typedef struct GCHRON_Scanner {
  const char * text;                 /**< The input; not NUL-terminated. */
  size_t len;                        /**< Bytes of input. */
  size_t pos;                        /**< How far the parse has got. */
  const GCHRON_ParseOptions * opts;  /**< Never NULL inside a parser. */
  GCHRON_Error * err;                /**< May be NULL. */
} GCHRON_Scanner;

/** Whether a `time-offset` follows the `partial-time`, and whether it must. */
typedef enum GCHRON_OffsetMode {
  /** Do not read one. TOML's Local Date-Time and Local Time end here. */
  GCHRON_OFFSET_NONE = 0,
  /** One must follow. RFC 3339's `full-time`. */
  GCHRON_OFFSET_REQUIRED,
  /** One may follow, and whether it did is what TOML's kind turns on. */
  GCHRON_OFFSET_OPTIONAL
} GCHRON_OffsetMode;

/** The parts of a `full-time` a production hands back to its caller. */
typedef struct GCHRON_TimeParts {
  GCHRON_Time time;    /**< The time, with `:60` already read as `:59`. */
  bool leap_second;    /**< The text said `:60`. */
  bool truncated;      /**< The fraction was longer than nine digits. */
  uint8_t digits;      /**< Fractional digits the text carried, capped at 255. */
  int32_t offset_sec;  /**< Seconds ahead of UTC. */
  bool offset_unknown; /**< The offset was written `-00:00`. */
  bool has_offset;     /**< An offset was present at all. */
} GCHRON_TimeParts;

/**
 * The half of GCHRON_LEAP_TABLE that needs a date.
 *
 * A `full-time` production can check that a `:60` sits at 23:59 UTC, but not
 * whether that particular day gained a second - that is a question about a
 * date, and it has none. Every parser that reads a date and a time calls this
 * once it has both.
 *
 * @param opts The effective options.
 * @param date The date as the text wrote it, in local terms.
 * @param parts The time, with its offset.
 * @param err Receives the failure. May be NULL.
 * @return GCHRON_OK when the policy is satisfied or does not apply;
 *   GCHRON_ERR_FORMAT when the table lists no leap second that day;
 *   GCHRON_ERR_INVALID when no table was supplied; GCHRON_ERR_EXPIRED or
 *   GCHRON_ERR_RANGE when the table cannot answer for that date.
 */
GCHRON_Result gchron_scan_check_leap_table(const GCHRON_ParseOptions * opts,
    const GCHRON_Date * date, const GCHRON_TimeParts * parts,
    GCHRON_Error * err);

/**
 * Resolve the options a parser was called with.
 *
 * @param opts What the caller passed; may be NULL.
 * @param fallback Storage for the defaults, if they are needed.
 * @return @p opts, or @p fallback filled with gchron_parse_options_default().
 */
const GCHRON_ParseOptions * gchron_parse_options_effective(
    const GCHRON_ParseOptions * opts, GCHRON_ParseOptions * fallback);

/**
 * Check the input against GCHRON_Limits::max_parse_length, and refuse leading
 * whitespace, which no grammar here permits.
 *
 * @param sc The scanner, already initialised.
 * @return GCHRON_OK, GCHRON_ERR_LIMIT or GCHRON_ERR_FORMAT.
 */
GCHRON_Result gchron_scan_preflight(GCHRON_Scanner * sc);

/**
 * Read exactly @p count ASCII digits as a non-negative integer.
 *
 * "Exactly" is the whole point: `1998-1-20` and `2020-001-01` are both
 * refused by this rather than by a range check afterwards, and a digit
 * outside U+0030..U+0039 - the suite's Bengali `২` - is refused as a digit
 * rather than silently ending the number.
 *
 * @param sc The scanner.
 * @param count How many digits, 1..9.
 * @param out Receives the value.
 * @return GCHRON_OK or GCHRON_ERR_FORMAT.
 */
GCHRON_Result gchron_scan_digits(GCHRON_Scanner * sc, int count, int * out);

/**
 * Consume one literal byte.
 *
 * @param sc The scanner.
 * @param c The byte required.
 * @param diag What to report when it is not there.
 * @return GCHRON_OK or GCHRON_ERR_FORMAT.
 */
GCHRON_Result gchron_scan_literal(GCHRON_Scanner * sc, char c,
    GCHRON_Diag diag);

/**
 * Read RFC 3339 section 5.6 `full-date`.
 *
 * @param sc The scanner.
 * @param out Receives the date.
 * @return GCHRON_OK or GCHRON_ERR_FORMAT.
 */
GCHRON_Result gchron_scan_full_date(GCHRON_Scanner * sc, GCHRON_Date * out);

/**
 * Read RFC 3339 section 5.6 `partial-time`, and then the offset if
 * @p require_offset says the production has one.
 *
 * The leap-second policy needs the offset to decide, so `:60` is checked
 * after the offset has been read rather than where it was written.
 *
 * @param sc The scanner.
 * @param mode Whether a `time-offset` follows. GCHRON_OFFSET_NONE reads none
 *   at all, so that a trailing `Z` on a TOML Local Date-Time is trailing text
 *   rather than an offset; GCHRON_OFFSET_OPTIONAL reads one if it is there
 *   and records the fact in GCHRON_TimeParts::has_offset.
 * @param out Receives the time and what the text said about it.
 * @return GCHRON_OK, GCHRON_ERR_FORMAT or GCHRON_ERR_UNSUPPORTED.
 */
GCHRON_Result gchron_scan_full_time(GCHRON_Scanner * sc,
    GCHRON_OffsetMode mode, GCHRON_TimeParts * out);

/**
 * Consume the separator between a date and a time: `T`, `t`, or a space when
 * the options allow one.
 *
 * Lowercase needs no option. RFC 5234 section 2.3 makes ABNF string literals
 * case-insensitive, so `1963-06-19t08:30:06z` is as conformant as the
 * uppercase spelling.
 *
 * @param sc The scanner.
 * @return GCHRON_OK or GCHRON_ERR_FORMAT.
 */
GCHRON_Result gchron_scan_date_time_separator(GCHRON_Scanner * sc);

/**
 * Finish a parse: refuse trailing characters unless the options allow them,
 * and record how far the parse got.
 *
 * @param sc The scanner.
 * @param info Receives GCHRON_ParseInfo::consumed. May be NULL.
 * @return GCHRON_OK or GCHRON_ERR_FORMAT.
 */
GCHRON_Result gchron_scan_finish(GCHRON_Scanner * sc, GCHRON_ParseInfo * info);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_SRC_PARSE_PARSE_INTERNAL_H
