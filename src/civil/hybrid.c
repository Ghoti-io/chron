/**
 * @file
 *
 * The hybrid Julian-Gregorian calendar: what a historical record actually
 * uses.
 *
 * design.md, mistake M21: applying the proleptic Gregorian calendar to a date
 * that predates it is what every "historical" date computed in a database
 * gets wrong. Newton was born on Christmas Day 1642 in England and on 4
 * January 1643 in Rome, and both are true.
 *
 * The cut-over is a parameter because it was a different day in every
 * country, and the days the reform deleted are reported with GCHRON_ERR_GAP -
 * the same result a daylight-saving gap gives, because it is the same kind of
 * question: a label that names no day.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/calendar.h>
#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/cutil/allocator.h>

#include "../core/core_internal.h"
#include "calendar_internal.h"
#include "civil_internal.h"

/** What a hybrid calendar carries beside the vtable. */
typedef struct HybridContext {
  int64_t cutover;                     /**< First day the Gregorian labels. */
  const GCHRON_Allocator * allocator;  /**< Whose memory this is. */
} HybridContext;

/** Which calendar labels a given epoch day. */
static const GCHRON_Calendar * governing(const HybridContext * ctx,
    int64_t epoch_day) {
  return epoch_day < ctx->cutover ? gchron_calendar_julian()
                                  : gchron_calendar_gregorian();
}

static GCHRON_Result hybrid_from_epoch_day(const GCHRON_Calendar * self,
    int64_t epoch_day, GCHRON_Date * out) {
  const HybridContext * ctx = (const HybridContext *)self->ctx;
  return gchron_calendar_from_epoch_day(governing(ctx, epoch_day), epoch_day,
      out);
}

static GCHRON_Result hybrid_to_epoch_day(const GCHRON_Calendar * self,
    const GCHRON_Date * date, int64_t * out) {
  const HybridContext * ctx = (const HybridContext *)self->ctx;
  int64_t as_gregorian;
  int64_t as_julian;
  GCHRON_Result gregorian_result;
  GCHRON_Result julian_result;

  /*
   * Both readings are computed, and the one whose *own* epoch day falls on
   * its own side of the cut-over is the answer. A label that satisfies
   * neither is one the reform deleted: in Rome, 5 to 14 October 1582 read as
   * Gregorian land before the cut-over and read as Julian land after it, so
   * no day carries them.
   */
  gregorian_result = gchron_calendar_to_epoch_day(
      gchron_calendar_gregorian(), date, &as_gregorian);
  if (gregorian_result == GCHRON_OK && as_gregorian >= ctx->cutover) {
    *out = as_gregorian;
    return GCHRON_OK;
  }

  julian_result = gchron_calendar_to_epoch_day(gchron_calendar_julian(), date,
      &as_julian);
  if (julian_result == GCHRON_OK && as_julian < ctx->cutover) {
    *out = as_julian;
    return GCHRON_OK;
  }

  if (gregorian_result == GCHRON_OK || julian_result == GCHRON_OK) {
    /* A real label in both calendars, and a day in neither. */
    return GCHRON_ERR_GAP;
  }
  /* Not a date in either - 31 February, say. Report whichever complaint the
   * calendar in force would have made. */
  return julian_result != GCHRON_OK ? julian_result : gregorian_result;
}

static int hybrid_months_in_year(const GCHRON_Calendar * self, int32_t year) {
  (void)self;
  (void)year;
  return 12;
}

/**
 * Which calendar labels a whole month.
 *
 * The month the reform fell in is governed by whichever calendar was in force
 * at its **end**, because that is the one that decides how high the day
 * numbers go - and how high they go is what a caller iterating a month needs.
 * The days in the middle that no calendar labels are reported by
 * hybrid_to_epoch_day() as GCHRON_ERR_GAP.
 */
static const GCHRON_Calendar * governing_month(const HybridContext * ctx,
    int32_t year, int month) {
  GCHRON_Date first;
  int64_t as_gregorian;

  first.year = year;
  first.month = (uint8_t)(month < 1 ? 1 : (month > 12 ? 12 : month));
  first.day = 1;
  if (gchron_calendar_to_epoch_day(gchron_calendar_gregorian(), &first,
          &as_gregorian) != GCHRON_OK) {
    return gchron_calendar_gregorian();
  }
  /* A month whose first day, read as Gregorian, already reaches the cut-over
   * is wholly Gregorian; the cut-over month itself ends Gregorian too, and
   * the Gregorian reading of its first day is a few days short - which is
   * exactly the window this comparison has to allow for. */
  return (as_gregorian + 31 >= ctx->cutover) ? gchron_calendar_gregorian()
                                             : gchron_calendar_julian();
}

static int hybrid_days_in_month(const GCHRON_Calendar * self, int32_t year,
    int month) {
  const HybridContext * ctx = (const HybridContext *)self->ctx;
  const GCHRON_Calendar * cal = governing_month(ctx, year, month);
  return cal->days_in_month(cal, year, month);
}

static int hybrid_days_in_year(const GCHRON_Calendar * self, int32_t year) {
  GCHRON_Date first;
  GCHRON_Date next;
  int64_t start;
  int64_t end;

  /*
   * Counted rather than looked up, because the cut-over year genuinely is
   * shorter: 1582 had 355 days in Rome and 1752 had 355 in England. A table
   * would say 365 and a caller iterating the year would walk off the end of
   * it.
   */
  first.year = year;
  first.month = 1;
  first.day = 1;
  if (year >= GCHRON_YEAR_MAX) {
    return 365;
  }
  next.year = year + 1;
  next.month = 1;
  next.day = 1;
  if (hybrid_to_epoch_day(self, &first, &start) != GCHRON_OK
      || hybrid_to_epoch_day(self, &next, &end) != GCHRON_OK) {
    return 365;
  }
  return (int)(end - start);
}

static bool hybrid_is_leap_year(const GCHRON_Calendar * self, int32_t year) {
  const HybridContext * ctx = (const HybridContext *)self->ctx;
  /*
   * March, not January: the leap day is in February, so the calendar that
   * decides whether there is one is the one in force just after it. In a
   * country that adopted the new calendar in, say, 1700, February of that
   * year is governed by whichever was in force then - and Julian 1700 is a
   * leap year while Gregorian 1700 is not.
   */
  const GCHRON_Calendar * cal = governing_month(ctx, year, 3);
  return cal->is_leap_year(cal, year);
}

GCHRON_Result gchron_calendar_hybrid(int64_t cutover_epoch_day,
    const GCHRON_Allocator * allocator, GCHRON_Calendar ** out) {
  GCHRON_Calendar * calendar;
  HybridContext * ctx;
  char * id;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (allocator == NULL) {
    allocator = gchron_allocator_default();
  }
  /* The cut-over has to be a day both calendars can label, or the conversion
   * has nowhere to stand. */
  if (cutover_epoch_day < GCHRON_EPOCH_DAY_MIN
      || cutover_epoch_day > GCHRON_EPOCH_DAY_MAX) {
    return GCHRON_ERR_RANGE;
  }

  calendar = (GCHRON_Calendar *)gcu_allocator_calloc(allocator, 1,
      sizeof(GCHRON_Calendar));
  ctx = (HybridContext *)gcu_allocator_calloc(allocator, 1,
      sizeof(HybridContext));
  id = (char *)gcu_allocator_malloc(allocator, 40);
  if (calendar == NULL || ctx == NULL || id == NULL) {
    gcu_allocator_free(allocator, calendar);
    gcu_allocator_free(allocator, ctx);
    gcu_allocator_free(allocator, id);
    return GCHRON_ERR_OOM;
  }

  /* The identifier names the cut-over, because two hybrid calendars with
   * different cut-overs are different calendars and a caller comparing
   * identifiers should see that. */
  snprintf(id, 40, "hybrid:%lld", (long long)cutover_epoch_day);

  ctx->cutover = cutover_epoch_day;
  ctx->allocator = allocator;

  calendar->id = id;
  calendar->from_epoch_day = hybrid_from_epoch_day;
  calendar->to_epoch_day = hybrid_to_epoch_day;
  calendar->months_in_year = hybrid_months_in_year;
  calendar->days_in_month = hybrid_days_in_month;
  calendar->days_in_year = hybrid_days_in_year;
  calendar->is_leap_year = hybrid_is_leap_year;
  calendar->days_in_week = 7;
  /* The days of the week ran on through the reform - the one thing about
   * October 1582 that did not change - so the anchor is the Gregorian one. */
  calendar->week_epoch_day = gchron_calendar_gregorian()->week_epoch_day;
  calendar->ctx = ctx;

  *out = calendar;
  return GCHRON_OK;
}

bool gchron_calendar_is_hybrid(const GCHRON_Calendar * calendar) {
  return calendar != NULL && calendar->to_epoch_day == hybrid_to_epoch_day;
}

/**
 * Free whichever kind of run-time calendar this is.
 *
 * The two kinds are told apart by their conversion function rather than by a
 * tag in the struct: a tag would be visible to every caller and would have to
 * be maintained by anybody implementing a GCHRON_Calendar of their own, which
 * is a burden the escape hatch should not carry. A calendar this library did
 * not build - a caller's own, or one of the two static ones - is ignored, so
 * that a caller holding "whichever calendar the user chose" need not remember
 * which kind it is.
 */
void gchron_calendar_destroy(GCHRON_Calendar * calendar) {
  if (calendar == NULL) {
    return;
  }
  if (gchron_calendar_is_hybrid(calendar)) {
    gchron_calendar_hybrid_free(calendar);
    return;
  }
  if (gchron_calendar_is_tabular(calendar)) {
    gchron_calendar_tabular_free(calendar);
    return;
  }
  /* Static, or somebody else's. Not ours to free. */
}

void gchron_calendar_hybrid_free(GCHRON_Calendar * calendar) {
  HybridContext * ctx;
  const GCHRON_Allocator * allocator;

  if (calendar == NULL) {
    return;
  }
  ctx = (HybridContext *)calendar->ctx;
  allocator = ctx->allocator;
  gcu_allocator_free(allocator, (void *)(uintptr_t)calendar->id);
  gcu_allocator_free(allocator, ctx);
  gcu_allocator_free(allocator, calendar);
}
