/**
 * @file
 *
 * Civil time with a fixed offset from UTC: what an RFC 3339 timestamp says.
 *
 * Tier 1 (design.md section 3).
 *
 * Reference: RFC 3339 sections 4.3 and 5.6.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_OFFSET_H
#define GHOTI_IO_GCHRON_OFFSET_H

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A civil date-time together with the offset it was written with.
 *
 * An offset is not a zone. `-05:00` does not say New York: it says five hours
 * behind UTC, on that day, and it cannot answer what the offset will be next
 * summer. RFC 9557's `[America/New_York]` suffix is what carries the zone,
 * and zoned.h is the type that holds one (design.md, mistake M13).
 *
 * The offset is in **seconds**, not minutes, because the time-zone database
 * is: Europe/Amsterdam kept local mean time at `+00:19:32` until 1937, and an
 * offset type that cannot hold that cannot round-trip the zone's own history.
 */
typedef struct GCHRON_OffsetDateTime {
  GCHRON_DateTime civil; ///< The reading on the clock that wrote it.
  int32_t offset_sec;    ///< Seconds ahead of UTC; -86399..86399.

  /**
   * The offset was written `-00:00`.
   *
   * RFC 3339 section 4.3 gives `-00:00` a meaning that `Z` and `+00:00` do
   * not have: *the local offset is unknown*. A log line carrying it is
   * telling you something, and a library that rewrites it to `Z` on the way
   * through has destroyed evidence (design.md, mistake M14). The parser sets
   * this flag, every operation that keeps the offset preserves it, and the
   * RFC 3339 writer writes it back.
   *
   * RFC 5322's `-0000` means the same thing and sets the same flag.
   */
  bool offset_unknown;
} GCHRON_OffsetDateTime;

/**
 * @brief A time of day together with the offset it was written with.
 *
 * What RFC 3339's `full-time` production yields, and what EXIF 2.31's
 * `OffsetTime` tags carry beside a date with no zone of its own.
 */
typedef struct GCHRON_OffsetTime {
  GCHRON_Time time;   ///< The reading on the clock.
  int32_t offset_sec; ///< Seconds ahead of UTC; -86399..86399.
  bool offset_unknown; ///< As GCHRON_OffsetDateTime::offset_unknown.
} GCHRON_OffsetTime;

/** The exclusive bound on an offset, in seconds: 24 hours. */
#define GCHRON_OFFSET_LIMIT_SECONDS INT32_C(86400)

/**
 * @brief Build an offset date-time, checking every field.
 *
 * @param civil A valid civil date-time.
 * @param offset_sec Seconds ahead of UTC, strictly between -86400 and 86400.
 * @param offset_unknown Whether the offset was written `-00:00`. Setting it
 *   with a non-zero @p offset_sec is GCHRON_ERR_INVALID: "unknown" is a thing
 *   only zero can be.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_offset_create(const GCHRON_DateTime * civil,
    int32_t offset_sec, bool offset_unknown, GCHRON_OffsetDateTime * out);

/**
 * @brief Whether an offset date-time satisfies its invariants.
 *
 * @param odt The value. NULL is not valid.
 * @return `true` when the civil part is valid and the offset is in range.
 */
GCHRON_API bool gchron_offset_is_valid(const GCHRON_OffsetDateTime * odt);

/**
 * @brief Whether an offset time satisfies its invariants.
 *
 * @param ot The value. NULL is not valid.
 * @return `true` when the time is valid and the offset is in range.
 */
GCHRON_API bool gchron_offset_time_is_valid(const GCHRON_OffsetTime * ot);

/**
 * @brief The instant an offset date-time names.
 *
 * Always succeeds for a valid value short of overflow: an explicit offset
 * cannot be ambiguous and cannot fall in a gap. Those are a *zone's*
 * failures, not an offset's.
 *
 * @param odt A valid offset date-time.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID, or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_offset_to_instant(
    const GCHRON_OffsetDateTime * odt, GCHRON_Instant * out);

/**
 * @brief The offset date-time an instant has at a given offset.
 *
 * @param i A valid instant.
 * @param offset_sec Seconds ahead of UTC.
 * @param offset_unknown Whether to mark the offset as unknown; only
 *   permitted when @p offset_sec is zero.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID, or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_offset_from_instant(const GCHRON_Instant * i,
    int32_t offset_sec, bool offset_unknown, GCHRON_OffsetDateTime * out);

/**
 * @brief The same moment, written with a different offset.
 *
 * @param odt A valid offset date-time.
 * @param offset_sec The offset to rewrite it with.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID, or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_offset_with_offset(
    const GCHRON_OffsetDateTime * odt, int32_t offset_sec,
    GCHRON_OffsetDateTime * out);

/**
 * @brief Order two offset date-times **by the instant they name**.
 *
 * `2026-03-08T01:30-05:00` and `2026-03-08T06:30Z` are the same moment, and
 * this function says so. gchron_offset_identical() is the other question, and
 * it has a different name for the reason `java.time`'s `equals` and `isEqual`
 * needed one (design.md, mistake M16).
 *
 * @param a The first value.
 * @param b The second value.
 * @return Negative, zero or positive. NULL sorts before non-NULL.
 */
GCHRON_API int gchron_offset_compare(const GCHRON_OffsetDateTime * a,
    const GCHRON_OffsetDateTime * b);

/**
 * @brief Whether two offset date-times agree in **every field**, the offset
 * and the unknown-offset flag included.
 *
 * This is the round-trip question: did the text that came out say what the
 * text that went in said?
 *
 * `memcmp` is not this function: the structs have padding.
 *
 * @param a The first value.
 * @param b The second value.
 * @return `true` when every field matches. Two NULLs are identical.
 */
GCHRON_API bool gchron_offset_identical(const GCHRON_OffsetDateTime * a,
    const GCHRON_OffsetDateTime * b);

/**
 * @brief Whether two offset times agree in every field.
 *
 * @param a The first value.
 * @param b The second value.
 * @return `true` when every field matches. Two NULLs are identical.
 */
GCHRON_API bool gchron_offset_time_identical(const GCHRON_OffsetTime * a,
    const GCHRON_OffsetTime * b);

/**
 * @brief Write a human-readable description of an offset date-time to a
 * stream.
 *
 * For debugging and for tests; not a grammar. parse.h writes RFC 3339.
 *
 * @param odt The value. NULL prints as such.
 * @param stream Where to write. NULL is ignored.
 */
GCHRON_API void gchron_offset_dump(const GCHRON_OffsetDateTime * odt,
    FILE * stream);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_OFFSET_H
