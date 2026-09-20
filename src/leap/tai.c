/**
 * @file
 *
 * Converting between UTC and TAI.
 *
 * The arithmetic is one addition. What makes it worth a file is the second
 * that Unix time cannot name: across a positive leap second two distinct TAI
 * instants map to the same GCHRON_Instant, and every function here has to say
 * which of the two it means.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/leap.h>
#include <ghoti.io/chron/macros.h>
#include <string.h>

#include "../core/core_internal.h"
#include "leap_internal.h"

GCHRON_Result gchron_tai_from_instant(const GCHRON_LeapTable * table,
    GCHRON_Instant utc, bool leap_second, GCHRON_TaiInstant * out) {
  GCHRON_Result result;
  size_t index;
  int64_t sec;

  if (table == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (utc.nsec < 0 || utc.nsec >= GCHRON_NANOS_PER_SECOND) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_leap_check_span(table, utc.sec);
  if (result != GCHRON_OK) {
    return result;
  }
  index = gchron_leap_index_at(table, utc.sec);

  {
    /*
     * A negative leap second *removes* a second: the clock goes from
     * 23:59:58 straight to 00:00:00, and the Unix value in between names no
     * instant at all. Converting it anyway would produce the same TAI second
     * as its successor - two distinct UTC values colliding on one TAI value,
     * which is exactly what a scale with no repeated seconds is for.
     *
     * GCHRON_ERR_GAP, which is what this library already says about a civil
     * time inside a daylight-saving gap, because it is the same question: a
     * label that names no instant. `fuzz_leap` found this by synthesising a
     * negative leap - no oracle could, because none has ever been issued.
     */
    size_t next = index + 1;
    if (next < table->count && table->entries[next].negative
        && utc.sec == table->entries[next].at.sec - 1) {
      return GCHRON_ERR_GAP;
    }
  }

  if (leap_second) {
    /*
     * The caller is naming the repeated second - the one a timestamp spells
     * `:60`. It exists only at the very end of a day the table says gained a
     * second, so this both places it and checks the claim: a `:60` on a day
     * with no leap second is a caller error, not a value to convert.
     */
    size_t next = index + 1;
    if (next >= table->count
        || table->entries[next].at.sec != utc.sec + 1
        || table->entries[next].tai_minus_utc
            <= table->entries[index].tai_minus_utc) {
      return GCHRON_ERR_INVALID;
    }
    /* One past the last ordinary second of the day. */
    if (!gchron_add_i64(utc.sec, table->entries[index].tai_minus_utc, &sec)
        || !gchron_add_i64(sec, 1, &sec)) {
      return GCHRON_ERR_RANGE;
    }
  }
  else if (!gchron_add_i64(utc.sec, table->entries[index].tai_minus_utc,
               &sec)) {
    return GCHRON_ERR_RANGE;
  }

  out->sec = sec;
  out->nsec = utc.nsec;
  return GCHRON_OK;
}

GCHRON_Result gchron_tai_to_instant(const GCHRON_LeapTable * table,
    GCHRON_TaiInstant tai, GCHRON_Instant * out, bool * leap_second) {
  size_t i;
  int64_t candidate = 0;
  int32_t offset = 0;
  bool inside_leap = false;

  if (table == NULL || out == NULL || table->count == 0) {
    return GCHRON_ERR_INVALID;
  }
  if (tai.nsec < 0 || tai.nsec >= GCHRON_NANOS_PER_SECOND) {
    return GCHRON_ERR_INVALID;
  }

  /*
   * Walk backwards for the last row whose start, expressed in TAI, is at or
   * before this instant. The table is short - twenty-eight rows, and it will
   * not grow much given the 27th CGPM is retiring the practice - so a linear
   * scan is both simpler to read and faster than a search.
   */
  offset = table->entries[0].tai_minus_utc;
  for (i = table->count; i-- > 0;) {
    int64_t row_tai;
    if (!gchron_add_i64(table->entries[i].at.sec,
            table->entries[i].tai_minus_utc, &row_tai)) {
      return GCHRON_ERR_RANGE;
    }
    if (tai.sec >= row_tai) {
      offset = table->entries[i].tai_minus_utc;
      break;
    }
    if (i > 0) {
      int64_t previous_tai;
      if (!gchron_add_i64(table->entries[i].at.sec,
              table->entries[i - 1].tai_minus_utc, &previous_tai)) {
        return GCHRON_ERR_RANGE;
      }
      /*
       * Between the two: this TAI second is inside the second the table
       * inserted, which UTC spells `:60` and Unix time gives the same number
       * as the second before it.
       */
      if (tai.sec >= previous_tai) {
        offset = table->entries[i - 1].tai_minus_utc;
        inside_leap = true;
        break;
      }
    }
  }

  if (!gchron_sub_i64(tai.sec, offset, &candidate)) {
    return GCHRON_ERR_RANGE;
  }
  if (inside_leap) {
    /* Unix time names it as the last ordinary second of the day. */
    candidate -= 1;
  }

  {
    GCHRON_Result result = gchron_leap_check_span(table, candidate);
    if (result != GCHRON_OK) {
      return result;
    }
  }

  out->sec = candidate;
  out->nsec = tai.nsec;
  if (leap_second != NULL) {
    *leap_second = inside_leap;
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_leap_elapsed(const GCHRON_LeapTable * table,
    GCHRON_Instant from, GCHRON_Instant to, GCHRON_Duration * out) {
  GCHRON_TaiInstant a;
  GCHRON_TaiInstant b;
  GCHRON_Result result;
  int64_t seconds = 0;
  int64_t nanos = 0;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_tai_from_instant(table, from, false, &a);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_tai_from_instant(table, to, false, &b);
  if (result != GCHRON_OK) {
    return result;
  }

  if (!gchron_sub_i64(b.sec, a.sec, &seconds)) {
    return GCHRON_ERR_RANGE;
  }
  nanos = (int64_t)b.nsec - (int64_t)a.nsec;
  if (nanos < 0) {
    nanos += GCHRON_NANOS_PER_SECOND;
    if (!gchron_sub_i64(seconds, 1, &seconds)) {
      return GCHRON_ERR_RANGE;
    }
  }

  memset(out, 0, sizeof(*out));
  out->seconds = seconds;
  out->nsec = (int32_t)nanos;
  /*
   * Seconds and nanoseconds only: an elapsed time that counted leap seconds
   * and then reported itself in months would be answering two questions at
   * once, and the second answer would be the wrong one (mistake M9).
   */
  return GCHRON_OK;
}
