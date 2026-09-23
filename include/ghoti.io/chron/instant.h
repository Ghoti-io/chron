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
 * Instants: a point on the timeline, with no calendar and no zone attached.
 *
 * Tier 1 (design.md section 3): needs no data file and no operating-system
 * call. A GCHRON_Instant and a GCHRON_DateTime are different types, and
 * converting between them needs an offset or a zone - which is the first and
 * largest of the mistakes this library exists not to repeat (design.md,
 * mistake M1).
 */

#ifndef GHOTI_IO_GCHRON_INSTANT_H
#define GHOTI_IO_GCHRON_INSTANT_H

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A point on the timeline: Unix time, to the nanosecond.
 *
 * Seconds since 1970-01-01T00:00:00Z **with every day 86,400 seconds long**.
 * That is POSIX time, and it means the leap seconds UTC has inserted since
 * 1972 are not in the count: `{1483228799, 0}` is both 2016-12-31T23:59:59Z
 * and the 23:59:60Z that followed it. design.md section 5.1 argues the
 * decision and says what the library does about leap seconds instead.
 *
 * Integers, not a `double`: a floating-point millisecond count loses
 * nanoseconds by construction and loses milliseconds far from the epoch
 * (design.md, mistake M5). Sixty-four bits, not thirty-two, everywhere
 * including the interop conversions that read 32-bit fields (mistake M6).
 *
 * `nsec` is always 0..999999999 and the sign lives in `sec`, so -1ns is
 * `{-1, 999999999}`. That is `java.time`'s rule; it makes the arithmetic
 * uniform at the cost of a formatter having to think for a moment.
 */
typedef struct GCHRON_Instant {
  int64_t sec;  ///< Seconds since the Unix epoch; may be negative.
  int32_t nsec; ///< 0..999999999, always, whatever the sign of @ref sec.
} GCHRON_Instant;

/**
 * @brief A half-open span of the timeline, `[start, end)`.
 *
 * Half-open so that abutting intervals tile the line without overlapping and
 * without a gap, which closed intervals cannot do.
 */
typedef struct GCHRON_Interval {
  GCHRON_Instant start; ///< Included.
  GCHRON_Instant end;   ///< Excluded.
} GCHRON_Interval;

/*--------------------------------------------------------------------------*
 * Construction
 *--------------------------------------------------------------------------*/

/**
 * @brief Build an instant from a second count and a nanosecond remainder.
 *
 * @param sec Seconds since the Unix epoch.
 * @param nsec 0..999999999. A value outside that is an error rather than a
 *   carry, because the carry direction for a negative @p sec is exactly what
 *   callers get wrong; gchron_instant_normalize() is the function that
 *   carries.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_instant_create(int64_t sec, int32_t nsec,
    GCHRON_Instant * out);

/**
 * @brief Build an instant from a second count and any nanosecond count,
 * carrying the excess into the seconds.
 *
 * @param sec Seconds since the Unix epoch.
 * @param nanos Any nanosecond count, positive or negative.
 * @param out Receives the normalised instant on success; untouched on
 *   failure.
 * @return GCHRON_OK, or GCHRON_ERR_RANGE when the carry overflows.
 */
GCHRON_API GCHRON_Result gchron_instant_normalize(int64_t sec, int64_t nanos,
    GCHRON_Instant * out);

/**
 * @brief Whether an instant satisfies its invariant.
 *
 * @param i The instant. NULL is not valid.
 * @return `true` when @ref GCHRON_Instant::nsec is 0..999999999.
 */
GCHRON_API bool gchron_instant_is_valid(const GCHRON_Instant * i);

/*--------------------------------------------------------------------------*
 * Civil time in UTC
 *--------------------------------------------------------------------------*/

/**
 * @brief The UTC civil reading of an instant.
 *
 * UTC is the one zone where this conversion cannot be ambiguous and cannot
 * fall in a gap, because UTC has no transitions. Every other zone goes
 * through zone.h, where the same conversion is a function that can fail.
 *
 * @param i A valid instant.
 * @param out Receives the date-time on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE when the instant
 *   falls outside the supported years.
 */
GCHRON_API GCHRON_Result gchron_instant_to_utc(const GCHRON_Instant * i,
    GCHRON_DateTime * out);

/**
 * @brief The instant a UTC civil reading names.
 *
 * @param dt A valid date-time, read as UTC.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE on overflow.
 */
GCHRON_API GCHRON_Result gchron_instant_from_utc(const GCHRON_DateTime * dt,
    GCHRON_Instant * out);

/**
 * @brief Split an instant into the day it falls on and the nanosecond within
 * that day, both in UTC.
 *
 * The division is a floor, so an instant before the epoch gives a negative
 * day and a non-negative nanosecond - which is what every calendar conversion
 * wants and what C's truncating division does not give.
 *
 * @param i A valid instant.
 * @param out_epoch_day Receives days since 1970-01-01. May be NULL.
 * @param out_nanos_of_day Receives 0..86399999999999. May be NULL.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_instant_to_epoch_day(const GCHRON_Instant * i,
    int64_t * out_epoch_day, int64_t * out_nanos_of_day);

/*--------------------------------------------------------------------------*
 * Exact arithmetic
 *--------------------------------------------------------------------------*/

/**
 * @brief Add a duration to an instant.
 *
 * @param i A valid instant.
 * @param d A valid duration with **no calendar units**. A month has no length
 *   in seconds, so adding one to a point on the timeline is a question with
 *   no answer; ask it of a civil date-time or a zoned one instead.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID when an argument is wrong or @p d
 *   carries a calendar unit; GCHRON_ERR_RANGE on overflow, detected before it
 *   happens rather than observed after (design.md, mistake M24).
 */
GCHRON_API GCHRON_Result gchron_instant_add(const GCHRON_Instant * i,
    const GCHRON_Duration * d, GCHRON_Instant * out);

/**
 * @brief Subtract a duration from an instant.
 *
 * @param i A valid instant.
 * @param d A valid duration with no calendar units.
 * @param out Receives the instant on success; untouched on failure.
 * @return As gchron_instant_add().
 */
GCHRON_API GCHRON_Result gchron_instant_subtract(const GCHRON_Instant * i,
    const GCHRON_Duration * d, GCHRON_Instant * out);

/**
 * @brief The exact duration from one instant to another.
 *
 * The result carries seconds and nanoseconds only - never days, because the
 * length of a day is a zone's business and an instant has no zone. Phase 2's
 * `gchron_duration_balance()` is how a caller asks for larger units, and it
 * requires something to be relative to.
 *
 * @param from The earlier end. A later one gives a negative duration.
 * @param to The later end.
 * @param out Receives the duration on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE on overflow.
 */
GCHRON_API GCHRON_Result gchron_instant_until(const GCHRON_Instant * from,
    const GCHRON_Instant * to, GCHRON_Duration * out);

/*--------------------------------------------------------------------------*
 * Rounding
 *--------------------------------------------------------------------------*/

/**
 * @brief Round an instant to a multiple of a unit, measured from the epoch.
 *
 * "The nearest 15 minutes", "the second this fell in", "the start of the UTC
 * day" - bucketing a timestamp, which every caller writes for themselves and
 * writes as integer division that is wrong for negative times.
 *
 * The buckets are laid out from the Unix epoch, so they tile the timeline
 * without a gap and a value already on a boundary is returned unchanged.
 *
 * @param in A valid instant.
 * @param smallest The unit to round to. GCHRON_UNIT_NANOSECOND through
 *   GCHRON_UNIT_DAY only: a week, a month and a year have no length an
 *   instant can be divided by - a month is 28 to 31 days and which one
 *   depends on a calendar this type does not have - so they are
 *   GCHRON_ERR_INVALID here rather than a nanosecond count that pretends
 *   otherwise. gchron_datetime_round() and gchron_zoned_round() take them.
 * @param increment How many of @p smallest one bucket is; 1 is the plain
 *   case. It must divide the next unit up evenly - 15 minutes is legal, 7 is
 *   not, because 7-minute buckets do not tile an hour and the boundary the
 *   caller is imagining does not exist. GCHRON_UNIT_DAY takes 1 only.
 * @param mode What to do with a value between two boundaries.
 *   GCHRON_ROUND_REJECT, the zero value, returns GCHRON_ERR_RANGE rather than
 *   an answer the caller did not ask for.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID for a bad argument, a unit above
 *   GCHRON_UNIT_DAY, or an increment that does not tile its unit;
 *   GCHRON_ERR_RANGE on overflow or under GCHRON_ROUND_REJECT.
 */
GCHRON_API GCHRON_Result gchron_instant_round(const GCHRON_Instant * in,
    GCHRON_Unit smallest, int64_t increment, GCHRON_Rounding mode,
    GCHRON_Instant * out);

/*--------------------------------------------------------------------------*
 * Foreign integer encodings of Unix time
 *--------------------------------------------------------------------------*/

/**
 * @brief An instant from whole Unix seconds.
 *
 * @param seconds Seconds since 1970-01-01T00:00:00Z.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID when @p out is NULL.
 */
GCHRON_API GCHRON_Result gchron_instant_from_unix_seconds(int64_t seconds,
    GCHRON_Instant * out);

/**
 * @brief An instant from Unix milliseconds - JavaScript's `Date.now()`.
 *
 * @param millis Milliseconds since the Unix epoch.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID, or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_instant_from_unix_millis(int64_t millis,
    GCHRON_Instant * out);

/**
 * @brief An instant from Unix microseconds.
 *
 * @param micros Microseconds since the Unix epoch.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID, or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_instant_from_unix_micros(int64_t micros,
    GCHRON_Instant * out);

/**
 * @brief An instant from Unix nanoseconds.
 *
 * @param nanos Nanoseconds since the Unix epoch. The range this can express
 *   is only 1677..2262, which is why it is not the library's own encoding.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID, or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_instant_from_unix_nanos(int64_t nanos,
    GCHRON_Instant * out);

/**
 * @brief Whole Unix seconds, rounding towards negative infinity.
 *
 * @param i A valid instant.
 * @param out Receives the count on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_instant_to_unix_seconds(const GCHRON_Instant * i,
    int64_t * out);

/**
 * @brief Unix milliseconds, rounding towards negative infinity.
 *
 * @param i A valid instant.
 * @param out Receives the count on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID, or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_instant_to_unix_millis(const GCHRON_Instant * i,
    int64_t * out);

/**
 * @brief Unix microseconds, rounding towards negative infinity.
 *
 * @param i A valid instant.
 * @param out Receives the count on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID, or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_instant_to_unix_micros(const GCHRON_Instant * i,
    int64_t * out);

/**
 * @brief Unix nanoseconds.
 *
 * @param i A valid instant.
 * @param out Receives the count on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE outside 1677..2262.
 */
GCHRON_API GCHRON_Result gchron_instant_to_unix_nanos(const GCHRON_Instant * i,
    int64_t * out);

/**
 * @brief An instant as a `double` of seconds since the epoch.
 *
 * **Lossy, and named so.** A `double` has 53 bits of mantissa, so this loses
 * nanoseconds for any instant more than about 104 days from the epoch, and
 * loses microseconds beyond about the year 2255. It exists because a plotting
 * library or a physics step wants one, and refusing to provide it only means
 * the caller writes a worse version.
 *
 * @param i A valid instant.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_instant_as_double(const GCHRON_Instant * i,
    double * out);

/*--------------------------------------------------------------------------*
 * Comparison
 *--------------------------------------------------------------------------*/

/**
 * @brief Order two instants.
 *
 * @param a The first instant.
 * @param b The second instant.
 * @return Negative when @p a is earlier, 0 when equal, positive when later.
 *   NULL sorts before non-NULL.
 */
GCHRON_API int gchron_instant_compare(const GCHRON_Instant * a,
    const GCHRON_Instant * b);

/*--------------------------------------------------------------------------*
 * Intervals
 *--------------------------------------------------------------------------*/

/**
 * @brief Build a half-open interval.
 *
 * @param start The first instant in the interval.
 * @param end The first instant after it. Equal to @p start means an empty
 *   interval; earlier is GCHRON_ERR_INVALID.
 * @param out Receives the interval on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interval_create(const GCHRON_Instant * start,
    const GCHRON_Instant * end, GCHRON_Interval * out);

/**
 * @brief Whether an interval contains an instant.
 *
 * @param interval A valid interval.
 * @param i A valid instant.
 * @return `true` when `start <= i < end`.
 */
GCHRON_API bool gchron_interval_contains(const GCHRON_Interval * interval,
    const GCHRON_Instant * i);

/**
 * @brief Whether two intervals share any instant.
 *
 * Two abutting intervals do not overlap, which is the point of the half-open
 * convention.
 *
 * @param a The first interval.
 * @param b The second interval.
 * @return `true` when some instant is in both.
 */
GCHRON_API bool gchron_interval_overlaps(const GCHRON_Interval * a,
    const GCHRON_Interval * b);

/**
 * @brief How long an interval is.
 *
 * @param interval A valid interval.
 * @param out Receives an exact duration on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID, or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_interval_duration(
    const GCHRON_Interval * interval, GCHRON_Duration * out);

/*--------------------------------------------------------------------------*
 * Debugging
 *--------------------------------------------------------------------------*/

/**
 * @brief Write a human-readable description of an instant to a stream.
 *
 * For debugging and for tests; not a grammar. parse.h writes RFC 3339.
 *
 * @param i The instant. NULL prints as such.
 * @param stream Where to write. NULL is ignored.
 */
GCHRON_API void gchron_instant_dump(const GCHRON_Instant * i, FILE * stream);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_INSTANT_H
