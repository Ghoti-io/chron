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
 * An instant, in a zone: the type that knows both what moment it is and what
 * a clock in that place reads.
 *
 * Tier 2 (design.md section 3).
 */

#ifndef GHOTI_IO_GCHRON_ZONED_H
#define GHOTI_IO_GCHRON_ZONED_H

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/offset.h>
#include <ghoti.io/chron/parse.h>
#include <ghoti.io/chron/zone.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An instant and a zone.
 *
 * That is all it *is*. The civil reading is derived on demand, and
 * @ref offset_sec is a cache so that formatting does not re-run the
 * transition search - a convenience, never the authority.
 *
 * This is Temporal's model and the opposite of Joda-Time's, where the zoned
 * type held civil fields and the arithmetic on them was where the
 * daylight-saving bugs lived (design.md section 3.5).
 *
 * The consequence worth knowing: **arithmetic on a zoned date-time is defined
 * by the unit.** Adding one *day* keeps the wall-clock time and skips or
 * repeats an hour as the zone requires; adding twenty-four *hours* does not,
 * and on the day the clocks change the two are different instants. A library
 * that gives the same answer to both has picked one meaning for the caller.
 *
 * The zone is **borrowed**. A GCHRON_ZonedDateTime is valid only as long as
 * the database that produced its zone.
 */
typedef struct GCHRON_ZonedDateTime {
  GCHRON_Instant instant;   ///< The moment. The authority.
  const GCHRON_Zone * zone; ///< Borrowed; never freed by the caller.
  int32_t offset_sec;       ///< What the zone said for @ref instant. Cached.
} GCHRON_ZonedDateTime;

/**
 * @brief The zoned date-time an instant has in a zone.
 *
 * Cannot fail for a valid instant: every instant has exactly one reading in
 * every zone. It is the other direction that can fail.
 *
 * @param instant A valid instant.
 * @param zone The zone. Borrowed, and must outlive the result.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_zoned_from_instant(GCHRON_Instant instant,
    const GCHRON_Zone * zone, GCHRON_ZonedDateTime * out);

/**
 * @brief The instant a wall-clock reading names in a zone.
 *
 * **The function that can fail**, and the reason absolute time and civil time
 * are different types (design.md, mistake M1). A reading may name no instant
 * - the clocks went forward over it - or two, because they went back.
 *
 * On GCHRON_ERR_GAP and GCHRON_ERR_AMBIGUOUS the @p out parameter is not
 * written, per CONVENTIONS.md section 5, and gchron_zone_offsets_for_civil()
 * is how a caller who wants to ask the user gets the candidates.
 *
 * @param civil A valid civil date-time.
 * @param zone The zone. Borrowed, and must outlive the result.
 * @param resolve What to do about a gap or an overlap. The zero value
 *   refuses both.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_GAP; GCHRON_ERR_AMBIGUOUS;
 *   GCHRON_ERR_INVALID; GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_zoned_from_civil(GCHRON_DateTime civil,
    const GCHRON_Zone * zone, GCHRON_Resolve resolve,
    GCHRON_ZonedDateTime * out);

/**
 * @brief What a clock in that zone reads at that moment.
 *
 * @param zoned A valid zoned date-time.
 * @param out Receives the civil reading on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_zoned_to_civil(
    const GCHRON_ZonedDateTime * zoned, GCHRON_DateTime * out);

/**
 * @brief The offset, DST flag and abbreviation in force.
 *
 * @param zoned A valid zoned date-time.
 * @param out Receives the answer on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_zoned_info(const GCHRON_ZonedDateTime * zoned,
    GCHRON_ZoneInfo * out);

/**
 * @brief The same moment as a GCHRON_OffsetDateTime.
 *
 * Lossy, and documented as such: RFC 3339 carries an offset and not a zone
 * name, so `America/New_York` becomes `-05:00` and the fact that it was New
 * York is gone (design.md, mistake M13). RFC 9557's `[America/New_York]`
 * suffix is what keeps it.
 *
 * @param zoned A valid zoned date-time.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_zoned_to_offset(
    const GCHRON_ZonedDateTime * zoned, GCHRON_OffsetDateTime * out);

/**
 * @brief The first instant of the day a zoned date-time falls on.
 *
 * **Not midnight**, because in a zone where midnight did not exist that day
 * there is no such reading - America/Sao_Paulo on 2018-11-04, where the
 * clocks went from 23:59:59 to 01:00:00 - and `setHours(0,0,0,0)` has been
 * getting that wrong for as long as it has existed (design.md, mistake M11).
 *
 * This resolves a gap to the instant **after** it, because the first instant
 * of a day is after the gap and not before it. That is the reason
 * GCHRON_Resolve is a parameter on the general conversion rather than a
 * setting on the process: a library with one process-wide policy could not
 * make this function want something different.
 *
 * @param zoned A valid zoned date-time.
 * @param out Receives the first instant of that day; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_zoned_start_of_day(
    const GCHRON_ZonedDateTime * zoned, GCHRON_ZonedDateTime * out);

/**
 * @brief The same moment, read in a different zone.
 *
 * @param zoned A valid zoned date-time.
 * @param zone The zone to read it in. Borrowed, and must outlive the result.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_zoned_with_zone(
    const GCHRON_ZonedDateTime * zoned, const GCHRON_Zone * zone,
    GCHRON_ZonedDateTime * out);

/**
 * @brief Add a duration to a zoned date-time.
 *
 * **This is the row of design.md section 4.1's table that matters.** Calendar
 * units are applied in the zone's civil space and resolved by @p resolve;
 * exact units are then added to the instant. So "tomorrow at the same time"
 * is `+1 day` and "in twenty-four hours" is `+24 hours`, and on the day the
 * clocks change they are different instants. A library that gave the same
 * answer to both would have picked one meaning for the caller.
 *
 * Adding a calendar unit can land on a reading that did not occur - `+1 day`
 * onto the eve of a spring-forward morning, at an hour that morning skipped -
 * which is why this takes a GCHRON_Resolve as well as a GCHRON_Overflow.
 *
 * @param zoned A valid zoned date-time.
 * @param d A valid duration; either kind of unit, or both.
 * @param resolve What to do when the civil result falls in a gap or an
 *   overlap. The zero value refuses. Ignored when @p d has no calendar units,
 *   because exact arithmetic on an instant cannot land in a gap.
 * @param overflow What to do when the day does not exist in the target month.
 *   The zero value refuses.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE; GCHRON_ERR_GAP;
 *   GCHRON_ERR_AMBIGUOUS.
 */
GCHRON_API GCHRON_Result gchron_zoned_add(const GCHRON_ZonedDateTime * zoned,
    const GCHRON_Duration * d, GCHRON_Resolve resolve,
    GCHRON_Overflow overflow, GCHRON_ZonedDateTime * out);

/**
 * @brief The duration from one zoned date-time to another.
 *
 * Calendar units are counted in the **start**'s zone civil space, so that
 * "one month later" means what a person in that place would mean.
 *
 * @param from The start.
 * @param to The end. Its zone is ignored; only the instant matters.
 * @param largest_unit The largest unit the result may use.
 * @param out Receives the duration on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_zoned_until(const GCHRON_ZonedDateTime * from,
    const GCHRON_ZonedDateTime * to, GCHRON_Unit largest_unit,
    GCHRON_Duration * out);

/**
 * @brief Order two zoned date-times **by the instant they name**.
 *
 * Two readings in different zones compare equal when they are the same
 * moment. gchron_zoned_identical() is the other question (mistake M16).
 *
 * @param a The first value.
 * @param b The second value.
 * @return Negative, zero or positive. NULL sorts before non-NULL.
 */
GCHRON_API int gchron_zoned_compare(const GCHRON_ZonedDateTime * a,
    const GCHRON_ZonedDateTime * b);

/**
 * @brief Whether two zoned date-times name the same moment **in the same
 * zone**.
 *
 * Zone identity is pointer identity: two zones from one database with one
 * canonical identifier are one zone.
 *
 * @param a The first value.
 * @param b The second value.
 * @return `true` when the instant and the zone both match. Two NULLs are
 *   identical.
 */
GCHRON_API bool gchron_zoned_identical(const GCHRON_ZonedDateTime * a,
    const GCHRON_ZonedDateTime * b);

/*--------------------------------------------------------------------------*
 * RFC 9557
 *--------------------------------------------------------------------------*/

/**
 * @brief Parse RFC 9557's *Internet Extended Date/Time Format*.
 *
 * `2026-09-20T17:30:00+02:00[Europe/Paris]`. RFC 3339 plus annotations in
 * square brackets: a zone name, and any number of `key=value` tags of which
 * `[u-ca=julian]` is the one with a meaning here.
 *
 * **This is the grammar that fixes mistake M13.** RFC 3339 carries an offset
 * and not a zone, so `2026-03-08T01:30-05:00` cannot say it meant New York -
 * and next summer that same place is on a different offset. The annotation is
 * what carries the name, and this is the only text format in the library that
 * can round-trip a GCHRON_ZonedDateTime without losing it.
 *
 * Three things the RFC asks of a reader, and this does:
 *
 * - **A `!` on an annotation makes it critical.** A critical annotation the
 *   implementation does not understand is GCHRON_ERR_UNSUPPORTED, which is
 *   the whole point of the flag: the writer is saying that ignoring this
 *   would change the meaning.
 * - **An offset that disagrees with the zone** is resolved by
 *   GCHRON_ParseOptions::zone_conflict, whose zero value refuses. Whichever
 *   way it is resolved, GCHRON_ParseInfo::offset_disagreed_with_zone records
 *   that the text contradicted itself.
 * - **The annotation may be an offset rather than a name** (`[-05:00]`), and
 *   then the zone is a fixed-offset zone.
 *
 * @param text The input. Not assumed to be NUL-terminated.
 * @param len Bytes of input.
 * @param db The database to resolve a zone name against. Must outlive
 *   @p out. NULL parses the grammar but refuses any name annotation with
 *   GCHRON_ERR_INVALID, because there would be nothing to resolve it with.
 * @param opts Options. NULL means gchron_parse_options_default().
 * @param out Receives the value on success; untouched on failure.
 * @param info Receives what the text said - the annotation, the calendar, and
 *   whether the two halves agreed. May be NULL.
 * @param err Receives the failure and its position. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_FORMAT; GCHRON_ERR_UNSUPPORTED for a critical
 *   annotation that is not understood, or a zone the database does not have;
 *   GCHRON_ERR_GAP and GCHRON_ERR_AMBIGUOUS when the zone is believed and the
 *   reading does not resolve; GCHRON_ERR_INVALID; GCHRON_ERR_LIMIT.
 */
GCHRON_API GCHRON_Result gchron_parse_rfc9557(const char * text, size_t len,
    GCHRON_ZoneDb * db, const GCHRON_ParseOptions * opts,
    GCHRON_ZonedDateTime * out, GCHRON_ParseInfo * info, GCHRON_Error * err);

/** Bytes an RFC 9557 timestamp needs, the terminating NUL included. */
#define GCHRON_RFC9557_MAX ((size_t)96)

/**
 * @brief Write a zoned date-time as RFC 9557.
 *
 * `2026-09-20T17:30:00+02:00[Europe/Paris]`. The output contract is
 * parse.h's: a NUL-terminated string, @p out_len its length without the NUL,
 * and GCHRON_ERR_LIMIT with @p out_len still set when the buffer is too
 * small.
 *
 * An anonymous zone - one built from a `TZ` rule, or read from a
 * `/etc/localtime` that is a plain file - has no name to annotate, so the
 * output is plain RFC 3339. That is the honest answer rather than an invented
 * name, and gchron_zone_id() is how a caller checks in advance.
 *
 * @param zoned A valid zoned date-time.
 * @param opts Options. NULL means gchron_write_options_default().
 * @param buf Where to write. May be NULL only when @p buf_len is 0.
 * @param buf_len Bytes available at @p buf.
 * @param out_len Receives the length written, without the NUL. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_LIMIT; GCHRON_ERR_RANGE for a year RFC 3339
 *   cannot spell; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_write_rfc9557(
    const GCHRON_ZonedDateTime * zoned, const GCHRON_WriteOptions * opts,
    char * buf, size_t buf_len, size_t * out_len);

/**
 * @brief Write a human-readable description of a zoned date-time to a stream.
 *
 * @param zoned The value. NULL prints as such.
 * @param stream Where to write. NULL is ignored.
 */
GCHRON_API void gchron_zoned_dump(const GCHRON_ZonedDateTime * zoned,
    FILE * stream);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_ZONED_H
