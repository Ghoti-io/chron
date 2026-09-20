/**
 * @file
 *
 * The named text grammars, in both directions.
 *
 * Every function here names the specification it implements. "Parse a date"
 * with no grammar named is a heuristic, and heuristics are how
 * `Date.parse("2026-09-20")` came to give a different day in different
 * browsers (design.md section 8.1).
 *
 * Tier 1 (design.md section 3): no data file, no operating-system call. The
 * writers live here beside the parsers they invert, so that `parse(write(x))`
 * is one header's promise; format.h's pattern compiler is a separate, tier-3
 * route to the same text and is not needed to produce an RFC 3339 timestamp.
 *
 * Phase 0 (design.md section 16) implements RFC 3339 - `date-time`,
 * `full-date`, `full-time` and appendix A's `duration` - and TOML 1.0.0's
 * four date-time types. The remaining grammars of section 8.1 arrive with the
 * phases that need them, and are absent rather than stubbed.
 *
 * Reference: RFC 3339 (2002), sections 4.3, 5.6, 5.7 and appendix A;
 * RFC 5234 section 2.3, which is why `t` and `z` are as conformant as `T` and
 * `Z`; TOML v1.0.0, *Offset Date-Time*, *Local Date-Time*, *Local Date*,
 * *Local Time*.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_PARSE_H
#define GHOTI_IO_GCHRON_PARSE_H

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/offset.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The longest calendar identifier GCHRON_ParseInfo will carry.
 *
 * Unicode's longest registered one is `ethiopic-amete-alem`, at nineteen
 * characters.
 */
#define GCHRON_CALENDAR_ID_MAX 31

/*--------------------------------------------------------------------------*
 * Policies
 *--------------------------------------------------------------------------*/

/**
 * @brief What to do with a fraction longer than nanosecond precision.
 *
 * Rounding is not offered. A parser that rounds `23:59:59.9999999999` into
 * the next day has changed the date, and no caller asked it to (design.md,
 * mistake M22).
 */
typedef enum {
  /**
   * More than nine fractional digits is GCHRON_ERR_FORMAT.
   *
   * Zero, per design.md section 3.7. RFC 3339 does not say what to do, so the
   * default refuses rather than choosing for the caller.
   */
  GCHRON_FRACTION_REJECT = 0,

  /**
   * Keep the first nine digits and discard the rest, setting
   * GCHRON_ParseInfo::fraction_truncated.
   *
   * This is what TOML 1.0.0 requires ("the additional precision must be
   * truncated, not rounded"), and what a JSON Schema `format` check needs:
   * the suite's `00:59:59.999999999999999Z` is a valid `time`.
   */
  GCHRON_FRACTION_TRUNCATE
} GCHRON_Fraction;

/**
 * @brief What to do with a `:60` second.
 *
 * A leap second is a real reading that a real clock produced, and the two
 * usual treatments - crash the parser, or clamp it silently - each destroy
 * something (design.md, mistake M15). Here the caller chooses, and whichever
 * they choose, GCHRON_ParseInfo::leap_second records that `:60` was read.
 *
 * Whatever the policy, the resulting GCHRON_Time holds `:59` of the same
 * minute with the same fraction - where the Linux kernel puts the repeated
 * second, and what Temporal does. The mapping is lossy and the flag is the
 * evidence.
 */
typedef enum {
  /** `:60` is GCHRON_ERR_FORMAT. Zero, per design.md section 3.7. */
  GCHRON_LEAP_REJECT = 0,

  /** `:60` anywhere reads as `:59`. */
  GCHRON_LEAP_CLAMP,

  /**
   * As GCHRON_LEAP_CLAMP, but only when the minute is 23:59 **in UTC**, after
   * applying the offset; any other minute is GCHRON_ERR_FORMAT.
   *
   * This is the level a JSON Schema `format` check needs, and the reason it
   * is a level of its own: the suite requires `1998-12-31T23:59:60Z` and
   * `1998-12-31T15:59:60.123-08:00` to be valid and `1998-12-31T23:58:60Z`,
   * `1998-12-31T22:59:60Z` and `2016-12-31T24:59:60+01:00` to be invalid.
   */
  GCHRON_LEAP_MINUTE,

  /**
   * As GCHRON_LEAP_MINUTE, and only on a date the leap-second table lists, so
   * that `1999-06-30T23:59:60Z` - when no leap second occurred - is refused.
   *
   * Requires leap.h and its table (phase 4); a conversion past the table's
   * expiry is GCHRON_ERR_EXPIRED. Until then this level is
   * GCHRON_ERR_UNSUPPORTED, which is a refusal and not a silent fallback to
   * GCHRON_LEAP_MINUTE.
   */
  GCHRON_LEAP_TABLE
} GCHRON_Leap;

/**
 * @brief What to do when an RFC 9557 timestamp's offset and its zone
 * disagree.
 *
 * `2026-03-08T01:30-05:00[America/New_York]` is consistent;
 * `2026-03-08T01:30-08:00[America/New_York]` is not, and something has to
 * give. RFC 9557 section 4.1 leaves the choice to the application, which is
 * exactly what a policy is for (design.md, mistake M13).
 *
 * The function that takes this lives in zoned.h, because resolving a zone
 * name needs a zone database and this header does not - but the policy is
 * here, beside the other three, so that design.md section 3.7's rule can be
 * checked in one place.
 */
typedef enum {
  /**
   * A disagreement is GCHRON_ERR_FORMAT.
   *
   * Zero, per design.md section 3.7. A timestamp whose two halves contradict
   * each other is evidence of a bug somewhere upstream, and picking one half
   * silently is how that bug reaches the next system along.
   */
  GCHRON_ZONECONFLICT_REJECT = 0,

  /**
   * Believe the offset, and keep the instant it names.
   *
   * What a caller wants when the timestamp came from a machine whose clock
   * was right and whose zone table was stale.
   */
  GCHRON_ZONECONFLICT_PREFER_OFFSET,

  /**
   * Believe the zone: re-resolve the civil reading through it and let the
   * offset go.
   *
   * What a caller wants when the timestamp came from a calendar entry that
   * was written before a government changed the rules.
   */
  GCHRON_ZONECONFLICT_PREFER_ZONE
} GCHRON_ZoneConflict;

/*--------------------------------------------------------------------------*
 * Options
 *--------------------------------------------------------------------------*/

/**
 * @brief What a parser is allowed to accept.
 *
 * A zero-initialised struct is the strict default: both policies refuse, no
 * extension is enabled, and the default limits apply. Call
 * gchron_parse_options_default() anyway - it is what will still be right when
 * a later field's safe value is not zero.
 */
typedef struct GCHRON_ParseOptions {
  /** Caps on the input. NULL means gchron_limits_default(). */
  const GCHRON_Limits * limits;

  /** What to do with more than nine fractional digits. */
  GCHRON_Fraction fraction;

  /** What to do with a `:60` second. */
  GCHRON_Leap leap;

  /**
   * Accept a space where the grammar wants `T`.
   *
   * RFC 3339 section 5.6's note permits it "for readability"; TOML 1.0.0
   * permits it outright. Off by default, because a format that accepts a
   * space accepts half of what a caller meant to reject.
   *
   * Lowercase `t` and `z` need no option: RFC 5234 section 2.3 makes ABNF
   * string literals case-insensitive, so `1963-06-19t08:30:06z` is as
   * conformant as the uppercase spelling, and a JSON Schema `format` check
   * must accept it.
   */
  bool allow_space_separator;

  /** What to do when an RFC 9557 offset and zone annotation disagree. */
  GCHRON_ZoneConflict zone_conflict;

  /**
   * Stop at the first byte that is not part of the grammar and report how far
   * the parse got in GCHRON_ParseInfo::consumed, instead of failing with
   * GCHRON_DIAG_TRAILING_CHARACTERS.
   *
   * For a caller reading a timestamp out of a larger document. Off by
   * default: a `format` check must reject `1985-04-12T23:20:50Ztail`.
   */
  bool allow_trailing;
} GCHRON_ParseOptions;

/**
 * @brief Fill in the strict defaults.
 *
 * @param out Structure to populate. NULL is ignored.
 */
GCHRON_API void gchron_parse_options_default(GCHRON_ParseOptions * out);

/**
 * @brief Fill in the options TOML 1.0.0 calls for.
 *
 * `GCHRON_FRACTION_TRUNCATE`, because TOML says to truncate, and a space
 * separator, because TOML permits one.
 *
 * TOML 1.0.0 defines its Offset Date-Time as "an RFC 3339 formatted
 * date-time", whose grammar permits `:60`, but says nothing about leap
 * seconds itself. The leap policy therefore stays `GCHRON_LEAP_REJECT` here,
 * and a caller who has decided the question passes the level they want; the
 * setting is due to be revisited against `toml-test` in phase 2.
 *
 * @param out Structure to populate. NULL is ignored.
 */
GCHRON_API void gchron_parse_options_toml(GCHRON_ParseOptions * out);

/**
 * @brief Fill in the options a JSON Schema `format` check calls for.
 *
 * `GCHRON_FRACTION_TRUNCATE` and `GCHRON_LEAP_MINUTE`, which is what the
 * JSON-Schema-Test-Suite's optional `date-time`, `date` and `time` vectors
 * require (design.md section 5.4).
 *
 * A `format` check asks whether text matches a grammar, not whether the value
 * fits in an integer, so a caller using these options treats
 * **GCHRON_ERR_RANGE as a pass**: `P9999999999999999999999D` is a well-formed
 * RFC 3339 duration that no integer can hold.
 *
 * @param out Structure to populate. NULL is ignored.
 */
GCHRON_API void gchron_parse_options_json_schema(GCHRON_ParseOptions * out);

/**
 * @brief What the text said that the value cannot hold.
 *
 * Every field is a fact about the *text*, and a caller who does not care
 * passes NULL wherever a `GCHRON_ParseInfo *` is accepted.
 */
typedef struct GCHRON_ParseInfo {
  /** Bytes of input the grammar consumed. */
  size_t consumed;

  /**
   * The text said `:60`.
   *
   * The GCHRON_Time that came out says `:59`; this is the evidence that it
   * did not have to.
   */
  bool leap_second;

  /** The fraction had more than nine digits and was cut, not rounded. */
  bool fraction_truncated;

  /** How many fractional digits the text carried, capped at 255. */
  uint8_t fraction_digits;

  /** The offset was written `-00:00`: RFC 3339 section 4.3's *unknown*. */
  bool offset_unknown;

  /** An RFC 9557 `[Zone]` annotation was present. */
  bool had_zone_annotation;

  /**
   * An RFC 9557 offset and zone annotation disagreed, and a policy other
   * than GCHRON_ZONECONFLICT_REJECT resolved it.
   *
   * The evidence that the text contradicted itself, kept for the same reason
   * GCHRON_ParseInfo::leap_second is: the value cannot hold it, and somebody
   * downstream may want to know.
   */
  bool offset_disagreed_with_zone;

  /**
   * The `[u-ca=...]` calendar annotation, NUL-terminated, or empty when the
   * text carried none.
   *
   * **Copied, not borrowed.** An earlier version of this field was a pointer
   * into the caller's own input, which is the arrangement that costs nothing
   * and dangles the first time somebody parses out of a temporary - the first
   * test written against it did exactly that. Every other field of every
   * value type in this library is a value for the same reason (design.md
   * section 3.1), and thirty-two bytes on a caller's stack is a smaller price
   * than a lifetime rule nobody can see.
   *
   * The longest calendar identifier Unicode registers is
   * `ethiopic-amete-alem`, at nineteen characters; a `u-ca` value too long to
   * fit here names no calendar that exists and is GCHRON_ERR_UNSUPPORTED.
   */
  char calendar[GCHRON_CALENDAR_ID_MAX + 1];
} GCHRON_ParseInfo;

/**
 * @brief Clear a parse info to "the text said nothing unusual".
 *
 * @param info The structure to clear. NULL is ignored.
 */
GCHRON_API void gchron_parse_info_clear(GCHRON_ParseInfo * info);

/*--------------------------------------------------------------------------*
 * RFC 3339
 *--------------------------------------------------------------------------*/

/**
 * @brief Parse RFC 3339 section 5.6 `date-time`.
 *
 * `1963-06-19T08:30:06.283185Z`. The year is exactly four digits, the month
 * and day exactly two; the seconds are required; the offset is required and
 * is `Z` or `+HH:MM`. Expanded years, week dates, ordinal dates and the basic
 * form are ISO 8601 and are not this grammar.
 *
 * @param text The input. Not assumed to be NUL-terminated.
 * @param len Bytes of input.
 * @param opts Options. NULL means gchron_parse_options_default().
 * @param out Receives the value on success; untouched on failure.
 * @param info Receives what the text said. May be NULL.
 * @param err Receives the failure and its position. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_FORMAT when the text is not this grammar;
 *   GCHRON_ERR_LIMIT past GCHRON_Limits::max_parse_length;
 *   GCHRON_ERR_INVALID for a NULL @p text or @p out.
 */
GCHRON_API GCHRON_Result gchron_parse_rfc3339_date_time(const char * text,
    size_t len, const GCHRON_ParseOptions * opts, GCHRON_OffsetDateTime * out,
    GCHRON_ParseInfo * info, GCHRON_Error * err);

/**
 * @brief Parse RFC 3339 section 5.6 `full-date`.
 *
 * `1963-06-19`, and nothing else: no offset, no time, no sign, no fifth digit
 * of year.
 *
 * @param text The input. Not assumed to be NUL-terminated.
 * @param len Bytes of input.
 * @param opts Options. NULL means gchron_parse_options_default().
 * @param out Receives the date on success; untouched on failure.
 * @param info Receives what the text said. May be NULL.
 * @param err Receives the failure and its position. May be NULL.
 * @return As gchron_parse_rfc3339_date_time().
 */
GCHRON_API GCHRON_Result gchron_parse_rfc3339_full_date(const char * text,
    size_t len, const GCHRON_ParseOptions * opts, GCHRON_Date * out,
    GCHRON_ParseInfo * info, GCHRON_Error * err);

/**
 * @brief Parse RFC 3339 section 5.6 `full-time`.
 *
 * `08:30:06.283185Z`. The offset is part of the production and is required.
 *
 * @param text The input. Not assumed to be NUL-terminated.
 * @param len Bytes of input.
 * @param opts Options. NULL means gchron_parse_options_default().
 * @param out Receives the time and its offset on success; untouched on
 *   failure.
 * @param info Receives what the text said. May be NULL.
 * @param err Receives the failure and its position. May be NULL.
 * @return As gchron_parse_rfc3339_date_time().
 */
GCHRON_API GCHRON_Result gchron_parse_rfc3339_full_time(const char * text,
    size_t len, const GCHRON_ParseOptions * opts, GCHRON_OffsetTime * out,
    GCHRON_ParseInfo * info, GCHRON_Error * err);

/**
 * @brief Parse RFC 3339 appendix A's `duration`.
 *
 * `P4DT12H30M5S`, `P2W`, `PT0S`. This is **not** ISO 8601's duration grammar,
 * and the difference is not cosmetic: appendix A permits no sign and no
 * fraction, and its productions nest, so that `P1Y2D` - years and days with
 * no months between them - is not a duration at all while `P1Y2M` and `P1M2D`
 * are. The permissive ISO 8601 grammar, which does accept `PT0.5S` and
 * `-P1D`, arrives with phase 2 as a separate function.
 *
 * A component too large for `int64_t` is GCHRON_ERR_RANGE, not
 * GCHRON_ERR_FORMAT: the text *is* this grammar, and a caller performing a
 * `format` check treats that as a pass.
 *
 * @param text The input. Not assumed to be NUL-terminated.
 * @param len Bytes of input.
 * @param opts Options. NULL means gchron_parse_options_default().
 * @param out Receives the duration on success; untouched on failure.
 * @param info Receives what the text said. May be NULL.
 * @param err Receives the failure and its position. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_FORMAT; GCHRON_ERR_RANGE; GCHRON_ERR_LIMIT;
 *   GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_parse_rfc3339_duration(const char * text,
    size_t len, const GCHRON_ParseOptions * opts, GCHRON_Duration * out,
    GCHRON_ParseInfo * info, GCHRON_Error * err);

/*--------------------------------------------------------------------------*
 * TOML v1.0.0
 *--------------------------------------------------------------------------*/

/**
 * @brief Which of TOML's four date-time types some text turned out to be.
 *
 * TOML distinguishes them by which fields are present, not by a tag, so a
 * parser that reads any of the four has to report which it read. Zero is not
 * one of them, so a zero-initialised GCHRON_TomlValue is recognisably unset.
 */
typedef enum {
  GCHRON_TOML_NONE = 0,           ///< Not set.
  GCHRON_TOML_OFFSET_DATE_TIME,   ///< `1979-05-27T07:32:00-08:00`.
  GCHRON_TOML_LOCAL_DATE_TIME,    ///< `1979-05-27T07:32:00`.
  GCHRON_TOML_LOCAL_DATE,         ///< `1979-05-27`.
  GCHRON_TOML_LOCAL_TIME          ///< `07:32:00`.
} GCHRON_TomlKind;

/**
 * @brief One of TOML's four date-time values.
 *
 * TOML's Local Time is the shape a YAML timestamp cannot hold, and the reason
 * a library that only has "date-time with an offset" cannot read TOML
 * (design.md section 11).
 */
typedef struct GCHRON_TomlValue {
  /** Which of the four this is, and so which fields below mean anything. */
  GCHRON_TomlKind kind;

  /**
   * The civil reading.
   *
   * For GCHRON_TOML_LOCAL_DATE the time half is all zeroes and means nothing;
   * for GCHRON_TOML_LOCAL_TIME the date half is 1970-01-01 and means nothing.
   */
  GCHRON_DateTime civil;

  /** Seconds ahead of UTC. Only for GCHRON_TOML_OFFSET_DATE_TIME. */
  int32_t offset_sec;

  /** The offset was `-00:00`. Only for GCHRON_TOML_OFFSET_DATE_TIME. */
  bool offset_unknown;
} GCHRON_TomlValue;

/**
 * @brief Parse any of TOML 1.0.0's four date-time types.
 *
 * @param text The input. Not assumed to be NUL-terminated.
 * @param len Bytes of input.
 * @param opts Options. NULL means gchron_parse_options_toml() - the only
 *   grammar here whose NULL is not the strict default, because TOML states
 *   what its own options are.
 * @param out Receives the value and its kind on success; untouched on
 *   failure.
 * @param info Receives what the text said. May be NULL.
 * @param err Receives the failure and its position. May be NULL.
 * @return As gchron_parse_rfc3339_date_time().
 */
GCHRON_API GCHRON_Result gchron_parse_toml(const char * text, size_t len,
    const GCHRON_ParseOptions * opts, GCHRON_TomlValue * out,
    GCHRON_ParseInfo * info, GCHRON_Error * err);

/*--------------------------------------------------------------------------*
 * Writing
 *--------------------------------------------------------------------------*/

/** Write the shortest fraction that loses nothing: 0, 3, 6 or 9 digits. */
#define GCHRON_FRACTION_DIGITS_AUTO 0

/** Write no fraction at all, whatever the value carries. */
#define GCHRON_FRACTION_DIGITS_NONE (-1)

/**
 * @brief How to spell the output.
 *
 * A zero-initialised struct writes `1963-06-19T08:30:06.283185Z`: an
 * automatic fraction, uppercase `T` and `Z`, and `Z` rather than `+00:00`.
 */
typedef struct GCHRON_WriteOptions {
  /**
   * GCHRON_FRACTION_DIGITS_AUTO, GCHRON_FRACTION_DIGITS_NONE, or 1..9 for
   * exactly that many digits.
   *
   * A fixed count smaller than the value needs **truncates**, and does so
   * because it was asked to; GCHRON_FRACTION_DIGITS_AUTO never loses
   * anything.
   */
  int fraction_digits;

  /** Write `t` and `z` rather than `T` and `Z`. */
  bool lowercase;

  /** Write a space rather than `T`. Outside RFC 3339 proper; TOML permits it. */
  bool space_separator;

  /**
   * Write `+00:00` rather than `Z` for a known zero offset.
   *
   * An unknown offset is written `-00:00` whatever this says, because that
   * spelling is the only one that carries the meaning.
   */
  bool zero_offset_as_numeric;
} GCHRON_WriteOptions;

/**
 * @brief Fill in the default write options.
 *
 * @param out Structure to populate. NULL is ignored.
 */
GCHRON_API void gchron_write_options_default(GCHRON_WriteOptions * out);

/** Bytes an RFC 3339 `full-date` needs, the terminating NUL included. */
#define GCHRON_RFC3339_DATE_MAX ((size_t)11)

/** Bytes an RFC 3339 `full-time` needs, the terminating NUL included. */
#define GCHRON_RFC3339_TIME_MAX ((size_t)25)

/** Bytes an RFC 3339 `date-time` needs, the terminating NUL included. */
#define GCHRON_RFC3339_DATE_TIME_MAX ((size_t)36)

/** Bytes an RFC 3339 appendix A `duration` needs, the NUL included. */
#define GCHRON_RFC3339_DURATION_MAX ((size_t)160)

/**
 * @brief Write an offset date-time as RFC 3339 `date-time`.
 *
 * The output contract for every writer here (design.md section 8.6): on
 * success the buffer holds a NUL-terminated string and @p out_len is its
 * length without the NUL. A buffer too small is GCHRON_ERR_LIMIT with @p
 * out_len set to the length the output *would* have had - so the buffer to
 * allocate is `out_len + 1` - and the buffer's contents are unspecified.
 * There is no allocating variant; the macros above bound every output.
 *
 * `GCHRON_OffsetDateTime::offset_unknown` is written back as `-00:00`, which
 * is what it came in as and what it means (RFC 3339 section 4.3).
 *
 * @param odt A valid offset date-time.
 * @param opts Options. NULL means gchron_write_options_default().
 * @param buf Where to write. May be NULL only when @p buf_len is 0, which is
 *   how a caller asks for the length alone.
 * @param buf_len Bytes available at @p buf.
 * @param out_len Receives the length written, without the NUL. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_LIMIT; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_write_rfc3339_date_time(
    const GCHRON_OffsetDateTime * odt, const GCHRON_WriteOptions * opts,
    char * buf, size_t buf_len, size_t * out_len);

/**
 * @brief Write a date as RFC 3339 `full-date`.
 *
 * @param date A valid date whose year is 0..9999; RFC 3339 has no expanded
 *   year, so anything else is GCHRON_ERR_RANGE rather than a truncated or
 *   signed year.
 * @param buf Where to write; see gchron_write_rfc3339_date_time().
 * @param buf_len Bytes available at @p buf.
 * @param out_len Receives the length written, without the NUL. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_LIMIT; GCHRON_ERR_RANGE; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_write_rfc3339_full_date(
    const GCHRON_Date * date, char * buf, size_t buf_len, size_t * out_len);

/**
 * @brief Write a time and its offset as RFC 3339 `full-time`.
 *
 * @param ot A valid offset time.
 * @param opts Options. NULL means gchron_write_options_default().
 * @param buf Where to write; see gchron_write_rfc3339_date_time().
 * @param buf_len Bytes available at @p buf.
 * @param out_len Receives the length written, without the NUL. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_LIMIT; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_write_rfc3339_full_time(
    const GCHRON_OffsetTime * ot, const GCHRON_WriteOptions * opts, char * buf,
    size_t buf_len, size_t * out_len);

/**
 * @brief Write a duration as RFC 3339 appendix A `duration`.
 *
 * Appendix A has no sign and no fraction, so a negative duration and one with
 * a non-zero nanosecond field are both GCHRON_ERR_UNSUPPORTED here. Phase 2's
 * ISO 8601 writer spells both.
 *
 * A duration of zero writes `PT0S`, which is the shortest spelling appendix A
 * permits: `P` alone is not a duration.
 *
 * @param d A valid duration.
 * @param buf Where to write; see gchron_write_rfc3339_date_time().
 * @param buf_len Bytes available at @p buf.
 * @param out_len Receives the length written, without the NUL. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_LIMIT; GCHRON_ERR_UNSUPPORTED;
 *   GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_write_rfc3339_duration(
    const GCHRON_Duration * d, char * buf, size_t buf_len, size_t * out_len);

/**
 * @brief Write one of TOML's four date-time types.
 *
 * @param value A value whose @ref GCHRON_TomlValue::kind says which.
 * @param opts Options. NULL means gchron_write_options_default().
 * @param buf Where to write; see gchron_write_rfc3339_date_time().
 * @param buf_len Bytes available at @p buf.
 * @param out_len Receives the length written, without the NUL. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_LIMIT; GCHRON_ERR_RANGE; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_write_toml(const GCHRON_TomlValue * value,
    const GCHRON_WriteOptions * opts, char * buf, size_t buf_len,
    size_t * out_len);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_PARSE_H
