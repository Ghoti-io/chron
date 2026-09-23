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
 * The rounding primitives, shared by every type that can be rounded.
 *
 * gchron_instant_round(), gchron_datetime_round() and gchron_zoned_round()
 * all reduce to the same question - a value sits between two boundaries, and
 * a GCHRON_Rounding says which one it becomes - and the reason they share
 * code rather than each being "obvious" is that the obvious version is wrong
 * before the epoch. Written once, armed once.
 */

#ifndef GHOTI_IO_GCHRON_SRC_CORE_ROUND_INTERNAL_H
#define GHOTI_IO_GCHRON_SRC_CORE_ROUND_INTERNAL_H

#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>
#include <stdint.h>
#include "core_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** How many seconds one of the exact units at or above a second is worth. */
static inline int64_t gchron_seconds_per_unit(GCHRON_Unit unit) {
  switch (unit) {
    case GCHRON_UNIT_SECOND: return 1;
    case GCHRON_UNIT_MINUTE: return 60;
    case GCHRON_UNIT_HOUR: return GCHRON_SECONDS_PER_HOUR;
    case GCHRON_UNIT_DAY: return GCHRON_SECONDS_PER_DAY;
    default: return 0;
  }
}

/** How many nanoseconds one of the sub-second units is worth. */
static inline int64_t gchron_subsecond_nanos(GCHRON_Unit unit) {
  switch (unit) {
    case GCHRON_UNIT_NANOSECOND: return 1;
    case GCHRON_UNIT_MICROSECOND: return 1000;
    case GCHRON_UNIT_MILLISECOND: return 1000000;
    default: return 0;
  }
}

/**
 * How many of @p unit make up the next unit above it, for deciding whether an
 * increment is legal.
 *
 * Buckets have to tile the unit above them or the boundary the caller is
 * imagining does not exist: "every 7 minutes" describes marks that drift
 * through every hour, so two callers rounding the same timestamp the same way
 * would agree while both being somewhere neither meant.
 *
 * GCHRON_UNIT_DAY returns 1 and so admits an increment of 1 only. A week is
 * seven days everywhere, but weeks do not tile a month or a year, and a day
 * count that crosses a month boundary is a calendar's business rather than a
 * divisor's. The calendar units are handled by their own code and never reach
 * this.
 */
static inline int64_t gchron_next_unit_ratio(GCHRON_Unit unit) {
  switch (unit) {
    case GCHRON_UNIT_NANOSECOND: return 1000;
    case GCHRON_UNIT_MICROSECOND: return 1000;
    case GCHRON_UNIT_MILLISECOND: return 1000;
    case GCHRON_UNIT_SECOND: return 60;
    case GCHRON_UNIT_MINUTE: return 60;
    case GCHRON_UNIT_HOUR: return 24;
    case GCHRON_UNIT_DAY: return 1;
    default: return 0;
  }
}

/**
 * Decide whether a value strictly between two boundaries moves up to the
 * upper one.
 *
 * The caller has already put the value into floor form: @p frac is how far it
 * sits past the lower boundary, @p whole is the distance between the two, and
 * @p frac is strictly between zero and @p whole. In that form FLOOR is "stay"
 * and CEIL is "move" with no sign analysis at all, which is the reason for
 * decomposing this way - the sign re-enters only for the two modes defined in
 * terms of zero rather than in terms of the timeline.
 *
 * @param negative Whether the value being rounded is before the origin.
 * @param lower_is_even Parity of the lower boundary's bucket number, for
 *   GCHRON_ROUND_HALF_EVEN.
 * @param ok Cleared when the mode refuses or is not a mode.
 * @return `true` to move up to the upper boundary.
 */
static inline bool gchron_round_moves_up(int64_t frac, int64_t whole,
    bool negative, bool lower_is_even, GCHRON_Rounding mode, bool * ok) {
  /*
   * Compared as `frac` against `whole - frac` rather than `2 * frac` against
   * `whole`, because doubling overflows: `whole` can be a whole day in
   * nanoseconds and `frac` is very nearly that.
   */
  const int64_t rest = whole - frac;

  *ok = true;
  switch (mode) {
    case GCHRON_ROUND_REJECT:
      *ok = false;
      return false;
    case GCHRON_ROUND_TRUNCATE:
      /* Toward zero: down after the origin, up before it. */
      return negative;
    case GCHRON_ROUND_FLOOR:
      return false;
    case GCHRON_ROUND_CEIL:
      return true;
    case GCHRON_ROUND_HALF_EXPAND:
      if (frac != rest) {
        return frac > rest;
      }
      /* A tie goes away from zero, which is downward before the origin. */
      return !negative;
    case GCHRON_ROUND_HALF_EVEN:
      if (frac != rest) {
        return frac > rest;
      }
      return !lower_is_even;
    default:
      *ok = false;
      return false;
  }
}

/** Turn a refusal from gchron_round_moves_up() into a result code. */
static inline GCHRON_Result gchron_round_refusal(GCHRON_Rounding mode) {
  return mode == GCHRON_ROUND_REJECT ? GCHRON_ERR_RANGE : GCHRON_ERR_INVALID;
}

/**
 * Check that a unit and increment can divide a timeline at all.
 *
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
static inline GCHRON_Result gchron_round_check_exact(GCHRON_Unit smallest,
    int64_t increment) {
  int64_t ratio;

  if (smallest <= GCHRON_UNIT_UNSPECIFIED || smallest > GCHRON_UNIT_DAY) {
    return GCHRON_ERR_INVALID;
  }
  if (increment <= 0) {
    return GCHRON_ERR_INVALID;
  }
  ratio = gchron_next_unit_ratio(smallest);
  if (ratio <= 0 || increment > ratio || (ratio % increment) != 0) {
    return GCHRON_ERR_INVALID;
  }
  return GCHRON_OK;
}

/**
 * Round a count of seconds and nanoseconds from an origin to a multiple of an
 * exact unit.
 *
 * @param sec Seconds from the origin; may be negative.
 * @param nsec 0..999999999, always, whatever the sign of @p sec - so the
 *   value is negative exactly when @p sec is.
 * @param out_sec Receives the rounded seconds.
 * @param out_nsec Receives 0..999999999.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
static inline GCHRON_Result gchron_round_exact(int64_t sec, int32_t nsec,
    GCHRON_Unit smallest, int64_t increment, GCHRON_Rounding mode,
    int64_t * out_sec, int64_t * out_nsec) {
  const bool negative = sec < 0;
  bool moves_up;
  bool ok;
  GCHRON_Result result = gchron_round_check_exact(smallest, increment);

  if (result != GCHRON_OK) {
    return result;
  }

  if (smallest < GCHRON_UNIT_SECOND) {
    const int64_t scale = gchron_subsecond_nanos(smallest) * increment;
    const int64_t bucket = (int64_t)nsec / scale;
    const int64_t frac = (int64_t)nsec - bucket * scale;
    const int64_t per_second = GCHRON_NANOS_PER_SECOND / scale;
    /*
     * The bucket number counts from the origin rather than from this second,
     * so its parity has to include the seconds.
     */
    const bool lower_is_even =
        (((sec & 1) & (per_second & 1)) ^ (bucket & 1)) == 0;

    if (frac == 0) {
      *out_sec = sec;
      *out_nsec = nsec;
      return GCHRON_OK;
    }
    moves_up = gchron_round_moves_up(frac, scale, negative, lower_is_even,
        mode, &ok);
    if (!ok) {
      return gchron_round_refusal(mode);
    }
    *out_nsec = (bucket + (moves_up ? 1 : 0)) * scale;
    /*
     * Rounding up out of the last bucket of a second carries. Returning
     * 1000000000 here was caught by gchron_instant_normalize() on one caller
     * and produced an invalid GCHRON_Time on the other, which is the reason
     * the carry lives in here rather than in each caller: a normalisation
     * that only one path happens to perform is a normalisation the next path
     * will not.
     */
    if (*out_nsec >= GCHRON_NANOS_PER_SECOND) {
      *out_nsec -= GCHRON_NANOS_PER_SECOND;
      if (!gchron_add_i64(sec, 1, out_sec)) {
        return GCHRON_ERR_RANGE;
      }
      return GCHRON_OK;
    }
    *out_sec = sec;
    return GCHRON_OK;
  }

  {
    const int64_t unit_seconds = gchron_seconds_per_unit(smallest);
    int64_t scale;
    int64_t bucket;
    int64_t rem;
    int64_t frac;
    int64_t whole;

    if (!gchron_mul_i64(unit_seconds, increment, &scale)) {
      return GCHRON_ERR_RANGE;
    }
    /*
     * `frac` is measured in nanoseconds, so a whole bucket has to fit in an
     * int64. Every legal increment is far inside this; the check is for the
     * arithmetic, not for the caller.
     */
    if (scale > INT64_MAX / GCHRON_NANOS_PER_SECOND) {
      return GCHRON_ERR_RANGE;
    }
    whole = scale * GCHRON_NANOS_PER_SECOND;

    bucket = gchron_floor_div(sec, scale);
    rem = sec - bucket * scale;
    frac = rem * GCHRON_NANOS_PER_SECOND + (int64_t)nsec;
    if (frac == 0) {
      *out_sec = sec;
      *out_nsec = nsec;
      return GCHRON_OK;
    }
    moves_up = gchron_round_moves_up(frac, whole, negative, (bucket & 1) == 0,
        mode, &ok);
    if (!ok) {
      return gchron_round_refusal(mode);
    }
    if (!gchron_add_i64(bucket, moves_up ? 1 : 0, &bucket)) {
      return GCHRON_ERR_RANGE;
    }
    if (!gchron_mul_i64(bucket, scale, out_sec)) {
      return GCHRON_ERR_RANGE;
    }
    *out_nsec = 0;
    return GCHRON_OK;
  }
}

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_SRC_CORE_ROUND_INTERNAL_H
