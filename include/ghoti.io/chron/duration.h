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
 * Durations: an amount of time, in calendar units, exact units, or both.
 *
 * Reference: RFC 3339 appendix A; ISO 8601-1:2019 section 5.5.2.
 */

#ifndef GHOTI_IO_GCHRON_DURATION_H
#define GHOTI_IO_GCHRON_DURATION_H

#include <ghoti.io/chron/calendar.h>
#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An amount of time.
 *
 * One type carries both kinds of unit and the *operation* decides what they
 * mean, which is Temporal's arrangement rather than `java.time`'s two types
 * (design.md section 4.1). The reason is ergonomic: an ISO 8601 duration
 * string `P1Y2M3DT4H5M6S` is neither a `Period` nor a `Duration`, and every
 * program that reads one has to hand-combine two objects.
 *
 * - **Calendar units** - `years`, `months`, `weeks`, `days` -
 *   have no fixed length. A day in a zone with a daylight-saving transition
 *   is 23 or 25 hours; a month is 28 to 31 days. Applying one needs a
 *   calendar, and in a zone, a zone.
 * - **Exact units** - `hours` down to `nsec` - are SI. Applying one to
 *   an instant is addition.
 *
 * Adding a month to an instant is therefore GCHRON_ERR_INVALID and not a
 * guess at 30 days (design.md, mistake M9).
 *
 * **Sign.** Every non-zero field carries the same sign, or the duration is
 * invalid. "One month minus one day" is not a duration; it is two operations,
 * and writing it as one is how `Jan 31 + P1M-1D` came to mean four different
 * things in four libraries.
 *
 * As with GCHRON_Instant, `nsec` is always 0..999999999 and the sign of a
 * negative sub-second duration lives in `seconds`.
 */
typedef struct GCHRON_Duration {
  int64_t years;   ///< Calendar unit.
  int64_t months;  ///< Calendar unit.
  int64_t weeks;   ///< Calendar unit.
  int64_t days;    ///< Calendar unit.
  int64_t hours;   ///< Exact.
  int64_t minutes; ///< Exact.
  int64_t seconds; ///< Exact.
  int32_t nsec;    ///< Exact; 0..999999999, with the sign in @ref seconds.
} GCHRON_Duration;

/**
 * @brief What to do when calendar arithmetic lands on a day that does not
 * exist.
 *
 * `Jan 31 + 1 month` has no correct answer; it has a chosen one, and this is
 * where the caller chooses (design.md section 4.3).
 *
 * The consequence, stated here and again wherever this enum is taken:
 * **calendar-unit arithmetic is neither associative nor commutative.**
 * `Jan 31 + P1M + P1M` is March 28 under `CONSTRAIN`; `Jan 31 + P2M` is March
 * 31. A caller who needs a stable "same day next month" keeps a
 * GCHRON_MonthDay and re-derives the date.
 */
typedef enum {
  /**
   * The day does not exist, so the operation fails with GCHRON_ERR_RANGE.
   *
   * Zero, per design.md section 3.7: a caller who zero-initialised an options
   * struct and forgot this field gets a refusal rather than a quiet clamp.
   */
  GCHRON_OVERFLOW_REJECT = 0,

  /** Clamp to the last day of the target month: `Jan 31 + P1M` is Feb 28. */
  GCHRON_OVERFLOW_CONSTRAIN
} GCHRON_Overflow;

/**
 * @brief What to do when a result cannot be expressed exactly.
 *
 * design.md section 3.7 again: the zero value refuses. A caller who did not
 * think about rounding gets told that the answer was not exact, rather than
 * an answer that quietly is not the one they asked for.
 */
typedef enum {
  /** An inexact result is GCHRON_ERR_RANGE. */
  GCHRON_ROUND_REJECT = 0,

  /** Toward zero. What C's integer division does. */
  GCHRON_ROUND_TRUNCATE,

  /** Toward negative infinity. */
  GCHRON_ROUND_FLOOR,

  /** Toward positive infinity. */
  GCHRON_ROUND_CEIL,

  /**
   * To the nearest, with a half going away from zero.
   *
   * What most people mean by "round", and Temporal's default.
   */
  GCHRON_ROUND_HALF_EXPAND,

  /** To the nearest, with a half going to the even value. */
  GCHRON_ROUND_HALF_EVEN
} GCHRON_Rounding;

/**
 * @brief Whether every non-zero field shares one sign, and @ref
 * GCHRON_Duration::nsec is in range.
 *
 * @param d The duration. NULL is not valid.
 * @return `true` when the duration is well-formed.
 */
GCHRON_API bool gchron_duration_is_valid(const GCHRON_Duration * d);

/**
 * @brief The sign of a duration.
 *
 * @param d A valid duration. NULL counts as zero.
 * @return -1, 0 or 1. A duration whose fields disagree in sign is not valid
 *   and returns 0; call gchron_duration_is_valid() to tell the two apart.
 */
GCHRON_API int gchron_duration_sign(const GCHRON_Duration * d);

/**
 * @brief Whether any calendar unit is non-zero.
 *
 * The question every operation that takes a duration asks first: a duration
 * with calendar units cannot be applied to an instant, because the length of
 * a month is not a number of seconds.
 *
 * @param d The duration. NULL counts as having none.
 * @return `true` when @ref GCHRON_Duration::years, months, weeks or days is
 *   non-zero.
 */
GCHRON_API bool gchron_duration_has_calendar_units(const GCHRON_Duration * d);

/**
 * @brief Whether any exact unit is non-zero.
 *
 * @param d The duration. NULL counts as having none.
 * @return `true` when @ref GCHRON_Duration::hours, minutes, seconds or nsec
 *   is non-zero.
 */
GCHRON_API bool gchron_duration_has_exact_units(const GCHRON_Duration * d);

/**
 * @brief Negate a duration, field by field.
 *
 * @param d A valid duration.
 * @param out Receives the negation on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID when @p d is not valid;
 *   GCHRON_ERR_RANGE when a field is `INT64_MIN` and cannot be negated.
 */
GCHRON_API GCHRON_Result gchron_duration_negate(const GCHRON_Duration * d,
    GCHRON_Duration * out);

/**
 * @brief The total exact part of a duration, in seconds and nanoseconds.
 *
 * Calendar units are not counted and their presence is an error, because
 * counting them would mean choosing a length for a month.
 *
 * @param d A valid duration with no calendar units.
 * @param out_seconds Receives the whole seconds. May be NULL.
 * @param out_nanos Receives 0..999999999, with the sign in @p out_seconds.
 *   May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_INVALID when @p d is not valid or carries a
 *   calendar unit; GCHRON_ERR_RANGE on overflow.
 */
GCHRON_API GCHRON_Result gchron_duration_to_exact_seconds(
    const GCHRON_Duration * d, int64_t * out_seconds, int32_t * out_nanos);

/**
 * @brief Build a duration from a whole number of seconds and a nanosecond
 * remainder.
 *
 * @param seconds Whole seconds; may be negative.
 * @param nanos 0..999999999. The sign lives in @p seconds, as it does in
 *   GCHRON_Instant.
 * @param out Receives the duration on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_duration_from_exact_seconds(int64_t seconds,
    int32_t nanos, GCHRON_Duration * out);

/**
 * @brief Whether two durations have identical fields.
 *
 * This is not "are these the same length": `PT90M` and `PT1H30M` are the same
 * length and are not identical, and the library never rewrites one as the
 * other unless asked (design.md section 4.2).
 *
 * @param a The first duration.
 * @param b The second duration.
 * @return `true` when every field matches. Two NULLs are identical.
 */
GCHRON_API bool gchron_duration_identical(const GCHRON_Duration * a,
    const GCHRON_Duration * b);

/*--------------------------------------------------------------------------*
 * Arithmetic
 *--------------------------------------------------------------------------*/

/**
 * @brief Carry a duration's fields into larger units.
 *
 * `90 minutes` and `1 hour 30 minutes` are the same exact duration, and the
 * library never rewrites one as the other unless asked (design.md section
 * 4.2). This is the asking.
 *
 * **A @p largest_unit of GCHRON_UNIT_DAY or above needs @p relative_to**, and
 * is GCHRON_ERR_INVALID without it - not "assume twenty-four hours". The
 * length of a day is not otherwise known: in a zone with a daylight-saving
 * transition it is 23 or 25 hours, and a month is 28 to 31 days.
 *
 * @param d A valid duration.
 * @param largest_unit The largest unit to carry into.
 * @param relative_to The civil date-time the duration is measured from.
 *   Required for GCHRON_UNIT_DAY and above; ignored below it, where every
 *   unit has a fixed length.
 * @param calendar The calendar @p relative_to is written in. NULL means
 *   Gregorian.
 * @param out Receives the balanced duration on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE on overflow;
 *   GCHRON_ERR_UNSUPPORTED for a calendar whose year length in months varies.
 */
GCHRON_API GCHRON_Result gchron_duration_balance(const GCHRON_Duration * d,
    GCHRON_Unit largest_unit, const GCHRON_DateTime * relative_to,
    const GCHRON_Calendar * calendar, GCHRON_Duration * out);

/**
 * @brief Add a duration to a civil date-time.
 *
 * Calendar units first, then exact units, in civil space - which is the order
 * Temporal fixed on and the reason `Jan 31 + P1M + P1M` is not `Jan 31 +
 * P2M`. The consequence is stated in the header for GCHRON_Overflow and is
 * worth stating twice: **calendar-unit arithmetic is neither associative nor
 * commutative.**
 *
 * @param dt A valid civil date-time.
 * @param d A valid duration; either kind of unit, or both.
 * @param calendar The calendar. NULL means Gregorian.
 * @param overflow What to do when the day does not exist in the target month.
 *   The zero value refuses.
 * @param out Receives the result on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE when the day does
 *   not exist and @p overflow refuses, or on overflow; GCHRON_ERR_GAP when
 *   the result lands on a day a calendar reform deleted;
 *   GCHRON_ERR_UNSUPPORTED for a calendar whose year length in months varies.
 */
GCHRON_API GCHRON_Result gchron_datetime_add(const GCHRON_DateTime * dt,
    const GCHRON_Duration * d, const GCHRON_Calendar * calendar,
    GCHRON_Overflow overflow, GCHRON_DateTime * out);

/**
 * @brief Subtract a duration from a civil date-time.
 *
 * @param dt A valid civil date-time.
 * @param d A valid duration.
 * @param calendar The calendar. NULL means Gregorian.
 * @param overflow What to do when the day does not exist.
 * @param out Receives the result on success; untouched on failure.
 * @return As gchron_datetime_add().
 */
GCHRON_API GCHRON_Result gchron_datetime_subtract(const GCHRON_DateTime * dt,
    const GCHRON_Duration * d, const GCHRON_Calendar * calendar,
    GCHRON_Overflow overflow, GCHRON_DateTime * out);

/**
 * @brief Add a duration's calendar units to a date.
 *
 * @param date A valid date.
 * @param d A valid duration whose exact units are all zero; a date has no
 *   time of day for them to act on.
 * @param calendar The calendar. NULL means Gregorian.
 * @param overflow What to do when the day does not exist.
 * @param out Receives the result on success; untouched on failure.
 * @return As gchron_datetime_add(); GCHRON_ERR_INVALID when @p d carries an
 *   exact unit.
 */
GCHRON_API GCHRON_Result gchron_date_add(const GCHRON_Date * date,
    const GCHRON_Duration * d, const GCHRON_Calendar * calendar,
    GCHRON_Overflow overflow, GCHRON_Date * out);

/**
 * @brief The duration from one civil date-time to another.
 *
 * For calendar units this **walks the calendar forward from @p from**, which
 * is why `Jan 31 until Mar 1` in months is `1 month 1 day` and not `1 month
 * -2 days` - and why `until(a, b)` is not the negation of `until(b, a)`. That
 * asymmetry is real, and is documented rather than hidden by symmetrising
 * (design.md section 4.4).
 *
 * @param from The start.
 * @param to The end. Earlier than @p from gives a negative duration.
 * @param largest_unit The largest unit the result may use.
 * @param calendar The calendar. NULL means Gregorian.
 * @param out Receives the duration on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE;
 *   GCHRON_ERR_UNSUPPORTED.
 */
GCHRON_API GCHRON_Result gchron_datetime_until(const GCHRON_DateTime * from,
    const GCHRON_DateTime * to, GCHRON_Unit largest_unit,
    const GCHRON_Calendar * calendar, GCHRON_Duration * out);

/**
 * @brief Round a duration to a unit.
 *
 * @param d A valid duration.
 * @param smallest_unit The finest unit the result may use.
 * @param rounding How to round. The zero value refuses an inexact result.
 * @param relative_to Required when @p smallest_unit is GCHRON_UNIT_DAY or
 *   above, or when @p d carries a calendar unit; ignored otherwise.
 * @param calendar The calendar. NULL means Gregorian.
 * @param out Receives the rounded duration on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE when the result is
 *   inexact and @p rounding refuses, or on overflow.
 */
GCHRON_API GCHRON_Result gchron_duration_round(const GCHRON_Duration * d,
    GCHRON_Unit smallest_unit, GCHRON_Rounding rounding,
    const GCHRON_DateTime * relative_to, const GCHRON_Calendar * calendar,
    GCHRON_Duration * out);

/**
 * @brief Round a civil date-time to a multiple of a unit.
 *
 * A civil day is exactly 24 hours, because a civil reading has no zone and
 * therefore no transitions. gchron_zoned_round() is the one where a day is 23
 * or 25 hours twice a year, and the two give different answers for half the
 * year - which is exactly the bug a caller writes for themselves by rounding
 * the underlying instant and calling it a local day.
 *
 * @param in A valid civil date-time.
 * @param smallest The unit to round to. Every unit is legal here:
 *   GCHRON_UNIT_WEEK rounds to a Monday, ISO 8601's first day of the week and
 *   the one numbering this library uses everywhere; GCHRON_UNIT_MONTH to the
 *   first of a month; GCHRON_UNIT_YEAR to the first of January.
 * @param increment How many of @p smallest one bucket is. It must divide the
 *   next unit up, so 15 minutes is legal and 7 is not. The calendar units -
 *   day, week, month and year - take 1 only, because months are not all the
 *   same length and weeks do not tile either a month or a year, so a larger
 *   increment would describe boundaries that do not exist.
 * @param mode What to do with a value between two boundaries. Toward zero,
 *   for GCHRON_ROUND_TRUNCATE and a GCHRON_ROUND_HALF_EXPAND tie, means
 *   toward 1970-01-01T00:00 - the same direction gchron_instant_round() takes
 *   for the same reading, so the two agree about which side of a boundary a
 *   value is on.
 * @param calendar The calendar the month and year boundaries are in. NULL
 *   means Gregorian. Ignored for units below a month.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID for a bad argument or an increment
 *   that does not tile its unit; GCHRON_ERR_RANGE on overflow, or when the
 *   value is not already exact and @p mode refuses.
 */
GCHRON_API GCHRON_Result gchron_datetime_round(const GCHRON_DateTime * in,
    GCHRON_Unit smallest, int64_t increment, GCHRON_Rounding mode,
    const GCHRON_Calendar * calendar, GCHRON_DateTime * out);

/**
 * @brief Write a human-readable description of a duration to a stream.
 *
 * For debugging and for tests; not a grammar. parse.h writes RFC 3339.
 *
 * @param d The duration. NULL prints as such.
 * @param stream Where to write. NULL is ignored.
 */
GCHRON_API void gchron_duration_dump(const GCHRON_Duration * d, FILE * stream);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_DURATION_H
