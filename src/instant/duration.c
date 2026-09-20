/**
 * @file
 *
 * Durations: the type's invariants, and the conversions between it and an
 * exact second count.
 *
 * The arithmetic that applies a duration to a calendar - balance, until,
 * rounding - is phase 2 (design.md section 16), and is absent rather than
 * stubbed.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/macros.h>
#include <stddef.h>
#include <stdio.h>

#include "../core/core_internal.h"

/** The sign of one field: -1, 0 or 1. */
static int sign_of(int64_t v) {
  if (v < 0) {
    return -1;
  }
  return v > 0 ? 1 : 0;
}

bool gchron_duration_is_valid(const GCHRON_Duration * d) {
  int seen = 0;
  size_t index;
  int64_t values[7];

  if (d == NULL) {
    return false;
  }
  if (d->nsec < 0 || d->nsec >= GCHRON_NANOS_PER_SECOND) {
    return false;
  }

  values[0] = d->years;
  values[1] = d->months;
  values[2] = d->weeks;
  values[3] = d->days;
  values[4] = d->hours;
  values[5] = d->minutes;
  values[6] = d->seconds;

  for (index = 0; index < 7; ++index) {
    if (values[index] != 0) {
      int s = sign_of(values[index]);
      if (seen == 0) {
        seen = s;
      }
      else if (seen != s) {
        /*
         * "One month minus one day" is not a duration; it is two operations.
         * Writing it as one is how `Jan 31 + P1M-1D` came to mean four
         * different things in four libraries (design.md section 4.1).
         */
        return false;
      }
    }
  }

  /*
   * nsec is always non-negative - the sign of a sub-second duration lives in
   * `seconds`, as it does in GCHRON_Instant - so a negative duration with a
   * non-zero nsec is well-formed and means `seconds` plus `nsec`. What is not
   * well-formed is a positive nsec beside nothing but negative fields with no
   * seconds field to carry the sign, which the check below catches.
   */
  if (d->nsec != 0 && seen < 0 && d->seconds == 0) {
    return false;
  }
  return true;
}

int gchron_duration_sign(const GCHRON_Duration * d) {
  int s;

  if (d == NULL || !gchron_duration_is_valid(d)) {
    return 0;
  }
  s = sign_of(d->years);
  if (s == 0) {
    s = sign_of(d->months);
  }
  if (s == 0) {
    s = sign_of(d->weeks);
  }
  if (s == 0) {
    s = sign_of(d->days);
  }
  if (s == 0) {
    s = sign_of(d->hours);
  }
  if (s == 0) {
    s = sign_of(d->minutes);
  }
  if (s == 0) {
    s = sign_of(d->seconds);
  }
  if (s == 0) {
    s = sign_of(d->nsec);
  }
  return s;
}

bool gchron_duration_has_calendar_units(const GCHRON_Duration * d) {
  if (d == NULL) {
    return false;
  }
  return d->years != 0 || d->months != 0 || d->weeks != 0 || d->days != 0;
}

bool gchron_duration_has_exact_units(const GCHRON_Duration * d) {
  if (d == NULL) {
    return false;
  }
  return d->hours != 0 || d->minutes != 0 || d->seconds != 0 || d->nsec != 0;
}

GCHRON_Result gchron_duration_negate(const GCHRON_Duration * d,
    GCHRON_Duration * out) {
  GCHRON_Duration result;
  int64_t seconds;

  if (out == NULL || !gchron_duration_is_valid(d)) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_neg_i64(d->years, &result.years)
      || !gchron_neg_i64(d->months, &result.months)
      || !gchron_neg_i64(d->weeks, &result.weeks)
      || !gchron_neg_i64(d->days, &result.days)
      || !gchron_neg_i64(d->hours, &result.hours)
      || !gchron_neg_i64(d->minutes, &result.minutes)) {
    return GCHRON_ERR_RANGE;
  }
  /*
   * The sub-second part negates as one quantity, because nsec carries no sign
   * of its own: -(s + n/1e9) is (-s - 1) + (1e9 - n)/1e9 when n is non-zero.
   */
  if (d->nsec == 0) {
    if (!gchron_neg_i64(d->seconds, &seconds)) {
      return GCHRON_ERR_RANGE;
    }
    result.seconds = seconds;
    result.nsec = 0;
  }
  else {
    if (!gchron_neg_i64(d->seconds, &seconds)
        || !gchron_sub_i64(seconds, 1, &seconds)) {
      return GCHRON_ERR_RANGE;
    }
    result.seconds = seconds;
    result.nsec = (int32_t)(GCHRON_NANOS_PER_SECOND - d->nsec);
  }
  *out = result;
  return GCHRON_OK;
}

GCHRON_Result gchron_duration_to_exact_seconds(const GCHRON_Duration * d,
    int64_t * out_seconds, int32_t * out_nanos) {
  int64_t total;
  int64_t scaled;

  if (!gchron_duration_is_valid(d)) {
    return GCHRON_ERR_INVALID;
  }
  if (gchron_duration_has_calendar_units(d)) {
    /*
     * Counting a month in seconds would mean choosing a length for it. The
     * caller asks a civil date-time or a zoned one instead, where the
     * calendar and the zone are there to answer (design.md section 4.1).
     */
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_mul_i64(d->hours, GCHRON_SECONDS_PER_HOUR, &total)) {
    return GCHRON_ERR_RANGE;
  }
  if (!gchron_mul_i64(d->minutes, 60, &scaled)
      || !gchron_add_i64(total, scaled, &total)) {
    return GCHRON_ERR_RANGE;
  }
  if (!gchron_add_i64(total, d->seconds, &total)) {
    return GCHRON_ERR_RANGE;
  }
  if (out_seconds != NULL) {
    *out_seconds = total;
  }
  if (out_nanos != NULL) {
    *out_nanos = d->nsec;
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_duration_from_exact_seconds(int64_t seconds,
    int32_t nanos, GCHRON_Duration * out) {
  if (out == NULL || nanos < 0 || nanos >= GCHRON_NANOS_PER_SECOND) {
    return GCHRON_ERR_INVALID;
  }
  out->years = 0;
  out->months = 0;
  out->weeks = 0;
  out->days = 0;
  out->hours = 0;
  out->minutes = 0;
  out->seconds = seconds;
  out->nsec = nanos;
  return GCHRON_OK;
}

bool gchron_duration_identical(const GCHRON_Duration * a,
    const GCHRON_Duration * b) {
  if (a == NULL || b == NULL) {
    return a == b;
  }
  return a->years == b->years && a->months == b->months && a->weeks == b->weeks
      && a->days == b->days && a->hours == b->hours && a->minutes == b->minutes
      && a->seconds == b->seconds && a->nsec == b->nsec;
}

void gchron_duration_dump(const GCHRON_Duration * d, FILE * stream) {
  if (stream == NULL) {
    return;
  }
  if (d == NULL) {
    fprintf(stream, "GCHRON_Duration(NULL)\n");
    return;
  }
  fprintf(stream,
      "GCHRON_Duration(Y%lld M%lld W%lld D%lld H%lld M%lld S%lld.%09d)%s\n",
      (long long)d->years, (long long)d->months, (long long)d->weeks,
      (long long)d->days, (long long)d->hours, (long long)d->minutes,
      (long long)d->seconds, d->nsec,
      gchron_duration_is_valid(d) ? "" : " [invalid]");
}
