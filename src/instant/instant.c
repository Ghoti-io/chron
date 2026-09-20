/**
 * @file
 *
 * Instants: a point on the timeline, in Unix seconds and nanoseconds.
 *
 * Reference: design.md section 5.1, on why the count has no leap seconds in
 * it, and what the library does about them instead.
 *
 * Copyright 2026 by Corey Pennycuff
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
