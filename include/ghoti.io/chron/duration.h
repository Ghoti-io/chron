/**
 * @file
 *
 * Durations: an amount of time, in calendar units, exact units, or both.
 *
 * Tier 0/1 (design.md section 3). This header carries the type, its
 * invariants and the policy that governs month-end overflow. The arithmetic -
 * `balance`, `until`, rounding, and applying a duration to a calendar - is
 * phase 2 (design.md section 16); what is here is what the phase 0 text
 * grammars need, and every function that is not here yet is absent rather
 * than stubbed.
 *
 * Reference: RFC 3339 appendix A; ISO 8601-1:2019 section 5.5.2.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_DURATION_H
#define GHOTI_IO_GCHRON_DURATION_H

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
 * - **Calendar units** - @ref years, @ref months, @ref weeks, @ref days -
 *   have no fixed length. A day in a zone with a daylight-saving transition
 *   is 23 or 25 hours; a month is 28 to 31 days. Applying one needs a
 *   calendar, and in a zone, a zone.
 * - **Exact units** - @ref hours down to @ref nsec - are SI. Applying one to
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
 * As with GCHRON_Instant, @ref nsec is always 0..999999999 and the sign of a
 * negative sub-second duration lives in @ref seconds.
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
