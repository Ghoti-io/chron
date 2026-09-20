/**
 * @file
 *
 * Duration arithmetic: applying one to a calendar, balancing, differencing
 * and rounding.
 *
 * The rule that governs all of it is design.md section 4.1: one type carries
 * both kinds of unit and the *operation* decides what they mean. Adding a
 * month to an instant is an error; adding one to a civil date-time is a
 * question a calendar can answer; and the two are different functions rather
 * than one function with a flag.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/calendar.h>
#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/macros.h>
#include <string.h>

#include "../core/core_internal.h"

/** Nanoseconds in a day, as an exact count. */
#define NANOS_PER_DAY (GCHRON_SECONDS_PER_DAY * GCHRON_NANOS_PER_SECOND)

/**
 * How many months a calendar's year has, when that is a constant.
 *
 * Calendar-unit arithmetic needs a fixed number of months per year to turn
 * "add fourteen months" into a year and two months. Every calendar this
 * library ships has one; a calendar with a leap *month* - the Hebrew one,
 * which is not shipped - does not, and is refused rather than answered
 * wrongly.
 *
 * @return `true` when the count is the same for both years, which is as much
 *   as can be checked without enumerating the range.
 */
static bool constant_months(const GCHRON_Calendar * calendar, int32_t year_a,
    int32_t year_b, int * out) {
  int a = 0;
  int b = 0;

  if (gchron_calendar_months_in_year(calendar, year_a, &a) != GCHRON_OK
      || gchron_calendar_months_in_year(calendar, year_b, &b) != GCHRON_OK) {
    return false;
  }
  if (a != b || a < 1) {
    return false;
  }
  *out = a;
  return true;
}

/**
 * Add whole years and months to a date, applying the overflow policy.
 *
 * The day is carried across unchanged and clamped only if it has to be, which
 * is what makes `Jan 31 + P1M` a *choice* rather than an accident.
 */
static GCHRON_Result add_year_month(const GCHRON_Date * date, int64_t years,
    int64_t months, const GCHRON_Calendar * calendar,
    GCHRON_Overflow overflow, GCHRON_Date * out) {
  int months_per_year;
  int64_t total;
  int64_t scaled;
  int64_t new_year;
  int new_month;
  int length = 0;
  int check;
  GCHRON_Date candidate;

  if (!constant_months(calendar, date->year,
          date->year > GCHRON_YEAR_MIN ? date->year - 1 : date->year + 1,
          &months_per_year)) {
    return GCHRON_ERR_UNSUPPORTED;
  }

  if (!gchron_mul_i64(date->year, months_per_year, &total)
      || !gchron_add_i64(total, date->month - 1, &total)) {
    return GCHRON_ERR_RANGE;
  }
  if (!gchron_mul_i64(years, months_per_year, &scaled)
      || !gchron_add_i64(total, scaled, &total)
      || !gchron_add_i64(total, months, &total)) {
    return GCHRON_ERR_RANGE;
  }

  new_year = gchron_floor_div(total, months_per_year);
  new_month = (int)gchron_floor_mod(total, months_per_year) + 1;
  if (new_year < GCHRON_YEAR_MIN || new_year > GCHRON_YEAR_MAX) {
    return GCHRON_ERR_RANGE;
  }
  if (!constant_months(calendar, (int32_t)new_year, (int32_t)new_year,
          &check)
      || check != months_per_year) {
    return GCHRON_ERR_UNSUPPORTED;
  }
  if (gchron_calendar_days_in_month(calendar, (int32_t)new_year, new_month,
          &length) != GCHRON_OK) {
    return GCHRON_ERR_RANGE;
  }

  candidate.year = (int32_t)new_year;
  candidate.month = (uint8_t)new_month;
  candidate.day = date->day;
  if (date->day > length) {
    switch (overflow) {
      case GCHRON_OVERFLOW_CONSTRAIN:
        candidate.day = (uint8_t)length;
        break;
      case GCHRON_OVERFLOW_REJECT:
      default:
        /* `Jan 31 + 1 month` has no correct answer; it has a chosen one, and
         * the caller who did not choose is told so (design.md section 4.3). */
        return GCHRON_ERR_RANGE;
    }
  }
  *out = candidate;
  return GCHRON_OK;
}

/** Add a whole number of days to a date, through the epoch day. */
static GCHRON_Result add_days(const GCHRON_Date * date, int64_t days,
    const GCHRON_Calendar * calendar, GCHRON_Date * out) {
  int64_t epoch_day;
  int64_t moved;
  GCHRON_Result result;

  result = gchron_calendar_to_epoch_day(calendar, date, &epoch_day);
  if (result != GCHRON_OK) {
    return result;
  }
  if (!gchron_add_i64(epoch_day, days, &moved)) {
    return GCHRON_ERR_RANGE;
  }
  return gchron_calendar_from_epoch_day(calendar, moved, out);
}

GCHRON_Result gchron_date_add(const GCHRON_Date * date,
    const GCHRON_Duration * d, const GCHRON_Calendar * calendar,
    GCHRON_Overflow overflow, GCHRON_Date * out) {
  GCHRON_Date moved;
  int64_t days;
  int64_t weeks;
  GCHRON_Result result;

  if (date == NULL || out == NULL || !gchron_duration_is_valid(d)) {
    return GCHRON_ERR_INVALID;
  }
  if (gchron_duration_has_exact_units(d)) {
    /* A date has no time of day for an hour to act on. The caller means
     * gchron_datetime_add(), and saying so beats silently discarding it. */
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_calendar_date_is_valid(calendar, date)) {
    return GCHRON_ERR_INVALID;
  }

  result = add_year_month(date, d->years, d->months, calendar, overflow,
      &moved);
  if (result != GCHRON_OK) {
    return result;
  }
  if (!gchron_mul_i64(d->weeks, 7, &weeks)
      || !gchron_add_i64(weeks, d->days, &days)) {
    return GCHRON_ERR_RANGE;
  }
  if (days == 0) {
    *out = moved;
    return GCHRON_OK;
  }
  return add_days(&moved, days, calendar, out);
}

GCHRON_Result gchron_datetime_add(const GCHRON_DateTime * dt,
    const GCHRON_Duration * d, const GCHRON_Calendar * calendar,
    GCHRON_Overflow overflow, GCHRON_DateTime * out) {
  GCHRON_Date date;
  GCHRON_Duration calendar_part;
  int64_t nanos_of_day;
  int64_t exact_seconds;
  int64_t exact_nanos;
  int64_t scaled;
  int64_t day_shift;
  int64_t remainder;
  GCHRON_Result result;

  if (dt == NULL || out == NULL || !gchron_duration_is_valid(d)) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_time_is_valid(&dt->time)
      || !gchron_calendar_date_is_valid(calendar, &dt->date)) {
    return GCHRON_ERR_INVALID;
  }

  /*
   * Calendar units first, then exact units. The order is Temporal's and it is
   * load-bearing: doing it the other way round would make the result depend
   * on the time of day, so that adding a month to 23:00 and to 01:00 of the
   * same day could land in different months.
   */
  calendar_part = *d;
  calendar_part.hours = 0;
  calendar_part.minutes = 0;
  calendar_part.seconds = 0;
  calendar_part.nsec = 0;
  result = gchron_date_add(&dt->date, &calendar_part, calendar, overflow,
      &date);
  if (result != GCHRON_OK) {
    return result;
  }

  /*
   * The exact units are accumulated in **seconds** with a separate
   * nanosecond remainder, not as a single nanosecond count. An `int64_t` of
   * nanoseconds spans 292 years either way, and this library's dates run to
   * nine digits; the first version of this overflowed on any duration three
   * centuries long, which `until`'s own round-trip property found.
   */
  result = gchron_time_to_nanos_of_day(&dt->time, &nanos_of_day);
  if (result != GCHRON_OK) {
    return result;
  }
  if (!gchron_mul_i64(d->hours, GCHRON_SECONDS_PER_HOUR, &exact_seconds)
      || !gchron_mul_i64(d->minutes, 60, &scaled)
      || !gchron_add_i64(exact_seconds, scaled, &exact_seconds)
      || !gchron_add_i64(exact_seconds, d->seconds, &exact_seconds)) {
    return GCHRON_ERR_RANGE;
  }
  /* The two sub-second parts, carried into the seconds. */
  exact_nanos = nanos_of_day % GCHRON_NANOS_PER_SECOND + d->nsec;
  if (!gchron_add_i64(exact_seconds,
          nanos_of_day / GCHRON_NANOS_PER_SECOND, &exact_seconds)
      || !gchron_add_i64(exact_seconds,
             gchron_floor_div(exact_nanos, GCHRON_NANOS_PER_SECOND),
             &exact_seconds)) {
    return GCHRON_ERR_RANGE;
  }
  exact_nanos = gchron_floor_mod(exact_nanos, GCHRON_NANOS_PER_SECOND);

  day_shift = gchron_floor_div(exact_seconds, GCHRON_SECONDS_PER_DAY);
  remainder = gchron_floor_mod(exact_seconds, GCHRON_SECONDS_PER_DAY);

  if (day_shift != 0) {
    GCHRON_Date shifted;
    result = add_days(&date, day_shift, calendar, &shifted);
    if (result != GCHRON_OK) {
      return result;
    }
    date = shifted;
  }
  result = gchron_time_from_nanos_of_day(
      remainder * GCHRON_NANOS_PER_SECOND + exact_nanos, &out->time);
  if (result != GCHRON_OK) {
    return result;
  }
  out->date = date;
  return GCHRON_OK;
}

GCHRON_Result gchron_datetime_subtract(const GCHRON_DateTime * dt,
    const GCHRON_Duration * d, const GCHRON_Calendar * calendar,
    GCHRON_Overflow overflow, GCHRON_DateTime * out) {
  GCHRON_Duration negated;
  GCHRON_Result result;

  result = gchron_duration_negate(d, &negated);
  if (result != GCHRON_OK) {
    return result;
  }
  return gchron_datetime_add(dt, &negated, calendar, overflow, out);
}

/**
 * The difference from one civil date-time to another, as seconds plus a
 * nanosecond remainder.
 *
 * **Not as a nanosecond count.** An `int64_t` of nanoseconds spans only 292
 * years either way, and this library's dates run to nine digits; the first
 * version of this function returned one and `until` failed with
 * GCHRON_ERR_RANGE on any pair three centuries apart. Seconds span 292
 * *billion* years, which is comfortably past what a date can express.
 *
 * The remainder is always non-negative, with the sign in the seconds - the
 * same normalisation GCHRON_Instant and GCHRON_Duration use.
 */
static GCHRON_Result civil_difference(const GCHRON_DateTime * from,
    const GCHRON_DateTime * to, const GCHRON_Calendar * calendar,
    int64_t * out_seconds, int32_t * out_nanos) {
  int64_t from_day;
  int64_t to_day;
  int64_t from_nanos;
  int64_t to_nanos;
  int64_t days;
  int64_t seconds;
  int64_t nanos;
  GCHRON_Result result;

  result = gchron_calendar_to_epoch_day(calendar, &from->date, &from_day);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_calendar_to_epoch_day(calendar, &to->date, &to_day);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_time_to_nanos_of_day(&from->time, &from_nanos);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_time_to_nanos_of_day(&to->time, &to_nanos);
  if (result != GCHRON_OK) {
    return result;
  }

  if (!gchron_sub_i64(to_day, from_day, &days)
      || !gchron_mul_i64(days, GCHRON_SECONDS_PER_DAY, &seconds)) {
    return GCHRON_ERR_RANGE;
  }
  nanos = to_nanos - from_nanos;
  if (!gchron_add_i64(seconds, gchron_floor_div(nanos,
          GCHRON_NANOS_PER_SECOND), &seconds)) {
    return GCHRON_ERR_RANGE;
  }
  *out_seconds = seconds;
  *out_nanos = (int32_t)gchron_floor_mod(nanos, GCHRON_NANOS_PER_SECOND);
  return GCHRON_OK;
}

/** Which way, and whether at all, one civil date-time differs from another. */
static GCHRON_Result civil_direction(const GCHRON_DateTime * from,
    const GCHRON_DateTime * to, const GCHRON_Calendar * calendar,
    int * out) {
  int64_t seconds;
  int32_t nanos;
  GCHRON_Result result = civil_difference(from, to, calendar, &seconds,
      &nanos);
  if (result != GCHRON_OK) {
    return result;
  }
  if (seconds == 0 && nanos == 0) {
    *out = 0;
  }
  else {
    *out = seconds < 0 ? -1 : 1;
  }
  return GCHRON_OK;
}

/**
 * Split a signed second count and its nanosecond remainder into exact fields
 * no larger than @p largest_unit.
 *
 * @param seconds Whole seconds; may be negative.
 * @param nanos 0..999999999, with the sign living in @p seconds.
 */
static void split_exact(int64_t seconds, int32_t nanos,
    GCHRON_Unit largest_unit, GCHRON_Duration * out) {
  int64_t magnitude;
  int64_t sign;
  int32_t fraction = nanos;

  memset(out, 0, sizeof(*out));

  if (seconds < 0 && nanos != 0) {
    /* Work in magnitudes, so that "every non-zero field shares a sign" holds
     * by construction rather than by a repair afterwards. -1.5s arrives as
     * {-2, 5e8} and its magnitude is 1.5s. */
    magnitude = -(seconds + 1);
    fraction = (int32_t)(GCHRON_NANOS_PER_SECOND - nanos);
    sign = -1;
  }
  else if (seconds < 0) {
    magnitude = -seconds;
    sign = -1;
  }
  else {
    magnitude = seconds;
    sign = 1;
  }

  if (largest_unit >= GCHRON_UNIT_HOUR) {
    out->hours = sign * (magnitude / GCHRON_SECONDS_PER_HOUR);
    magnitude %= GCHRON_SECONDS_PER_HOUR;
  }
  if (largest_unit >= GCHRON_UNIT_MINUTE) {
    out->minutes = sign * (magnitude / 60);
    magnitude %= 60;
  }
  out->seconds = sign * magnitude;
  if (sign < 0 && fraction != 0) {
    out->seconds -= 1;
    out->nsec = (int32_t)(GCHRON_NANOS_PER_SECOND - fraction);
  }
  else {
    out->nsec = fraction;
  }
}

/** Set the field of @p d that @p unit names, leaving the rest alone. */
static void set_unit(GCHRON_Duration * d, GCHRON_Unit unit, int64_t value) {
  switch (unit) {
    case GCHRON_UNIT_YEAR: d->years = value; break;
    case GCHRON_UNIT_MONTH: d->months = value; break;
    case GCHRON_UNIT_WEEK: d->weeks = value; break;
    case GCHRON_UNIT_DAY:
    default: d->days = value; break;
  }
}

/**
 * How many whole @p unit fit between @p from and @p to, on top of what
 * @p accumulated already accounts for.
 *
 * A doubling search and then a halving back-off, rather than a loop that
 * counts up: a difference of ten thousand years is 120,000 months, and a
 * month-by-month walk makes every such question quadratic in the answer.
 *
 * Every probe re-adds the **whole accumulated duration** from @p from rather
 * than stepping from the previous answer. That is what makes `until` compose
 * the way `add` does: `gchron_datetime_add` applies years and months
 * together, so `2000-02-29 + P26Y6M` is 2026-08-29 while `+P26Y` then `+P6M`
 * is 2026-08-28, and a `until` that measured the second way produced a
 * duration that did not add back up to its own endpoint.
 */
static GCHRON_Result count_whole(const GCHRON_DateTime * from,
    const GCHRON_DateTime * to, GCHRON_Unit unit, int direction,
    const GCHRON_Calendar * calendar, GCHRON_Duration * accumulated,
    int64_t * inout_value) {
  int64_t value = *inout_value;
  int64_t step = 1;
  GCHRON_Duration probe;
  GCHRON_DateTime reached;
  int sign_at_probe;
  GCHRON_Result result;

  while (step > 0) {
    int64_t candidate;
    if (!gchron_mul_i64(step, direction, &candidate)
        || !gchron_add_i64(value, candidate, &candidate)) {
      step /= 2;
      continue;
    }
    probe = *accumulated;
    set_unit(&probe, unit, candidate);

    result = gchron_datetime_add(from, &probe, calendar,
        GCHRON_OVERFLOW_CONSTRAIN, &reached);
    if (result == GCHRON_OK) {
      result = civil_direction(&reached, to, calendar, &sign_at_probe);
    }
    if (result != GCHRON_OK) {
      /* Off the end of the calendar, or past what an integer holds. Back off
       * rather than fail: a smaller step may still fit. */
      if (step == 1) {
        break;
      }
      step /= 2;
      continue;
    }

    if (sign_at_probe == 0 || sign_at_probe == direction) {
      /* Still short of the target, or exactly on it. */
      value = candidate;
      if (sign_at_probe == 0) {
        break;
      }
      if (step > INT64_MAX / 2) {
        break;
      }
      step *= 2;
      continue;
    }
    if (step == 1) {
      break;
    }
    step /= 2;
  }

  *inout_value = value;
  set_unit(accumulated, unit, value);
  return GCHRON_OK;
}

GCHRON_Result gchron_datetime_until(const GCHRON_DateTime * from,
    const GCHRON_DateTime * to, GCHRON_Unit largest_unit,
    const GCHRON_Calendar * calendar, GCHRON_Duration * out) {
  static const GCHRON_Unit ORDER[] = {
    GCHRON_UNIT_YEAR, GCHRON_UNIT_MONTH, GCHRON_UNIT_WEEK, GCHRON_UNIT_DAY
  };
  GCHRON_Duration accumulated;
  GCHRON_DateTime reached;
  int64_t seconds;
  int32_t nanos;
  int direction;
  size_t i;
  GCHRON_Result result;

  if (from == NULL || to == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (largest_unit <= GCHRON_UNIT_UNSPECIFIED
      || largest_unit >= GCHRON_UNIT_COUNT) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_time_is_valid(&from->time) || !gchron_time_is_valid(&to->time)
      || !gchron_calendar_date_is_valid(calendar, &from->date)
      || !gchron_calendar_date_is_valid(calendar, &to->date)) {
    return GCHRON_ERR_INVALID;
  }

  if (largest_unit <= GCHRON_UNIT_HOUR) {
    /* Nothing above an hour has a fixed length, and nothing at or below one
     * needs a calendar. */
    result = civil_difference(from, to, calendar, &seconds, &nanos);
    if (result != GCHRON_OK) {
      return result;
    }
    split_exact(seconds, nanos, largest_unit, out);
    return GCHRON_OK;
  }

  result = civil_direction(from, to, calendar, &direction);
  if (result != GCHRON_OK) {
    return result;
  }
  memset(&accumulated, 0, sizeof(accumulated));
  if (direction == 0) {
    *out = accumulated;
    return GCHRON_OK;
  }

  /*
   * One unit at a time, largest first, each search building on the duration
   * the previous ones produced. Walking *forward from `from`* at every step
   * is what makes `Jan 31 until Mar 1` come out as `1 month 1 day` rather
   * than `1 month -2 days` - and what makes `until(a, b)` not the negation of
   * `until(b, a)`. The asymmetry is real, and hiding it by symmetrising would
   * invent an answer neither end asked for (design.md section 4.4).
   */
  for (i = 0; i < sizeof(ORDER) / sizeof(ORDER[0]); ++i) {
    int64_t value = 0;
    if (ORDER[i] > largest_unit) {
      continue;
    }
    if (ORDER[i] == GCHRON_UNIT_WEEK && largest_unit != GCHRON_UNIT_WEEK) {
      /*
       * Weeks are a unit a caller opts into, not one that appears in every
       * answer. `until(..., GCHRON_UNIT_YEAR)` giving "26 years, 6 months, 3
       * weeks and 1 day" is arithmetically right and is not what anybody
       * asked; Temporal draws the line in the same place, and a caller who
       * wants weeks names them.
       */
      continue;
    }
    if (ORDER[i] == GCHRON_UNIT_YEAR || ORDER[i] == GCHRON_UNIT_MONTH) {
      int months_check = 0;
      if (!constant_months(calendar, from->date.year, to->date.year,
              &months_check)) {
        return GCHRON_ERR_UNSUPPORTED;
      }
    }
    result = count_whole(from, to, ORDER[i], direction, calendar,
        &accumulated, &value);
    if (result != GCHRON_OK) {
      return result;
    }
  }

  /* Whatever is left over, in exact units. */
  result = gchron_datetime_add(from, &accumulated, calendar,
      GCHRON_OVERFLOW_CONSTRAIN, &reached);
  if (result != GCHRON_OK) {
    return result;
  }
  result = civil_difference(&reached, to, calendar, &seconds, &nanos);
  if (result != GCHRON_OK) {
    return result;
  }
  {
    GCHRON_Duration remainder;
    split_exact(seconds, nanos, GCHRON_UNIT_HOUR, &remainder);
    accumulated.hours = remainder.hours;
    accumulated.minutes = remainder.minutes;
    accumulated.seconds = remainder.seconds;
    accumulated.nsec = remainder.nsec;
  }
  *out = accumulated;
  return GCHRON_OK;
}

GCHRON_Result gchron_duration_balance(const GCHRON_Duration * d,
    GCHRON_Unit largest_unit, const GCHRON_DateTime * relative_to,
    const GCHRON_Calendar * calendar, GCHRON_Duration * out) {
  if (out == NULL || !gchron_duration_is_valid(d)) {
    return GCHRON_ERR_INVALID;
  }
  if (largest_unit <= GCHRON_UNIT_UNSPECIFIED
      || largest_unit >= GCHRON_UNIT_COUNT) {
    return GCHRON_ERR_INVALID;
  }

  if (largest_unit <= GCHRON_UNIT_HOUR) {
    int32_t remainder;
    int64_t seconds;
    GCHRON_Result result;

    if (gchron_duration_has_calendar_units(d)) {
      /* Carrying a day into hours needs to know how long a day is, and that
       * is a zone's business. */
      if (relative_to == NULL) {
        return GCHRON_ERR_INVALID;
      }
      {
        GCHRON_DateTime moved;
        result = gchron_datetime_add(relative_to, d, calendar,
            GCHRON_OVERFLOW_CONSTRAIN, &moved);
        if (result != GCHRON_OK) {
          return result;
        }
        return gchron_datetime_until(relative_to, &moved, largest_unit,
            calendar, out);
      }
    }
    result = gchron_duration_to_exact_seconds(d, &seconds, &remainder);
    if (result != GCHRON_OK) {
      return result;
    }
    split_exact(seconds, remainder, largest_unit, out);
    return GCHRON_OK;
  }

  /*
   * design.md section 4.2: a largest unit at or above days requires something
   * to be relative to, and asking for GCHRON_UNIT_DAY without one is
   * GCHRON_ERR_INVALID - not "assume twenty-four hours". The length of a day
   * is not otherwise known.
   */
  if (relative_to == NULL) {
    return GCHRON_ERR_INVALID;
  }
  {
    GCHRON_DateTime moved;
    GCHRON_Result result = gchron_datetime_add(relative_to, d, calendar,
        GCHRON_OVERFLOW_CONSTRAIN, &moved);
    if (result != GCHRON_OK) {
      return result;
    }
    return gchron_datetime_until(relative_to, &moved, largest_unit, calendar,
        out);
  }
}

/** Round a quotient, given its remainder and the divisor. */
static bool round_quotient(int64_t value, int64_t divisor,
    GCHRON_Rounding rounding, int64_t * out) {
  int64_t quotient = value / divisor;
  int64_t remainder = value % divisor;
  int64_t twice;

  if (remainder == 0) {
    *out = quotient;
    return true;
  }
  switch (rounding) {
    case GCHRON_ROUND_REJECT:
      return false;
    case GCHRON_ROUND_TRUNCATE:
      break;
    case GCHRON_ROUND_FLOOR:
      if (remainder < 0) {
        quotient -= 1;
      }
      break;
    case GCHRON_ROUND_CEIL:
      if (remainder > 0) {
        quotient += 1;
      }
      break;
    case GCHRON_ROUND_HALF_EXPAND:
      twice = remainder < 0 ? -remainder * 2 : remainder * 2;
      if (twice >= divisor) {
        quotient += remainder < 0 ? -1 : 1;
      }
      break;
    case GCHRON_ROUND_HALF_EVEN:
      twice = remainder < 0 ? -remainder * 2 : remainder * 2;
      if (twice > divisor || (twice == divisor && (quotient % 2) != 0)) {
        quotient += remainder < 0 ? -1 : 1;
      }
      break;
    default:
      return false;
  }
  *out = quotient;
  return true;
}

/** How many nanoseconds an exact unit is worth. */
static int64_t nanos_per_unit(GCHRON_Unit unit) {
  switch (unit) {
    case GCHRON_UNIT_NANOSECOND: return 1;
    case GCHRON_UNIT_MICROSECOND: return 1000;
    case GCHRON_UNIT_MILLISECOND: return 1000000;
    case GCHRON_UNIT_SECOND: return GCHRON_NANOS_PER_SECOND;
    case GCHRON_UNIT_MINUTE: return 60 * GCHRON_NANOS_PER_SECOND;
    case GCHRON_UNIT_HOUR:
      return GCHRON_SECONDS_PER_HOUR * GCHRON_NANOS_PER_SECOND;
    default: return 0;
  }
}

GCHRON_Result gchron_duration_round(const GCHRON_Duration * d,
    GCHRON_Unit smallest_unit, GCHRON_Rounding rounding,
    const GCHRON_DateTime * relative_to, const GCHRON_Calendar * calendar,
    GCHRON_Duration * out) {
  GCHRON_Duration working;
  int64_t nanos;
  int64_t seconds;
  int32_t remainder;
  int64_t scale;
  int64_t rounded;
  GCHRON_Result result;

  if (out == NULL || !gchron_duration_is_valid(d)) {
    return GCHRON_ERR_INVALID;
  }
  if (smallest_unit <= GCHRON_UNIT_UNSPECIFIED
      || smallest_unit >= GCHRON_UNIT_COUNT) {
    return GCHRON_ERR_INVALID;
  }

  working = *d;
  if (gchron_duration_has_calendar_units(d) || smallest_unit
      >= GCHRON_UNIT_DAY) {
    /* Anything involving a calendar unit needs something to measure from,
     * for the same reason balancing does. */
    if (relative_to == NULL) {
      return GCHRON_ERR_INVALID;
    }
    result = gchron_duration_balance(d, GCHRON_UNIT_HOUR, relative_to,
        calendar, &working);
    if (result != GCHRON_OK) {
      return result;
    }
    if (smallest_unit >= GCHRON_UNIT_DAY) {
      /*
       * Rounding to a calendar unit. How long "one month" is depends on which
       * month, so the two neighbouring whole-unit marks are computed and the
       * target is placed between them - which is the only way to round to a
       * unit that has no fixed length, and is what Temporal does.
       */
      GCHRON_DateTime target;
      GCHRON_DateTime lower;
      GCHRON_DateTime upper;
      GCHRON_Duration whole;
      GCHRON_Duration next;
      int64_t to_target;
      int64_t to_upper;
      int64_t sign;

      result = gchron_datetime_add(relative_to, d, calendar,
          GCHRON_OVERFLOW_CONSTRAIN, &target);
      if (result != GCHRON_OK) {
        return result;
      }
      result = gchron_datetime_until(relative_to, &target, smallest_unit,
          calendar, &whole);
      if (result != GCHRON_OK) {
        return result;
      }

      /* Truncated to the unit: everything below it dropped, which is what
       * "round to months" means and is the half the first version left in. */
      memset(out, 0, sizeof(*out));
      out->years = whole.years;
      if (smallest_unit <= GCHRON_UNIT_MONTH) {
        out->months = whole.months;
      }
      if (smallest_unit <= GCHRON_UNIT_WEEK) {
        out->weeks = whole.weeks;
      }
      if (smallest_unit <= GCHRON_UNIT_DAY) {
        out->days = whole.days;
      }

      result = gchron_datetime_add(relative_to, out, calendar,
          GCHRON_OVERFLOW_CONSTRAIN, &lower);
      if (result != GCHRON_OK) {
        return result;
      }
      {
        int32_t spare;
        result = civil_difference(&lower, &target, calendar, &to_target,
            &spare);
        if (result != GCHRON_OK) {
          return result;
        }
        (void)spare;
      }
      if (to_target == 0) {
        return GCHRON_OK; /* Already exact. */
      }
      if (rounding == GCHRON_ROUND_REJECT) {
        return GCHRON_ERR_RANGE;
      }

      sign = to_target < 0 ? -1 : 1;
      next = *out;
      switch (smallest_unit) {
        case GCHRON_UNIT_YEAR: next.years += sign; break;
        case GCHRON_UNIT_MONTH: next.months += sign; break;
        case GCHRON_UNIT_WEEK: next.weeks += sign; break;
        default: next.days += sign; break;
      }
      result = gchron_datetime_add(relative_to, &next, calendar,
          GCHRON_OVERFLOW_CONSTRAIN, &upper);
      if (result != GCHRON_OK) {
        return result;
      }
      {
        int32_t spare;
        result = civil_difference(&lower, &upper, calendar, &to_upper,
            &spare);
        if (result != GCHRON_OK) {
          return result;
        }
        (void)spare;
      }
      if (to_upper == 0) {
        /* The next mark is the same instant - a month-end clamp collapsed it.
         * There is nowhere further to round to. */
        return GCHRON_OK;
      }

      {
        /* round_quotient() answers "which of the two marks" once the distance
         * to the target is expressed as a fraction of the distance between
         * them. Reusing it keeps one rounding rule rather than two. */
        int64_t choice = 0;
        if (!round_quotient(to_target, to_upper, rounding, &choice)) {
          return GCHRON_ERR_RANGE;
        }
        if (choice != 0) {
          *out = next;
        }
      }
      return GCHRON_OK;
    }
  }

  result = gchron_duration_to_exact_seconds(&working, &seconds, &remainder);
  if (result != GCHRON_OK) {
    return result;
  }
  scale = nanos_per_unit(smallest_unit);
  if (scale == 0) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_mul_i64(seconds, GCHRON_NANOS_PER_SECOND, &nanos)
      || !gchron_add_i64(nanos, remainder, &nanos)) {
    /* Past what a nanosecond count holds - 292 years. Rounding a duration
     * that long to a nanosecond is not a question anybody is really asking,
     * and saying so beats an answer computed in a wrapped integer. */
    return GCHRON_ERR_RANGE;
  }
  if (!round_quotient(nanos, scale, rounding, &rounded)) {
    return GCHRON_ERR_RANGE;
  }
  if (!gchron_mul_i64(rounded, scale, &nanos)) {
    return GCHRON_ERR_RANGE;
  }
  split_exact(nanos / GCHRON_NANOS_PER_SECOND,
      (int32_t)gchron_floor_mod(nanos, GCHRON_NANOS_PER_SECOND),
      GCHRON_UNIT_HOUR, out);
  return GCHRON_OK;
}
