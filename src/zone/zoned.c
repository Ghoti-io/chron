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
 * An instant, in a zone.
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/offset.h>
#include <ghoti.io/chron/zone.h>
#include <ghoti.io/chron/zoned.h>
#include <stdio.h>
#include <string.h>

#include "../core/core_internal.h"
#include "../core/round_internal.h"
#include "zone_internal.h"

GCHRON_Result gchron_zoned_from_instant(GCHRON_Instant instant,
    const GCHRON_Zone * zone, GCHRON_ZonedDateTime * out) {
  GCHRON_ZoneInfo info;
  GCHRON_Result result;

  if (zone == NULL || out == NULL || !gchron_instant_is_valid(&instant)) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_zone_offset_at(zone, instant, &info);
  if (result != GCHRON_OK) {
    return result;
  }
  out->instant = instant;
  out->zone = zone;
  out->offset_sec = info.offset_sec;
  return GCHRON_OK;
}

GCHRON_Result gchron_zoned_from_civil(GCHRON_DateTime civil,
    const GCHRON_Zone * zone, GCHRON_Resolve resolve,
    GCHRON_ZonedDateTime * out) {
  GCHRON_CivilOffsets options;
  GCHRON_Result result;
  GCHRON_Instant chosen;

  if (zone == NULL || out == NULL || !gchron_datetime_is_valid(&civil)) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_zone_offsets_for_civil(zone, civil, &options);
  if (result != GCHRON_OK) {
    return result;
  }

  if (options.count == 1) {
    chosen = options.instants[0];
  }
  else if (options.count == 2) {
    /* An overlap: the reading happened twice. */
    switch (resolve) {
      case GCHRON_RESOLVE_EARLIER:
      case GCHRON_RESOLVE_COMPATIBLE:
        chosen = options.instants[0];
        break;
      case GCHRON_RESOLVE_LATER:
        chosen = options.instants[1];
        break;
      case GCHRON_RESOLVE_REJECT:
      default:
        /* On failure the out parameter is not written, per CONVENTIONS.md
         * section 5; gchron_zone_offsets_for_civil() is how a caller who
         * wants to ask the user gets the candidates. */
        return GCHRON_ERR_AMBIGUOUS;
    }
  }
  else {
    /* A gap: the reading never happened. */
    GCHRON_Instant boundary = options.transition;
    switch (resolve) {
      case GCHRON_RESOLVE_EARLIER:
        /* The last instant before the clocks jumped. */
        if (!gchron_sub_i64(boundary.sec, 1, &chosen.sec)) {
          return GCHRON_ERR_RANGE;
        }
        chosen.nsec = civil.time.nsec;
        break;

      case GCHRON_RESOLVE_LATER:
        /* The first instant after them. */
        chosen = boundary;
        chosen.nsec = civil.time.nsec;
        break;

      case GCHRON_RESOLVE_COMPATIBLE: {
        /*
         * Push the reading forward by the length of the gap, so that 02:30 on
         * a spring-forward morning becomes 03:30. This is what `java.time`
         * and legacy JavaScript `Date` do, and it is the behaviour a caller
         * has to name rather than get by default.
         */
        GCHRON_Instant probe;
        GCHRON_ZoneInfo after;
        int64_t local;
        int64_t sec;

        result = gchron_instant_from_utc(&civil, &probe);
        if (result != GCHRON_OK) {
          return result;
        }
        local = probe.sec;
        if (!gchron_add_i64(local, options.gap_seconds, &local)) {
          return GCHRON_ERR_RANGE;
        }
        result = gchron_zone_offset_at(zone, boundary, &after);
        if (result != GCHRON_OK) {
          return result;
        }
        if (!gchron_sub_i64(local, after.offset_sec, &sec)) {
          return GCHRON_ERR_RANGE;
        }
        chosen.sec = sec;
        chosen.nsec = civil.time.nsec;
        break;
      }

      case GCHRON_RESOLVE_REJECT:
      default:
        return GCHRON_ERR_GAP;
    }
  }

  return gchron_zoned_from_instant(chosen, zone, out);
}

GCHRON_Result gchron_zoned_to_civil(const GCHRON_ZonedDateTime * zoned,
    GCHRON_DateTime * out) {
  GCHRON_Instant shifted;
  GCHRON_ZoneInfo info;
  GCHRON_Result result;

  if (zoned == NULL || out == NULL || zoned->zone == NULL) {
    return GCHRON_ERR_INVALID;
  }
  /*
   * The cached offset is a convenience and never the authority (design.md
   * section 3.5), so the zone is asked again rather than trusted. A
   * GCHRON_ZonedDateTime a caller built by hand, or copied and edited, would
   * otherwise print a reading its own zone disagrees with.
   */
  result = gchron_zone_offset_at(zoned->zone, zoned->instant, &info);
  if (result != GCHRON_OK) {
    return result;
  }
  if (!gchron_add_i64(zoned->instant.sec, info.offset_sec, &shifted.sec)) {
    return GCHRON_ERR_RANGE;
  }
  shifted.nsec = zoned->instant.nsec;
  return gchron_instant_to_utc(&shifted, out);
}

GCHRON_Result gchron_zoned_info(const GCHRON_ZonedDateTime * zoned,
    GCHRON_ZoneInfo * out) {
  if (zoned == NULL || out == NULL || zoned->zone == NULL) {
    return GCHRON_ERR_INVALID;
  }
  return gchron_zone_offset_at(zoned->zone, zoned->instant, out);
}

GCHRON_Result gchron_zoned_to_offset(const GCHRON_ZonedDateTime * zoned,
    GCHRON_OffsetDateTime * out) {
  GCHRON_ZoneInfo info;
  GCHRON_Result result;

  if (zoned == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_zoned_info(zoned, &info);
  if (result != GCHRON_OK) {
    return result;
  }
  /* Lossy, and documented: the zone's name does not fit in an offset, and
   * RFC 9557's suffix is what keeps it (mistake M13). */
  return gchron_offset_from_instant(&zoned->instant, info.offset_sec, false,
      out);
}

GCHRON_Result gchron_zoned_start_of_day(const GCHRON_ZonedDateTime * zoned,
    GCHRON_ZonedDateTime * out) {
  GCHRON_DateTime civil;
  GCHRON_Result result;

  if (zoned == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_zoned_to_civil(zoned, &civil);
  if (result != GCHRON_OK) {
    return result;
  }
  civil.time.hour = 0;
  civil.time.minute = 0;
  civil.time.second = 0;
  civil.time.nsec = 0;

  /*
   * GCHRON_RESOLVE_LATER, and this is the reason the policy is a parameter
   * rather than a process-wide setting (mistake M11). In a zone where
   * midnight did not exist that day - America/Sao_Paulo on 2018-11-04, where
   * the clocks went from 23:59:59 straight to 01:00:00 - the first instant of
   * the day is the one *after* the gap. A library with one global policy
   * could not make this function want something different from the rest.
   */
  return gchron_zoned_from_civil(civil, zoned->zone, GCHRON_RESOLVE_LATER,
      out);
}

GCHRON_Result gchron_zoned_with_zone(const GCHRON_ZonedDateTime * zoned,
    const GCHRON_Zone * zone, GCHRON_ZonedDateTime * out) {
  if (zoned == NULL) {
    return GCHRON_ERR_INVALID;
  }
  return gchron_zoned_from_instant(zoned->instant, zone, out);
}

GCHRON_Result gchron_zoned_add(const GCHRON_ZonedDateTime * zoned,
    const GCHRON_Duration * d, GCHRON_Resolve resolve,
    GCHRON_Overflow overflow, GCHRON_ZonedDateTime * out) {
  GCHRON_ZonedDateTime working;
  GCHRON_Duration exact;
  GCHRON_Instant moved;
  GCHRON_Result result;

  if (zoned == NULL || out == NULL || !gchron_duration_is_valid(d)) {
    return GCHRON_ERR_INVALID;
  }

  working = *zoned;
  if (gchron_duration_has_calendar_units(d)) {
    /*
     * The calendar units, in the zone's own civil space. This is what makes
     * "+1 day" keep the wall-clock time across a daylight-saving change while
     * "+24 hours" does not - and it is why this step can fail with
     * GCHRON_ERR_GAP, which exact arithmetic on an instant never can.
     */
    GCHRON_DateTime civil;
    GCHRON_DateTime shifted;
    GCHRON_Duration calendar_part = *d;

    calendar_part.hours = 0;
    calendar_part.minutes = 0;
    calendar_part.seconds = 0;
    calendar_part.nsec = 0;

    result = gchron_zoned_to_civil(zoned, &civil);
    if (result != GCHRON_OK) {
      return result;
    }
    result = gchron_datetime_add(&civil, &calendar_part, NULL, overflow,
        &shifted);
    if (result != GCHRON_OK) {
      return result;
    }
    result = gchron_zoned_from_civil(shifted, zoned->zone, resolve, &working);
    if (result != GCHRON_OK) {
      return result;
    }
  }

  /* Then the exact units, on the instant, where they mean seconds. */
  exact = *d;
  exact.years = 0;
  exact.months = 0;
  exact.weeks = 0;
  exact.days = 0;
  if (!gchron_duration_has_exact_units(&exact)) {
    *out = working;
    return GCHRON_OK;
  }
  result = gchron_instant_add(&working.instant, &exact, &moved);
  if (result != GCHRON_OK) {
    return result;
  }
  return gchron_zoned_from_instant(moved, zoned->zone, out);
}

GCHRON_Result gchron_zoned_until(const GCHRON_ZonedDateTime * from,
    const GCHRON_ZonedDateTime * to, GCHRON_Unit largest_unit,
    GCHRON_Duration * out) {
  GCHRON_DateTime from_civil;
  GCHRON_ZonedDateTime to_here;
  GCHRON_DateTime to_civil;
  GCHRON_Result result;

  if (from == NULL || to == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (largest_unit <= GCHRON_UNIT_HOUR) {
    /* Nothing at or below an hour depends on a zone, so the instants answer
     * it directly and no civil conversion is needed. */
    GCHRON_Duration exact;
    result = gchron_instant_until(&from->instant, &to->instant, &exact);
    if (result != GCHRON_OK) {
      return result;
    }
    return gchron_duration_balance(&exact, largest_unit, NULL, NULL, out);
  }

  /*
   * Calendar units are counted in the *start*'s zone, so that "one month
   * later" means what a person in that place would mean. The end is read in
   * the same zone for the same reason: a difference measured against two
   * different calendars is not a difference.
   */
  result = gchron_zoned_to_civil(from, &from_civil);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_zoned_with_zone(to, from->zone, &to_here);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_zoned_to_civil(&to_here, &to_civil);
  if (result != GCHRON_OK) {
    return result;
  }
  return gchron_datetime_until(&from_civil, &to_civil, largest_unit, NULL,
      out);
}

int gchron_zoned_compare(const GCHRON_ZonedDateTime * a,
    const GCHRON_ZonedDateTime * b) {
  if (a == NULL || b == NULL) {
    return (a == b) ? 0 : (a == NULL ? -1 : 1);
  }
  return gchron_instant_compare(&a->instant, &b->instant);
}

bool gchron_zoned_identical(const GCHRON_ZonedDateTime * a,
    const GCHRON_ZonedDateTime * b) {
  if (a == NULL || b == NULL) {
    return a == b;
  }
  /* Zone identity is pointer identity: a database hands out one zone per
   * identifier, so two values that came from one database and name one zone
   * hold one pointer. */
  return a->zone == b->zone
      && gchron_instant_compare(&a->instant, &b->instant) == 0;
}

void gchron_zoned_dump(const GCHRON_ZonedDateTime * zoned, FILE * stream) {
  GCHRON_DateTime civil;
  GCHRON_ZoneInfo info;

  if (stream == NULL) {
    return;
  }
  if (zoned == NULL) {
    fprintf(stream, "GCHRON_ZonedDateTime(NULL)\n");
    return;
  }
  if (gchron_zoned_to_civil(zoned, &civil) != GCHRON_OK
      || gchron_zoned_info(zoned, &info) != GCHRON_OK) {
    fprintf(stream, "GCHRON_ZonedDateTime(%lld.%09d [unreadable])\n",
        (long long)zoned->instant.sec, zoned->instant.nsec);
    return;
  }
  fprintf(stream,
      "GCHRON_ZonedDateTime(%+011d-%02u-%02u %02u:%02u:%02u.%09d %+d %s%s"
      " [%s])\n",
      civil.date.year, (unsigned)civil.date.month, (unsigned)civil.date.day,
      (unsigned)civil.time.hour, (unsigned)civil.time.minute,
      (unsigned)civil.time.second, civil.time.nsec, info.offset_sec,
      info.abbreviation, info.is_dst ? " dst" : "",
      gchron_zone_id(zoned->zone) ? gchron_zone_id(zoned->zone)
                                  : "<anonymous>");
}

/*--------------------------------------------------------------------------*
 * Rounding
 *--------------------------------------------------------------------------*/

/** One bucket of @p smallest, as a duration to step a boundary forward by. */
static GCHRON_Result bucket_duration(GCHRON_Unit smallest, int64_t increment,
    GCHRON_Duration * out) {
  GCHRON_Duration d;

  memset(&d, 0, sizeof d);
  switch (smallest) {
    case GCHRON_UNIT_NANOSECOND:
    case GCHRON_UNIT_MICROSECOND:
    case GCHRON_UNIT_MILLISECOND: {
      const int64_t per = smallest == GCHRON_UNIT_NANOSECOND ? 1
          : (smallest == GCHRON_UNIT_MICROSECOND ? 1000 : 1000000);
      /*
       * Below a second the bucket is always a proper fraction of a second -
       * the increment has to divide its unit's next step up - so this never
       * needs to carry into seconds.
       */
      d.nsec = (int32_t)(per * increment);
      break;
    }
    case GCHRON_UNIT_SECOND: d.seconds = increment; break;
    case GCHRON_UNIT_MINUTE: d.minutes = increment; break;
    case GCHRON_UNIT_HOUR: d.hours = increment; break;
    case GCHRON_UNIT_DAY: d.days = increment; break;
    case GCHRON_UNIT_WEEK: d.weeks = increment; break;
    case GCHRON_UNIT_MONTH: d.months = increment; break;
    case GCHRON_UNIT_YEAR: d.years = increment; break;
    default: return GCHRON_ERR_INVALID;
  }
  *out = d;
  return GCHRON_OK;
}

/**
 * Which bucket a local boundary is, for GCHRON_ROUND_HALF_EVEN.
 *
 * Counted in local terms, because the boundaries are local: the day a zoned
 * value falls on is the local date, not whatever UTC calls it.
 */
static GCHRON_Result local_bucket(const GCHRON_DateTime * boundary,
    GCHRON_Unit smallest, int64_t increment, int64_t * out) {
  int64_t epoch_day;
  GCHRON_Result result;

  switch (smallest) {
    case GCHRON_UNIT_YEAR:
      *out = (int64_t)boundary->date.year;
      return GCHRON_OK;
    case GCHRON_UNIT_MONTH:
      *out = (int64_t)boundary->date.year * 12 + (boundary->date.month - 1);
      return GCHRON_OK;
    case GCHRON_UNIT_WEEK:
      result = gchron_date_to_epoch_day(&boundary->date, &epoch_day);
      if (result != GCHRON_OK) {
        return result;
      }
      *out = gchron_floor_div(epoch_day, 7);
      return GCHRON_OK;
    default: {
      int64_t seconds;
      int64_t scale;

      result = gchron_date_to_epoch_day(&boundary->date, &epoch_day);
      if (result != GCHRON_OK) {
        return result;
      }
      if (!gchron_mul_i64(epoch_day, GCHRON_SECONDS_PER_DAY, &seconds)) {
        return GCHRON_ERR_RANGE;
      }
      seconds += (int64_t)boundary->time.hour * GCHRON_SECONDS_PER_HOUR
          + (int64_t)boundary->time.minute * 60
          + (int64_t)boundary->time.second;
      if (smallest < GCHRON_UNIT_SECOND) {
        /* Sub-second buckets: count them within the second we landed on. */
        const int64_t per = smallest == GCHRON_UNIT_NANOSECOND ? 1
            : (smallest == GCHRON_UNIT_MICROSECOND ? 1000 : 1000000);
        *out = (int64_t)boundary->time.nsec / (per * increment);
        return GCHRON_OK;
      }
      switch (smallest) {
        case GCHRON_UNIT_SECOND: scale = increment; break;
        case GCHRON_UNIT_MINUTE: scale = 60 * increment; break;
        case GCHRON_UNIT_HOUR:
          scale = GCHRON_SECONDS_PER_HOUR * increment;
          break;
        case GCHRON_UNIT_DAY: scale = GCHRON_SECONDS_PER_DAY * increment;
          break;
        default: return GCHRON_ERR_INVALID;
      }
      *out = gchron_floor_div(seconds, scale);
      return GCHRON_OK;
    }
  }
}

/** Nanoseconds from @p from to @p to, refusing anything that will not fit. */
static GCHRON_Result instant_gap_nanos(GCHRON_Instant from, GCHRON_Instant to,
    int64_t * out) {
  int64_t seconds;
  int64_t nanos;

  if (!gchron_sub_i64(to.sec, from.sec, &seconds)) {
    return GCHRON_ERR_RANGE;
  }
  if (!gchron_mul_i64(seconds, GCHRON_NANOS_PER_SECOND, &nanos)) {
    return GCHRON_ERR_RANGE;
  }
  if (!gchron_add_i64(nanos, (int64_t)to.nsec - (int64_t)from.nsec, &nanos)) {
    return GCHRON_ERR_RANGE;
  }
  *out = nanos;
  return GCHRON_OK;
}

GCHRON_Result gchron_zoned_round(const GCHRON_ZonedDateTime * in,
    GCHRON_Unit smallest, int64_t increment, GCHRON_Rounding mode,
    GCHRON_Resolve resolve, GCHRON_ZonedDateTime * out) {
  GCHRON_DateTime civil;
  GCHRON_DateTime lower_civil;
  GCHRON_DateTime upper_civil;
  GCHRON_Duration step;
  GCHRON_ZonedDateTime lower;
  GCHRON_ZonedDateTime upper;
  GCHRON_Result result;
  int64_t frac;
  int64_t whole;
  int64_t bucket;
  bool moves_up;
  bool ok;

  if (out == NULL || in == NULL || in->zone == NULL
      || !gchron_instant_is_valid(&in->instant)) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_zoned_to_civil(in, &civil);
  if (result != GCHRON_OK) {
    return result;
  }

  /*
   * The lower boundary is a *local* one, found with civil arithmetic. This is
   * the step that makes the answer different from rounding the instant: on a
   * 25-hour local day the civil floor is that day's midnight, and the instant
   * 24 hours before the day ends is an hour into it.
   *
   * gchron_datetime_round() also does the argument checking - the unit, the
   * increment and its tiling rule - so a bad argument is refused here with
   * the same code it would get on a civil value.
   */
  result = gchron_datetime_round(&civil, smallest, increment,
      GCHRON_ROUND_FLOOR, NULL, &lower_civil);
  if (result != GCHRON_OK) {
    return result;
  }
  result = bucket_duration(smallest, increment, &step);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_datetime_add(&lower_civil, &step, NULL,
      GCHRON_OVERFLOW_CONSTRAIN, &upper_civil);
  if (result != GCHRON_OK) {
    return result;
  }

  /*
   * Both boundaries come back through the zone, and both can fail: a local
   * midnight that falls in a gap does not exist, and one in an overlap
   * happens twice. That is what the GCHRON_Resolve parameter is for, and why
   * this function can fail where gchron_instant_round() cannot.
   */
  result = gchron_zoned_from_civil(lower_civil, in->zone, resolve, &lower);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_zoned_from_civil(upper_civil, in->zone, resolve, &upper);
  if (result != GCHRON_OK) {
    return result;
  }

  result = instant_gap_nanos(lower.instant, in->instant, &frac);
  if (result != GCHRON_OK) {
    return result;
  }
  if (frac == 0) {
    *out = *in;
    return GCHRON_OK;
  }
  /*
   * Measured in absolute time between the two absolute instants the local
   * boundaries name, so a 25-hour day's midpoint is 12.5 hours after it
   * starts rather than at local noon.
   */
  result = instant_gap_nanos(lower.instant, upper.instant, &whole);
  if (result != GCHRON_OK) {
    return result;
  }
  if (whole <= 0 || frac >= whole) {
    /*
     * The value is not inside the span its own floor named. Nothing in a sane
     * zone does this; it would mean the two boundaries resolved across each
     * other, and guessing which one was meant is worse than saying so.
     */
    return GCHRON_ERR_INTERNAL;
  }
  result = local_bucket(&lower_civil, smallest, increment, &bucket);
  if (result != GCHRON_OK) {
    return result;
  }
  moves_up = gchron_round_moves_up(frac, whole, in->instant.sec < 0,
      (bucket & 1) == 0, mode, &ok);
  if (!ok) {
    return gchron_round_refusal(mode);
  }
  *out = moves_up ? upper : lower;
  return GCHRON_OK;
}
