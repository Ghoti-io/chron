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
 * Instants: a point on the timeline, in Unix seconds and nanoseconds.
 *
 * Reference: design.md section 5.1, on why the count has no leap seconds in
 * it, and what the library does about them instead.
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>
#include <stdio.h>

#include "../core/core_internal.h"

GCHRON_Result gchron_instant_create(int64_t sec, int32_t nsec,
    GCHRON_Instant * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (nsec < 0 || nsec >= GCHRON_NANOS_PER_SECOND) {
    /*
     * Not a carry. Which way a carry should go for a negative `sec` is
     * exactly what callers get wrong, so the function that carries has its
     * own name and the invariant is stated rather than repaired.
     */
    return GCHRON_ERR_INVALID;
  }
  out->sec = sec;
  out->nsec = nsec;
  return GCHRON_OK;
}

GCHRON_Result gchron_instant_normalize(int64_t sec, int64_t nanos,
    GCHRON_Instant * out) {
  int64_t carry;
  int64_t total_sec;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  carry = gchron_floor_div(nanos, GCHRON_NANOS_PER_SECOND);
  if (!gchron_add_i64(sec, carry, &total_sec)) {
    return GCHRON_ERR_RANGE;
  }
  out->sec = total_sec;
  out->nsec = (int32_t)gchron_floor_mod(nanos, GCHRON_NANOS_PER_SECOND);
  return GCHRON_OK;
}

bool gchron_instant_is_valid(const GCHRON_Instant * i) {
  if (i == NULL) {
    return false;
  }
  return i->nsec >= 0 && i->nsec < GCHRON_NANOS_PER_SECOND;
}

GCHRON_Result gchron_instant_to_epoch_day(const GCHRON_Instant * i,
    int64_t * out_epoch_day, int64_t * out_nanos_of_day) {
  int64_t day;
  int64_t second_of_day;

  if (!gchron_instant_is_valid(i)) {
    return GCHRON_ERR_INVALID;
  }
  day = gchron_floor_div(i->sec, GCHRON_SECONDS_PER_DAY);
  second_of_day = gchron_floor_mod(i->sec, GCHRON_SECONDS_PER_DAY);
  if (out_epoch_day != NULL) {
    *out_epoch_day = day;
  }
  if (out_nanos_of_day != NULL) {
    *out_nanos_of_day = second_of_day * GCHRON_NANOS_PER_SECOND + i->nsec;
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_instant_to_utc(const GCHRON_Instant * i,
    GCHRON_DateTime * out) {
  int64_t day;
  int64_t nanos_of_day;
  GCHRON_Date date;
  GCHRON_Time time;
  GCHRON_Result result;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_instant_to_epoch_day(i, &day, &nanos_of_day);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_date_from_epoch_day(day, &date);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_time_from_nanos_of_day(nanos_of_day, &time);
  if (result != GCHRON_OK) {
    return result;
  }
  out->date = date;
  out->time = time;
  return GCHRON_OK;
}

GCHRON_Result gchron_instant_from_utc(const GCHRON_DateTime * dt,
    GCHRON_Instant * out) {
  int64_t epoch_day;
  int64_t nanos_of_day;
  int64_t seconds;
  GCHRON_Result result;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_date_to_epoch_day(&dt->date, &epoch_day);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_time_to_nanos_of_day(&dt->time, &nanos_of_day);
  if (result != GCHRON_OK) {
    return result;
  }
  if (!gchron_mul_i64(epoch_day, GCHRON_SECONDS_PER_DAY, &seconds)) {
    return GCHRON_ERR_RANGE;
  }
  if (!gchron_add_i64(seconds, nanos_of_day / GCHRON_NANOS_PER_SECOND,
          &seconds)) {
    return GCHRON_ERR_RANGE;
  }
  out->sec = seconds;
  out->nsec = (int32_t)(nanos_of_day % GCHRON_NANOS_PER_SECOND);
  return GCHRON_OK;
}

GCHRON_Result gchron_instant_add(const GCHRON_Instant * i,
    const GCHRON_Duration * d, GCHRON_Instant * out) {
  int64_t seconds;
  int32_t nanos;
  int64_t sum;
  GCHRON_Result result;

  if (out == NULL || !gchron_instant_is_valid(i)) {
    return GCHRON_ERR_INVALID;
  }
  /*
   * gchron_duration_to_exact_seconds() refuses a duration with calendar
   * units, which is the whole of mistake M9: a month has no length in
   * seconds, so adding one to a point on the timeline is a question with no
   * answer rather than a question whose answer is thirty days.
   */
  result = gchron_duration_to_exact_seconds(d, &seconds, &nanos);
  if (result != GCHRON_OK) {
    return result;
  }
  if (!gchron_add_i64(i->sec, seconds, &sum)) {
    return GCHRON_ERR_RANGE;
  }
  return gchron_instant_normalize(sum, (int64_t)i->nsec + nanos, out);
}

GCHRON_Result gchron_instant_subtract(const GCHRON_Instant * i,
    const GCHRON_Duration * d, GCHRON_Instant * out) {
  int64_t seconds;
  int32_t nanos;
  int64_t sum;
  GCHRON_Result result;

  if (out == NULL || !gchron_instant_is_valid(i)) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_duration_to_exact_seconds(d, &seconds, &nanos);
  if (result != GCHRON_OK) {
    return result;
  }
  if (!gchron_sub_i64(i->sec, seconds, &sum)) {
    return GCHRON_ERR_RANGE;
  }
  return gchron_instant_normalize(sum, (int64_t)i->nsec - nanos, out);
}

GCHRON_Result gchron_instant_until(const GCHRON_Instant * from,
    const GCHRON_Instant * to, GCHRON_Duration * out) {
  int64_t seconds;
  int64_t nanos;

  if (out == NULL || !gchron_instant_is_valid(from)
      || !gchron_instant_is_valid(to)) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_sub_i64(to->sec, from->sec, &seconds)) {
    return GCHRON_ERR_RANGE;
  }
  nanos = (int64_t)to->nsec - (int64_t)from->nsec;
  if (nanos < 0) {
    if (!gchron_sub_i64(seconds, 1, &seconds)) {
      return GCHRON_ERR_RANGE;
    }
    nanos += GCHRON_NANOS_PER_SECOND;
  }
  return gchron_duration_from_exact_seconds(seconds, (int32_t)nanos, out);
}

/*--------------------------------------------------------------------------*
 * Foreign integer encodings of Unix time
 *--------------------------------------------------------------------------*/

GCHRON_Result gchron_instant_from_unix_seconds(int64_t seconds,
    GCHRON_Instant * out) {
  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  out->sec = seconds;
  out->nsec = 0;
  return GCHRON_OK;
}

/** Split a scaled count into whole seconds and a nanosecond remainder. */
static GCHRON_Result from_scaled(int64_t value, int64_t per_second,
    GCHRON_Instant * out) {
  int64_t nanos_per_unit = GCHRON_NANOS_PER_SECOND / per_second;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  out->sec = gchron_floor_div(value, per_second);
  out->nsec = (int32_t)(gchron_floor_mod(value, per_second) * nanos_per_unit);
  return GCHRON_OK;
}

GCHRON_Result gchron_instant_from_unix_millis(int64_t millis,
    GCHRON_Instant * out) {
  return from_scaled(millis, 1000, out);
}

GCHRON_Result gchron_instant_from_unix_micros(int64_t micros,
    GCHRON_Instant * out) {
  return from_scaled(micros, 1000000, out);
}

GCHRON_Result gchron_instant_from_unix_nanos(int64_t nanos,
    GCHRON_Instant * out) {
  return from_scaled(nanos, GCHRON_NANOS_PER_SECOND, out);
}

GCHRON_Result gchron_instant_to_unix_seconds(const GCHRON_Instant * i,
    int64_t * out) {
  if (out == NULL || !gchron_instant_is_valid(i)) {
    return GCHRON_ERR_INVALID;
  }
  *out = i->sec;
  return GCHRON_OK;
}

/** Scale an instant into a count per second, rounding towards -infinity. */
static GCHRON_Result to_scaled(const GCHRON_Instant * i, int64_t per_second,
    int64_t * out) {
  int64_t nanos_per_unit = GCHRON_NANOS_PER_SECOND / per_second;
  int64_t scaled;

  if (out == NULL || !gchron_instant_is_valid(i)) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_mul_i64(i->sec, per_second, &scaled)) {
    return GCHRON_ERR_RANGE;
  }
  if (!gchron_add_i64(scaled, i->nsec / nanos_per_unit, &scaled)) {
    return GCHRON_ERR_RANGE;
  }
  *out = scaled;
  return GCHRON_OK;
}

GCHRON_Result gchron_instant_to_unix_millis(const GCHRON_Instant * i,
    int64_t * out) {
  return to_scaled(i, 1000, out);
}

GCHRON_Result gchron_instant_to_unix_micros(const GCHRON_Instant * i,
    int64_t * out) {
  return to_scaled(i, 1000000, out);
}

GCHRON_Result gchron_instant_to_unix_nanos(const GCHRON_Instant * i,
    int64_t * out) {
  return to_scaled(i, GCHRON_NANOS_PER_SECOND, out);
}

GCHRON_Result gchron_instant_as_double(const GCHRON_Instant * i, double * out) {
  if (out == NULL || !gchron_instant_is_valid(i)) {
    return GCHRON_ERR_INVALID;
  }
  *out = (double)i->sec + (double)i->nsec / 1e9;
  return GCHRON_OK;
}

int gchron_instant_compare(const GCHRON_Instant * a, const GCHRON_Instant * b) {
  if (a == NULL || b == NULL) {
    return (a == b) ? 0 : (a == NULL ? -1 : 1);
  }
  if (a->sec != b->sec) {
    return a->sec < b->sec ? -1 : 1;
  }
  if (a->nsec != b->nsec) {
    return a->nsec < b->nsec ? -1 : 1;
  }
  return 0;
}

void gchron_instant_dump(const GCHRON_Instant * i, FILE * stream) {
  if (stream == NULL) {
    return;
  }
  if (i == NULL) {
    fprintf(stream, "GCHRON_Instant(NULL)\n");
    return;
  }
  fprintf(stream, "GCHRON_Instant(%lld.%09d)%s\n", (long long)i->sec, i->nsec,
      gchron_instant_is_valid(i) ? "" : " [invalid]");
}

/*--------------------------------------------------------------------------*
 * Rounding
 *--------------------------------------------------------------------------*/

/** How many seconds one of the exact units at or above a second is worth. */
static int64_t seconds_per_unit(GCHRON_Unit unit) {
  switch (unit) {
    case GCHRON_UNIT_SECOND: return 1;
    case GCHRON_UNIT_MINUTE: return 60;
    case GCHRON_UNIT_HOUR: return GCHRON_SECONDS_PER_HOUR;
    case GCHRON_UNIT_DAY: return GCHRON_SECONDS_PER_DAY;
    default: return 0;
  }
}

/** How many nanoseconds one of the sub-second units is worth. */
static int64_t subsecond_nanos(GCHRON_Unit unit) {
  switch (unit) {
    case GCHRON_UNIT_NANOSECOND: return 1;
    case GCHRON_UNIT_MICROSECOND: return 1000;
    case GCHRON_UNIT_MILLISECOND: return 1000000;
    default: return 0;
  }
}

/**
 * How many of @p unit make up the next unit above it.
 *
 * This is what makes an increment legal or not. Buckets have to tile the unit
 * above them or the boundary the caller is imagining does not exist: asking
 * for "every 7 minutes" describes marks that drift through every hour, so
 * two callers rounding the same timestamp with the same arguments would agree
 * while both being somewhere the caller never meant. GCHRON_UNIT_DAY has no
 * next unit here - a week is a calendar's business and an instant has no
 * calendar - so it returns 1 and admits only an increment of 1.
 */
static int64_t next_unit_ratio(GCHRON_Unit unit) {
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
 * decomposing this way - the sign only re-enters for the two modes that are
 * defined in terms of zero rather than in terms of the timeline.
 *
 * @param negative Whether the value being rounded is before the epoch.
 * @param lower_is_even Parity of the lower boundary's bucket number, for
 *   GCHRON_ROUND_HALF_EVEN.
 * @return `true` to move up, `false` to stay, and `false` with @p ok cleared
 *   for GCHRON_ROUND_REJECT.
 */
static bool round_moves_up(int64_t frac, int64_t whole, bool negative,
    bool lower_is_even, GCHRON_Rounding mode, bool * ok) {
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
      /* Toward zero: down above the epoch, up below it. */
      return negative;
    case GCHRON_ROUND_FLOOR:
      return false;
    case GCHRON_ROUND_CEIL:
      return true;
    case GCHRON_ROUND_HALF_EXPAND:
      if (frac != rest) {
        return frac > rest;
      }
      /* A tie goes away from zero, which is down when before the epoch. */
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

GCHRON_Result gchron_instant_round(const GCHRON_Instant * in,
    GCHRON_Unit smallest, int64_t increment, GCHRON_Rounding mode,
    GCHRON_Instant * out) {
  int64_t ratio;
  bool negative;
  bool moves_up;
  bool ok;

  if (out == NULL || !gchron_instant_is_valid(in)) {
    return GCHRON_ERR_INVALID;
  }
  if (smallest <= GCHRON_UNIT_UNSPECIFIED || smallest > GCHRON_UNIT_DAY) {
    /*
     * A week, a month and a year are refused rather than approximated. The
     * note this was written from is explicit that they must not fall through
     * to a nanosecond count: a caller who asks an instant to round to a month
     * has asked a question with no answer, and 30 days is not it.
     */
    return GCHRON_ERR_INVALID;
  }
  if (increment <= 0) {
    return GCHRON_ERR_INVALID;
  }
  ratio = next_unit_ratio(smallest);
  if (ratio <= 0 || increment > ratio || (ratio % increment) != 0) {
    return GCHRON_ERR_INVALID;
  }

  /* `nsec` is always 0..999999999, so the sign lives entirely in `sec`. */
  negative = in->sec < 0;

  if (smallest < GCHRON_UNIT_SECOND) {
    const int64_t scale = subsecond_nanos(smallest) * increment;
    const int64_t bucket = (int64_t)in->nsec / scale;
    const int64_t frac = (int64_t)in->nsec - bucket * scale;
    int64_t nanos;

    if (frac == 0) {
      *out = *in;
      return GCHRON_OK;
    }
    /*
     * The bucket number counts from the epoch, not from this second, so its
     * parity has to include the seconds. `per_second` is exact because the
     * increment divides its unit and the unit divides a second.
     */
    {
      const int64_t per_second = GCHRON_NANOS_PER_SECOND / scale;
      const bool lower_is_even =
          (((in->sec & 1) & (per_second & 1)) ^ (bucket & 1)) == 0;

      moves_up = round_moves_up(frac, scale, negative, lower_is_even, mode,
          &ok);
    }
    if (!ok) {
      return mode == GCHRON_ROUND_REJECT ? GCHRON_ERR_RANGE
                                         : GCHRON_ERR_INVALID;
    }
    nanos = (bucket + (moves_up ? 1 : 0)) * scale;
    return gchron_instant_normalize(in->sec, nanos, out);
  }

  {
    const int64_t unit_seconds = seconds_per_unit(smallest);
    int64_t scale;
    int64_t bucket;
    int64_t rem;
    int64_t frac;
    int64_t whole;
    int64_t seconds;

    if (!gchron_mul_i64(unit_seconds, increment, &scale)) {
      return GCHRON_ERR_RANGE;
    }
    /*
     * `frac` is measured in nanoseconds, so the bucket has to be small enough
     * that a whole one fits in an int64. Every legal increment here is far
     * inside this; the check is for the arithmetic, not for the caller.
     */
    if (scale > INT64_MAX / GCHRON_NANOS_PER_SECOND) {
      return GCHRON_ERR_RANGE;
    }
    whole = scale * GCHRON_NANOS_PER_SECOND;

    bucket = gchron_floor_div(in->sec, scale);
    rem = in->sec - bucket * scale;
    frac = rem * GCHRON_NANOS_PER_SECOND + (int64_t)in->nsec;
    if (frac == 0) {
      *out = *in;
      return GCHRON_OK;
    }
    moves_up = round_moves_up(frac, whole, negative, (bucket & 1) == 0, mode,
        &ok);
    if (!ok) {
      return mode == GCHRON_ROUND_REJECT ? GCHRON_ERR_RANGE
                                         : GCHRON_ERR_INVALID;
    }
    if (!gchron_add_i64(bucket, moves_up ? 1 : 0, &bucket)) {
      return GCHRON_ERR_RANGE;
    }
    if (!gchron_mul_i64(bucket, scale, &seconds)) {
      return GCHRON_ERR_RANGE;
    }
    return gchron_instant_normalize(seconds, 0, out);
  }
}
