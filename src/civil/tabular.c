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
 * Tabular calendars: fixed month lengths and a cyclic leap rule.
 *
 * design.md section 5.5: the user's stated need is a game, a game's calendar
 * is usually simple in structure and arbitrary in its constants, and what it
 * must never require is a C compiler. Ten months of thirty-six days and a
 * five-day festival is a struct, filled in at run time.
 *
 * Mistake M19 is a library whose set of calendars is closed. ICU's is;
 * `java.time`'s needs a JAR. This is the escape hatch, and it is wide enough
 * that the Julian and Gregorian calendars themselves fit through it -
 * `tests/unit/test_calendar_tabular.cpp` builds both this way and proves them
 * identical to the shipped closed-form ones over the whole supported range,
 * which is what says the engine is right.
 */

#include <ghoti.io/chron/calendar.h>
#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/cutil/safemath.h>

#include "../core/core_internal.h"
#include "calendar_internal.h"
#include "civil_internal.h"

/** What a tabular calendar carries beside the vtable. */
typedef struct TabularContext {
  const GCHRON_Allocator * allocator;

  int month_count;
  uint16_t * month_days;      /**< Common-year lengths, month_count of them. */
  int64_t * month_start;      /**< Prefix sums, month_count + 1 of them. */
  int leap_month;             /**< 1-based, or 0. */
  int leap_days;

  int cycle_years;
  uint8_t * pattern;          /**< One bit per year of the cycle. */
  int leap_years_in_cycle;

  int64_t common_year_days;
  int64_t days_in_cycle;
  /** Epoch day of year 0 of each cycle position: cycle_years + 1 entries. */
  int64_t * year_start;

  int64_t epoch_day_of_year_zero;
} TabularContext;

/** Whether year @p n of the cycle is a leap year. */
static bool cycle_year_is_leap(const TabularContext * ctx, int n) {
  return (ctx->pattern[n / 8] & (uint8_t)(1u << (n % 8))) != 0;
}

/** Whether a calendar year is a leap year. */
static bool tabular_leap(const TabularContext * ctx, int64_t year) {
  return cycle_year_is_leap(ctx,
      (int)gchron_floor_mod(year, ctx->cycle_years));
}

/** How many days a month of a given year has. */
static int tabular_month_length(const TabularContext * ctx, int64_t year,
    int month) {
  int length;

  if (month < 1 || month > ctx->month_count) {
    return 0;
  }
  length = ctx->month_days[month - 1];
  if (month == ctx->leap_month && tabular_leap(ctx, year)) {
    length += ctx->leap_days;
  }
  return length;
}

static GCHRON_Result tabular_to_epoch_day(const GCHRON_Calendar * self,
    const GCHRON_Date * date, int64_t * out) {
  const TabularContext * ctx = (const TabularContext *)self->ctx;
  int64_t cycles;
  int position;
  int64_t days;
  int month;

  if (date == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (date->year < GCHRON_YEAR_MIN || date->year > GCHRON_YEAR_MAX) {
    return GCHRON_ERR_RANGE;
  }
  if (date->month < 1 || date->month > ctx->month_count) {
    return GCHRON_ERR_INVALID;
  }
  if (date->day < 1
      || date->day > tabular_month_length(ctx, date->year, date->month)) {
    return GCHRON_ERR_INVALID;
  }

  cycles = gchron_floor_div(date->year, ctx->cycle_years);
  position = (int)gchron_floor_mod(date->year, ctx->cycle_years);

  if (!gchron_mul_i64(cycles, ctx->days_in_cycle, &days)) {
    return GCHRON_ERR_RANGE;
  }
  if (!gchron_add_i64(days, ctx->year_start[position], &days)) {
    return GCHRON_ERR_RANGE;
  }
  /* The months before this one, with the leap month's extra days where they
   * apply. The prefix table holds the common-year sums; the leap adjustment
   * is one comparison rather than a second table. */
  if (!gchron_add_i64(days, ctx->month_start[date->month - 1], &days)) {
    return GCHRON_ERR_RANGE;
  }
  month = ctx->leap_month;
  if (month > 0 && month < date->month && tabular_leap(ctx, date->year)) {
    if (!gchron_add_i64(days, ctx->leap_days, &days)) {
      return GCHRON_ERR_RANGE;
    }
  }
  if (!gchron_add_i64(days, date->day - 1, &days)
      || !gchron_add_i64(days, ctx->epoch_day_of_year_zero, out)) {
    return GCHRON_ERR_RANGE;
  }
  return GCHRON_OK;
}

static GCHRON_Result tabular_from_epoch_day(const GCHRON_Calendar * self,
    int64_t epoch_day, GCHRON_Date * out) {
  const TabularContext * ctx = (const TabularContext *)self->ctx;
  int64_t days;
  int64_t cycles;
  int64_t within;
  int low;
  int high;
  int position;
  int64_t year;
  int64_t day_of_year;
  int month;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_sub_i64(epoch_day, ctx->epoch_day_of_year_zero, &days)) {
    return GCHRON_ERR_RANGE;
  }
  cycles = gchron_floor_div(days, ctx->days_in_cycle);
  within = gchron_floor_mod(days, ctx->days_in_cycle);

  /* Which year of the cycle `within` falls in: a binary search over the
   * prefix table rather than a loop, because a 400-year Gregorian cycle makes
   * the loop the inner loop of every conversion. */
  low = 0;
  high = ctx->cycle_years - 1;
  while (low < high) {
    int mid = low + (high - low + 1) / 2;
    if (ctx->year_start[mid] <= within) {
      low = mid;
    }
    else {
      high = mid - 1;
    }
  }
  position = low;
  day_of_year = within - ctx->year_start[position];

  if (!gchron_mul_i64(cycles, ctx->cycle_years, &year)
      || !gchron_add_i64(year, position, &year)) {
    return GCHRON_ERR_RANGE;
  }
  if (year < GCHRON_YEAR_MIN || year > GCHRON_YEAR_MAX) {
    return GCHRON_ERR_RANGE;
  }

  for (month = 1; month <= ctx->month_count; ++month) {
    int length = tabular_month_length(ctx, year, month);
    if (day_of_year < length) {
      out->year = (int32_t)year;
      out->month = (uint8_t)month;
      out->day = (uint8_t)(day_of_year + 1);
      return GCHRON_OK;
    }
    day_of_year -= length;
  }
  /* Unreachable for a well-formed calendar: the months of a year sum to the
   * year's length by construction. Reported rather than asserted, because an
   * invariant that can only be violated by a bug in this file is exactly what
   * GCHRON_ERR_INTERNAL is for. */
  return GCHRON_ERR_INTERNAL;
}

static int tabular_months_in_year(const GCHRON_Calendar * self, int32_t year) {
  const TabularContext * ctx = (const TabularContext *)self->ctx;
  (void)year;
  return ctx->month_count;
}

static int tabular_days_in_month(const GCHRON_Calendar * self, int32_t year,
    int month) {
  return tabular_month_length((const TabularContext *)self->ctx, year, month);
}

static int tabular_days_in_year(const GCHRON_Calendar * self, int32_t year) {
  const TabularContext * ctx = (const TabularContext *)self->ctx;
  return (int)(ctx->common_year_days
      + (tabular_leap(ctx, year) ? ctx->leap_days : 0));
}

static bool tabular_is_leap_year(const GCHRON_Calendar * self, int32_t year) {
  return tabular_leap((const TabularContext *)self->ctx, year);
}

GCHRON_Result gchron_calendar_tabular(
    const GCHRON_TabularCalendar * description,
    const GCHRON_Allocator * allocator, GCHRON_Calendar ** out) {
  GCHRON_Calendar * calendar = NULL;
  TabularContext * ctx = NULL;
  char * id = NULL;
  size_t pattern_bytes;
  int i;
  int64_t running;

  if (description == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (allocator == NULL) {
    allocator = gchron_allocator_default();
  }
  if (description->month_count < 1
      || description->month_count > GCHRON_TABULAR_MONTH_MAX
      || description->month_days == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (description->leap_rule.cycle_years < 1
      || description->leap_rule.cycle_years > GCHRON_LEAP_CYCLE_MAX
      || description->leap_rule.pattern == NULL) {
    return GCHRON_ERR_INVALID;
  }
  pattern_bytes = ((size_t)description->leap_rule.cycle_years + 7) / 8;
  if (description->leap_rule.pattern_bytes < pattern_bytes) {
    return GCHRON_ERR_INVALID;
  }
  if (description->days_in_week < 1) {
    return GCHRON_ERR_INVALID;
  }
  if (description->leap_month != 0
      && (description->leap_month < 1
          || description->leap_month > description->month_count
          || description->leap_days < 1)) {
    return GCHRON_ERR_INVALID;
  }
  for (i = 0; i < description->month_count; ++i) {
    if (description->month_days[i] == 0) {
      /* A month of no days is not a month, and a year containing one would
       * make from_epoch_day's month walk ambiguous. */
      return GCHRON_ERR_INVALID;
    }
  }

  calendar = (GCHRON_Calendar *)gcu_allocator_calloc(allocator, 1,
      sizeof(GCHRON_Calendar));
  ctx = (TabularContext *)gcu_allocator_calloc(allocator, 1,
      sizeof(TabularContext));
  if (calendar == NULL || ctx == NULL) {
    goto oom;
  }
  ctx->allocator = allocator;
  ctx->month_count = description->month_count;
  ctx->leap_month = description->leap_month;
  ctx->leap_days = description->leap_month != 0 ? description->leap_days : 0;
  ctx->cycle_years = description->leap_rule.cycle_years;
  ctx->epoch_day_of_year_zero = description->epoch_day_of_year_zero;

  ctx->month_days = (uint16_t *)gcu_allocator_calloc(allocator,
      (size_t)ctx->month_count, sizeof(uint16_t));
  ctx->month_start = (int64_t *)gcu_allocator_calloc(allocator,
      (size_t)ctx->month_count + 1, sizeof(int64_t));
  ctx->pattern = (uint8_t *)gcu_allocator_calloc(allocator, pattern_bytes, 1);
  ctx->year_start = (int64_t *)gcu_allocator_calloc(allocator,
      (size_t)ctx->cycle_years + 1, sizeof(int64_t));
  id = (char *)gcu_allocator_malloc(allocator,
      strlen(description->id != NULL ? description->id : "tabular") + 1);
  if (ctx->month_days == NULL || ctx->month_start == NULL
      || ctx->pattern == NULL || ctx->year_start == NULL || id == NULL) {
    goto oom;
  }
  strcpy(id, description->id != NULL ? description->id : "tabular");
  memcpy(ctx->month_days, description->month_days,
      (size_t)ctx->month_count * sizeof(uint16_t));
  memcpy(ctx->pattern, description->leap_rule.pattern, pattern_bytes);

  running = 0;
  for (i = 0; i < ctx->month_count; ++i) {
    ctx->month_start[i] = running;
    running += ctx->month_days[i];
  }
  ctx->month_start[ctx->month_count] = running;
  ctx->common_year_days = running;

  /*
   * The number of leap years in the cycle is *counted* rather than taken from
   * the description. design.md section 5.5 sketches a `leap_years_in_cycle`
   * field beside the pattern; two ways of saying the same thing is two ways
   * for them to disagree, and the count is a popcount of something already
   * here.
   */
  ctx->leap_years_in_cycle = 0;
  running = 0;
  for (i = 0; i < ctx->cycle_years; ++i) {
    ctx->year_start[i] = running;
    running += ctx->common_year_days;
    if (cycle_year_is_leap(ctx, i)) {
      ctx->leap_years_in_cycle += 1;
      running += ctx->leap_days;
    }
  }
  ctx->year_start[ctx->cycle_years] = running;
  ctx->days_in_cycle = running;
  if (ctx->days_in_cycle <= 0) {
    /* A cycle of no days would make from_epoch_day divide by zero, and there
     * is no calendar it could describe. */
    calendar->ctx = ctx;
    calendar->id = id;
    gchron_calendar_tabular_free(calendar);
    return GCHRON_ERR_INVALID;
  }

  calendar->id = id;
  calendar->from_epoch_day = tabular_from_epoch_day;
  calendar->to_epoch_day = tabular_to_epoch_day;
  calendar->months_in_year = tabular_months_in_year;
  calendar->days_in_month = tabular_days_in_month;
  calendar->days_in_year = tabular_days_in_year;
  calendar->is_leap_year = tabular_is_leap_year;
  calendar->days_in_week = description->days_in_week;
  calendar->week_epoch_day = description->week_epoch_day;
  calendar->ctx = ctx;

  *out = calendar;
  return GCHRON_OK;

oom:
  if (ctx != NULL) {
    gcu_allocator_free(allocator, ctx->month_days);
    gcu_allocator_free(allocator, ctx->month_start);
    gcu_allocator_free(allocator, ctx->pattern);
    gcu_allocator_free(allocator, ctx->year_start);
  }
  gcu_allocator_free(allocator, ctx);
  gcu_allocator_free(allocator, calendar);
  gcu_allocator_free(allocator, id);
  return GCHRON_ERR_OOM;
}

void gchron_calendar_tabular_free(GCHRON_Calendar * calendar) {
  TabularContext * ctx;
  const GCHRON_Allocator * allocator;

  if (calendar == NULL) {
    return;
  }
  ctx = (TabularContext *)calendar->ctx;
  if (ctx == NULL) {
    return;
  }
  allocator = ctx->allocator;
  gcu_allocator_free(allocator, ctx->month_days);
  gcu_allocator_free(allocator, ctx->month_start);
  gcu_allocator_free(allocator, ctx->pattern);
  gcu_allocator_free(allocator, ctx->year_start);
  gcu_allocator_free(allocator, (void *)(uintptr_t)calendar->id);
  gcu_allocator_free(allocator, ctx);
  gcu_allocator_free(allocator, calendar);
}

bool gchron_calendar_is_tabular(const GCHRON_Calendar * calendar) {
  return calendar != NULL && calendar->to_epoch_day == tabular_to_epoch_day;
}
