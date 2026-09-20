/**
 * @file
 *
 * Civil time with a fixed offset from UTC.
 *
 * Reference: RFC 3339 sections 4.3 and 5.6.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/offset.h>
#include <stdio.h>

#include "../core/core_internal.h"

/** Whether an offset is one RFC 3339 can spell, and this library hold. */
static bool offset_in_range(int32_t offset_sec) {
  return offset_sec > -GCHRON_OFFSET_LIMIT_SECONDS
      && offset_sec < GCHRON_OFFSET_LIMIT_SECONDS;
}

GCHRON_Result gchron_offset_create(const GCHRON_DateTime * civil,
    int32_t offset_sec, bool offset_unknown, GCHRON_OffsetDateTime * out) {
  if (out == NULL || !gchron_datetime_is_valid(civil)) {
    return GCHRON_ERR_INVALID;
  }
  if (!offset_in_range(offset_sec)) {
    return GCHRON_ERR_INVALID;
  }
  if (offset_unknown && offset_sec != 0) {
    /*
     * "-00:00" is the only spelling that means *unknown*, and it is
     * necessarily zero. A non-zero offset marked unknown is a state no text
     * can produce and none can express.
     */
    return GCHRON_ERR_INVALID;
  }
  out->civil = *civil;
  out->offset_sec = offset_sec;
  out->offset_unknown = offset_unknown;
  return GCHRON_OK;
}

bool gchron_offset_is_valid(const GCHRON_OffsetDateTime * odt) {
  if (odt == NULL) {
    return false;
  }
  if (!gchron_datetime_is_valid(&odt->civil)) {
    return false;
  }
  if (!offset_in_range(odt->offset_sec)) {
    return false;
  }
  return !(odt->offset_unknown && odt->offset_sec != 0);
}

bool gchron_offset_time_is_valid(const GCHRON_OffsetTime * ot) {
  if (ot == NULL) {
    return false;
  }
  if (!gchron_time_is_valid(&ot->time)) {
    return false;
  }
  if (!offset_in_range(ot->offset_sec)) {
    return false;
  }
  return !(ot->offset_unknown && ot->offset_sec != 0);
}

GCHRON_Result gchron_offset_to_instant(const GCHRON_OffsetDateTime * odt,
    GCHRON_Instant * out) {
  GCHRON_Instant local;
  GCHRON_Result result;
  int64_t sec;

  if (out == NULL || !gchron_offset_is_valid(odt)) {
    return GCHRON_ERR_INVALID;
  }
  /*
   * The civil reading is what a clock that many seconds ahead of UTC showed,
   * so the instant is that reading treated as UTC, less the offset. An
   * explicit offset can neither be ambiguous nor fall in a gap; those are a
   * zone's failures, and zone.h is where they are reported.
   */
  result = gchron_instant_from_utc(&odt->civil, &local);
  if (result != GCHRON_OK) {
    return result;
  }
  if (!gchron_sub_i64(local.sec, odt->offset_sec, &sec)) {
    return GCHRON_ERR_RANGE;
  }
  out->sec = sec;
  out->nsec = local.nsec;
  return GCHRON_OK;
}

GCHRON_Result gchron_offset_from_instant(const GCHRON_Instant * i,
    int32_t offset_sec, bool offset_unknown, GCHRON_OffsetDateTime * out) {
  GCHRON_Instant shifted;
  GCHRON_DateTime civil;
  GCHRON_Result result;

  if (out == NULL || !gchron_instant_is_valid(i)) {
    return GCHRON_ERR_INVALID;
  }
  if (!offset_in_range(offset_sec)) {
    return GCHRON_ERR_INVALID;
  }
  if (offset_unknown && offset_sec != 0) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_add_i64(i->sec, offset_sec, &shifted.sec)) {
    return GCHRON_ERR_RANGE;
  }
  shifted.nsec = i->nsec;
  result = gchron_instant_to_utc(&shifted, &civil);
  if (result != GCHRON_OK) {
    return result;
  }
  out->civil = civil;
  out->offset_sec = offset_sec;
  out->offset_unknown = offset_unknown;
  return GCHRON_OK;
}

GCHRON_Result gchron_offset_with_offset(const GCHRON_OffsetDateTime * odt,
    int32_t offset_sec, GCHRON_OffsetDateTime * out) {
  GCHRON_Instant instant;
  GCHRON_Result result;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_offset_to_instant(odt, &instant);
  if (result != GCHRON_OK) {
    return result;
  }
  /*
   * The flag does not travel: it says how *this* text wrote its offset, and
   * rewriting the offset is exactly the operation that makes it no longer
   * true. Rewriting to zero does not make the offset unknown again.
   */
  return gchron_offset_from_instant(&instant, offset_sec, false, out);
}

int gchron_offset_compare(const GCHRON_OffsetDateTime * a,
    const GCHRON_OffsetDateTime * b) {
  GCHRON_Instant ia;
  GCHRON_Instant ib;

  if (a == NULL || b == NULL) {
    return (a == b) ? 0 : (a == NULL ? -1 : 1);
  }
  /*
   * By the instant, so that 01:30-05:00 and 06:30Z compare equal: they are
   * the same moment, written twice. gchron_offset_identical() is the other
   * question, and it has a different name for the reason java.time's `equals`
   * and `isEqual` needed one (design.md, mistake M16).
   *
   * An invalid value has no instant; sorting it before every valid one keeps
   * this a total order rather than something a sort can crash on.
   */
  if (gchron_offset_to_instant(a, &ia) != GCHRON_OK) {
    return (gchron_offset_to_instant(b, &ib) != GCHRON_OK) ? 0 : -1;
  }
  if (gchron_offset_to_instant(b, &ib) != GCHRON_OK) {
    return 1;
  }
  return gchron_instant_compare(&ia, &ib);
}

bool gchron_offset_identical(const GCHRON_OffsetDateTime * a,
    const GCHRON_OffsetDateTime * b) {
  if (a == NULL || b == NULL) {
    return a == b;
  }
  /*
   * Field by field, and not memcmp: these structs have padding, and two
   * values that agree in every field can differ in the bytes between them.
   */
  return gchron_datetime_compare(&a->civil, &b->civil) == 0
      && a->offset_sec == b->offset_sec
      && a->offset_unknown == b->offset_unknown;
}

bool gchron_offset_time_identical(const GCHRON_OffsetTime * a,
    const GCHRON_OffsetTime * b) {
  if (a == NULL || b == NULL) {
    return a == b;
  }
  return gchron_time_compare(&a->time, &b->time) == 0
      && a->offset_sec == b->offset_sec
      && a->offset_unknown == b->offset_unknown;
}

void gchron_offset_dump(const GCHRON_OffsetDateTime * odt, FILE * stream) {
  if (stream == NULL) {
    return;
  }
  if (odt == NULL) {
    fprintf(stream, "GCHRON_OffsetDateTime(NULL)\n");
    return;
  }
  fprintf(stream,
      "GCHRON_OffsetDateTime(%+011d-%02u-%02u %02u:%02u:%02u.%09d %+d%s)%s\n",
      odt->civil.date.year, (unsigned)odt->civil.date.month,
      (unsigned)odt->civil.date.day, (unsigned)odt->civil.time.hour,
      (unsigned)odt->civil.time.minute, (unsigned)odt->civil.time.second,
      odt->civil.time.nsec, odt->offset_sec,
      odt->offset_unknown ? " unknown" : "",
      gchron_offset_is_valid(odt) ? "" : " [invalid]");
}
