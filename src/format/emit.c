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
 * Running a compiled pattern: one item at a time into a caller's buffer.
 *
 * The output contract is parse.h's, stated once and holding everywhere: the
 * buffer receives a NUL-terminated string, the length is reported without the
 * NUL, and a buffer too small is GCHRON_ERR_LIMIT with the needed length
 * still set. That is why this writes through a cursor that counts even when
 * it cannot store - a caller may pass a zero-length buffer to ask the length.
 *
 * Reference: Unicode TR35 part 4, *Dates*, the Date Field Symbol Table.
 */

#include <ghoti.io/chron/calendar.h>
#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/format.h>
#include <ghoti.io/chron/macros.h>
#include <string.h>

#include "../core/core_internal.h"
#include "format_internal.h"

/**
 * A write cursor that keeps counting after it runs out of room.
 *
 * `snprintf`'s contract, and the reason a caller can ask for the length by
 * passing no buffer at all.
 */
typedef struct Cursor {
  char * buf;
  size_t capacity;
  size_t written;
} Cursor;

static void put_bytes(Cursor * c, const char * text, size_t len) {
  size_t i;
  for (i = 0; i < len; ++i) {
    if (c->written < c->capacity) {
      c->buf[c->written] = text[i];
    }
    c->written += 1;
  }
}

static void put_char(Cursor * c, char ch) {
  put_bytes(c, &ch, 1);
}

static void put_string(Cursor * c, const char * text) {
  put_bytes(c, text, strlen(text));
}

/** Write a non-negative number, zero-padded to at least @p width. */
static void put_number(Cursor * c, int64_t value, int width) {
  char digits[24];
  int length = 0;
  int i;

  if (value < 0) {
    put_char(c, '-');
    value = -value;
  }
  do {
    digits[length++] = (char)('0' + (int)(value % 10));
    value /= 10;
  } while (value != 0 && length < (int)sizeof(digits));

  for (i = length; i < width; ++i) {
    put_char(c, '0');
  }
  for (i = length - 1; i >= 0; --i) {
    put_char(c, digits[i]);
  }
}

/** Write a non-negative number, space-padded to @p width. `strftime`'s `%e`. */
static void put_number_spaced(Cursor * c, int64_t value, int width) {
  char digits[24];
  int length = 0;
  int i;

  do {
    digits[length++] = (char)('0' + (int)(value % 10));
    value /= 10;
  } while (value != 0 && length < (int)sizeof(digits));

  for (i = length; i < width; ++i) {
    put_char(c, ' ');
  }
  for (i = length - 1; i >= 0; --i) {
    put_char(c, digits[i]);
  }
}

/**
 * Which name width a run of that many letters asks for.
 *
 * TR35: three letters or fewer is abbreviated, four is wide, five is narrow,
 * and six - which only the weekday letters accept - is short. The six-letter
 * case is the one a reader skips, and it is the one ICU's root locale
 * answers with the abbreviated form because it has no short names.
 */
static GCHRON_NameWidth width_for(int count) {
  if (count >= 6) {
    return GCHRON_NAME_SHORT;
  }
  if (count == 5) {
    return GCHRON_NAME_NARROW;
  }
  return (count == 4) ? GCHRON_NAME_WIDE : GCHRON_NAME_ABBREVIATED;
}

/**
 * Write an offset.
 *
 * @param style 0 for `X` (Z when zero), 1 for `x` (always numeric), 2 for
 *   `Z` (RFC 822), 3 for `O` (localised GMT).
 */
static void put_offset(Cursor * c, int32_t offset_sec, int count, int style) {
  int32_t magnitude = offset_sec < 0 ? -offset_sec : offset_sec;
  int hours = (int)(magnitude / 3600);
  int minutes = (int)((magnitude / 60) % 60);
  int seconds = (int)(magnitude % 60);

  if ((style == 0 || (style == 2 && count >= 5)) && offset_sec == 0) {
    /* TR35: `X` writes `Z` for a zero offset and `x` never does - that one
     * difference is the whole reason the two letters exist - and `ZZZZZ`
     * follows `X`'s rule rather than `Z`'s. */
    put_char(c, 'Z');
    return;
  }

  if (style == 3) {
    /* `O` is the localised GMT format: `GMT+8`, or `GMT` exactly at zero. */
    if (offset_sec == 0) {
      put_string(c, "GMT");
      return;
    }
    put_string(c, "GMT");
    put_char(c, offset_sec < 0 ? '-' : '+');
    if (count >= 4) {
      put_number(c, hours, 2);
      put_char(c, ':');
      put_number(c, minutes, 2);
    }
    else {
      put_number(c, hours, 1);
      if (minutes != 0 || seconds != 0) {
        put_char(c, ':');
        put_number(c, minutes, 2);
      }
    }
    /* The seconds again, for the local-mean-time offsets the tzdb records to
     * the second. */
    if (seconds != 0) {
      put_char(c, ':');
      put_number(c, seconds, 2);
    }
    return;
  }

  put_char(c, offset_sec < 0 ? '-' : '+');
  if (style == 2) {
    /*
     * TR35: `Z`..`ZZZ` are "the ISO 8601 basic format with hours, minutes and
     * **optional seconds** fields" - so a zone on local mean time writes
     * `+000921` and not `+0009`. Europe/Amsterdam kept +00:19:32 until 1937
     * and Europe/Paris +00:09:21 until 1911, and dropping the seconds there
     * is a quarter-hour of silent error. `ZZZZ` is localised GMT and `ZZZZZ`
     * is the extended format, which does use `Z` for a zero offset.
     */
    put_number(c, hours, 2);
    if (count >= 5) {
      put_char(c, ':');
    }
    put_number(c, minutes, 2);
    if (seconds != 0) {
      if (count >= 5) {
        put_char(c, ':');
      }
      put_number(c, seconds, 2);
    }
    return;
  }

  switch (count) {
    case 1:
      /* `X`/`x`: hours, and minutes only when they are not zero. */
      put_number(c, hours, 2);
      if (minutes != 0) {
        put_number(c, minutes, 2);
      }
      break;
    case 2:
      put_number(c, hours, 2);
      put_number(c, minutes, 2);
      break;
    case 3:
      put_number(c, hours, 2);
      put_char(c, ':');
      put_number(c, minutes, 2);
      break;
    case 4:
      put_number(c, hours, 2);
      put_number(c, minutes, 2);
      if (seconds != 0) {
        put_number(c, seconds, 2);
      }
      break;
    case 5:
    default:
      put_number(c, hours, 2);
      put_char(c, ':');
      put_number(c, minutes, 2);
      if (seconds != 0) {
        put_char(c, ':');
        put_number(c, seconds, 2);
      }
      break;
  }
}

/**
 * Which week of its own week runs a day falls in, 0-based before adjustment.
 *
 * This is ICU's `Calendar::weekNumber`, transcribed. design.md section 8.3
 * makes ICU the definition of what a pattern letter means, and `w` is a
 * letter whose answer depends on two locale numbers in a way no amount of
 * reasoning from first principles reproduces - the `7001` is there to keep
 * the modulo non-negative for a day early in the year, and getting it subtly
 * wrong gives an answer that is right for fifty weeks a year.
 */
static int week_number(int day_of_period, int weekday, int first_day_of_week,
    int minimum_days) {
  int relative_jan1 =
      (weekday - day_of_period + 7001 - first_day_of_week) % 7;
  int week = (day_of_period - 1 + relative_jan1) / 7;
  if ((7 - relative_jan1) >= minimum_days) {
    week += 1;
  }
  return week;
}

/**
 * The week of the year and the year that week belongs to, by the names
 * provider's own rules.
 *
 * TR35's `w` and `Y` are the *locale's* week, not ISO 8601's. The two differ
 * whenever `minimum_days_in_first_week` is not four or the week does not
 * start on Monday - which is true of the United States, and of what ICU's
 * root locale resolves to. gchron_names_english() ships the ISO rules,
 * because GCHRON_NAMED_ISO_WEEK has to produce an ISO week date; a provider
 * that says otherwise gets what it says.
 *
 * gchron_date_to_iso_week() is the ISO answer regardless, and is what a
 * caller who wants ISO 8601 should use rather than a pattern letter.
 */
static GCHRON_Result local_week_of_year(const GCHRON_Date * date,
    const GCHRON_Calendar * calendar, const GCHRON_Names * names,
    int32_t * out_year, int * out_week) {
  int first_day_of_week = names->first_day_of_week;
  int minimum_days = names->minimum_days_in_first_week;
  int weekday = 0;
  int day_of_year = 0;
  int year_length = 0;
  int relative_dow;
  int week;

  if (first_day_of_week < 1 || first_day_of_week > 7) {
    first_day_of_week = 1;
  }
  if (minimum_days < 1 || minimum_days > 7) {
    minimum_days = 1;
  }
  if (gchron_calendar_day_of_week(calendar, date, &weekday) != GCHRON_OK
      || gchron_calendar_day_of_year(calendar, date, &day_of_year)
          != GCHRON_OK
      || gchron_calendar_days_in_year(calendar, date->year, &year_length)
          != GCHRON_OK) {
    return GCHRON_ERR_RANGE;
  }

  relative_dow = (weekday + 7 - first_day_of_week) % 7;
  week = week_number(day_of_year, weekday, first_day_of_week, minimum_days);

  if (week == 0) {
    /* The first days of the year fell short of the minimum, so they belong to
     * the last week of the year before. */
    int previous_length = 0;
    if (date->year <= GCHRON_YEAR_MIN) {
      return GCHRON_ERR_RANGE;
    }
    if (gchron_calendar_days_in_year(calendar, date->year - 1,
            &previous_length) != GCHRON_OK) {
      return GCHRON_ERR_RANGE;
    }
    *out_year = date->year - 1;
    *out_week = week_number(day_of_year + previous_length, weekday,
        first_day_of_week, minimum_days);
    return GCHRON_OK;
  }

  /* And the mirror case: the last days of the year may be week 1 of the next.
   * Only the final six can be, which is why ICU checks the cheap bound
   * first. */
  if (day_of_year >= year_length - 5) {
    int last_relative_dow = (relative_dow + year_length - day_of_year) % 7;
    if (last_relative_dow < 0) {
      last_relative_dow += 7;
    }
    if ((6 - last_relative_dow) >= minimum_days
        && (day_of_year + 7 - relative_dow) > year_length) {
      if (date->year >= GCHRON_YEAR_MAX) {
        return GCHRON_ERR_RANGE;
      }
      *out_year = date->year + 1;
      *out_week = 1;
      return GCHRON_OK;
    }
  }

  *out_year = date->year;
  *out_week = week;
  return GCHRON_OK;
}

/** Write a year, with TR35's two-digit special case. */
static void put_year(Cursor * c, int64_t year, int count) {
  if (count == 2) {
    /*
     * TR35: exactly two letters means the last two digits, zero-padded. This
     * is the only place a year is truncated, and it is truncated because the
     * pattern asked for it.
     */
    int64_t last_two = gchron_floor_mod(year, 100);
    put_number(c, last_two, 2);
    return;
  }
  put_number(c, year, count);
}

GCHRON_Result gchron_format_emit(const GCHRON_Format * format,
    const GCHRON_DateTime * dt, const GCHRON_FormatContext * context,
    const GCHRON_Instant * instant, char * buf, size_t buf_len,
    size_t * out_len) {
  Cursor c;
  size_t index;
  const GCHRON_Names * names;
  const GCHRON_Calendar * calendar;
  int weekday = 0;
  int day_of_year = 0;
  int64_t epoch_day = 0;
  int32_t local_week_year = 0;
  int local_week = 0;
  bool have_local_week = false;

  if (format == NULL || dt == NULL || context == NULL || out_len == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (buf == NULL && buf_len != 0) {
    return GCHRON_ERR_INVALID;
  }

  names = context->names != NULL ? context->names : gchron_names_english();
  calendar = context->calendar;

  if (gchron_calendar_to_epoch_day(calendar, &dt->date, &epoch_day)
      != GCHRON_OK) {
    return GCHRON_ERR_INVALID;
  }
  (void)gchron_calendar_day_of_week(calendar, &dt->date, &weekday);
  (void)gchron_calendar_day_of_year(calendar, &dt->date, &day_of_year);

  c.buf = buf;
  c.capacity = buf_len == 0 ? 0 : buf_len - 1; /* leave room for the NUL */
  c.written = 0;

  for (index = 0; index < format->item_count; ++index) {
    const GCHRON_FormatItem * item = &format->items[index];
    int count = item->count;

    switch (item->kind) {
      case GCHRON_ITEM_LITERAL:
        put_bytes(&c, format->literals + item->literal_at, item->literal_len);
        break;

      case GCHRON_ITEM_ERA: {
        /* Year 0 is 1 BCE, so the era boundary is at zero and not at one. */
        const char * name = names->era(names, dt->date.year > 0 ? 1 : 0,
            width_for(count));
        if (name == NULL) {
          return GCHRON_ERR_UNSUPPORTED;
        }
        put_string(&c, name);
        break;
      }

      case GCHRON_ITEM_YEAR: {
        /* `y` is the year *of the era*: 1 BCE prints as 1, not as 0. */
        int64_t year = dt->date.year > 0 ? dt->date.year
                                         : 1 - (int64_t)dt->date.year;
        put_year(&c, year, count);
        break;
      }

      case GCHRON_ITEM_EXTENDED_YEAR:
        /* `u` is the astronomical year, signed, with no era. */
        put_number(&c, dt->date.year, count);
        break;

      case GCHRON_ITEM_WEEK_YEAR:
        if (!have_local_week) {
          if (local_week_of_year(&dt->date, calendar, names,
                  &local_week_year, &local_week) != GCHRON_OK) {
            return GCHRON_ERR_RANGE;
          }
          have_local_week = true;
        }
        put_year(&c, local_week_year, count);
        break;

      case GCHRON_ITEM_QUARTER: {
        int quarter = (dt->date.month - 1) / 3 + 1;
        if (count >= 3) {
          /* Quarter names are CLDR's, and the root provider has none. */
          put_char(&c, 'Q');
          put_number(&c, quarter, 1);
        }
        else {
          put_number(&c, quarter, count);
        }
        break;
      }

      case GCHRON_ITEM_MONTH:
        if (count >= 3) {
          const char * name = names->month(names, dt->date.month,
              width_for(count));
          if (name == NULL) {
            return GCHRON_ERR_UNSUPPORTED;
          }
          put_string(&c, name);
        }
        else {
          put_number(&c, dt->date.month, count);
        }
        break;

      case GCHRON_ITEM_WEEK_OF_YEAR:
        if (!have_local_week) {
          if (local_week_of_year(&dt->date, calendar, names,
                  &local_week_year, &local_week) != GCHRON_OK) {
            return GCHRON_ERR_RANGE;
          }
          have_local_week = true;
        }
        put_number(&c, local_week, count);
        break;

      case GCHRON_ITEM_WEEK_OF_MONTH:
        /* The same counting, from the first of the month rather than the
         * first of the year. */
        put_number(&c,
            week_number(dt->date.day, weekday,
                names->first_day_of_week < 1 || names->first_day_of_week > 7
                    ? 1 : names->first_day_of_week,
                names->minimum_days_in_first_week < 1
                        || names->minimum_days_in_first_week > 7
                    ? 1 : names->minimum_days_in_first_week),
            count);
        break;

      case GCHRON_ITEM_DAY:
        put_number(&c, dt->date.day, count);
        break;

      case GCHRON_ITEM_DAY_SPACE_PADDED:
        put_number_spaced(&c, dt->date.day, count);
        break;

      case GCHRON_ITEM_DAY_OF_YEAR:
        put_number(&c, day_of_year, count);
        break;

      case GCHRON_ITEM_WEEKDAY_IN_MONTH:
        put_number(&c, (dt->date.day - 1) / 7 + 1, count);
        break;

      case GCHRON_ITEM_MODIFIED_JULIAN: {
        int64_t rd = 0;
        if (gchron_epoch_day_to_rd(epoch_day, &rd) != GCHRON_OK) {
          return GCHRON_ERR_RANGE;
        }
        /* TR35's `g` is the modified Julian day, whose day begins at
         * midnight - which is the whole reason it is not the Julian day. */
        put_number(&c, epoch_day + 40587, count);
        break;
      }

      case GCHRON_ITEM_WEEKDAY:
      case GCHRON_ITEM_WEEKDAY_LOCAL: {
        if (item->kind == GCHRON_ITEM_WEEKDAY_LOCAL && count <= 2) {
          /* `e`/`c` numeric: counted from the locale's first day of week. */
          int local = (weekday - names->first_day_of_week + 7) % 7 + 1;
          put_number(&c, local, count);
          break;
        }
        {
          const char * name = names->weekday(names, weekday,
              width_for(count));
          if (name == NULL) {
            return GCHRON_ERR_UNSUPPORTED;
          }
          put_string(&c, name);
        }
        break;
      }

      case GCHRON_ITEM_WEEKDAY_ISO_NUMBER:
        put_number(&c, weekday, 1);
        break;

      case GCHRON_ITEM_WEEKDAY_SUNDAY_ZERO:
        put_number(&c, weekday % 7, 1);
        break;

      case GCHRON_ITEM_DAY_PERIOD: {
        const char * name = names->day_period(names,
            dt->time.hour < 12 ? 0 : 1, width_for(count));
        if (name == NULL) {
          return GCHRON_ERR_UNSUPPORTED;
        }
        put_string(&c, name);
        break;
      }

      case GCHRON_ITEM_HOUR_1_12: {
        int hour = dt->time.hour % 12;
        put_number(&c, hour == 0 ? 12 : hour, count);
        break;
      }

      case GCHRON_ITEM_HOUR_1_12_SPACE_PADDED: {
        int hour = dt->time.hour % 12;
        put_number_spaced(&c, hour == 0 ? 12 : hour, count);
        break;
      }

      case GCHRON_ITEM_HOUR_0_23:
        put_number(&c, dt->time.hour, count);
        break;

      case GCHRON_ITEM_HOUR_0_23_SPACE_PADDED:
        put_number_spaced(&c, dt->time.hour, count);
        break;

      case GCHRON_ITEM_HOUR_0_11:
        put_number(&c, dt->time.hour % 12, count);
        break;

      case GCHRON_ITEM_HOUR_1_24:
        put_number(&c, dt->time.hour == 0 ? 24 : dt->time.hour, count);
        break;

      case GCHRON_ITEM_CENTURY:
        put_number(&c, gchron_floor_div(dt->date.year, 100), count);
        break;

      case GCHRON_ITEM_MINUTE:
        put_number(&c, dt->time.minute, count);
        break;

      case GCHRON_ITEM_SECOND:
        put_number(&c, dt->time.second, count);
        break;

      case GCHRON_ITEM_FRACTION: {
        /*
         * TR35: the letter count is the number of digits, truncating or
         * zero-padding as needed. Truncating and never rounding, for the same
         * reason the parser never rounds: carrying into the next second would
         * change the time.
         */
        int32_t scale = 100000000;
        int digit;
        for (digit = 0; digit < count; ++digit) {
          if (digit < 9) {
            put_char(&c, (char)('0' + (dt->time.nsec / scale) % 10));
            scale /= 10;
          }
          else {
            put_char(&c, '0');
          }
        }
        break;
      }

      case GCHRON_ITEM_MILLIS_OF_DAY: {
        int64_t nanos = 0;
        if (gchron_time_to_nanos_of_day(&dt->time, &nanos) != GCHRON_OK) {
          return GCHRON_ERR_INVALID;
        }
        put_number(&c, nanos / 1000000, count);
        break;
      }

      case GCHRON_ITEM_ZONE_ABBREV:
        if (context->abbreviation == NULL) {
          /* No zone, so no abbreviation. Inventing one would be mistake M12
           * from the other direction. */
          return GCHRON_ERR_UNSUPPORTED;
        }
        put_string(&c, context->abbreviation);
        break;

      case GCHRON_ITEM_ZONE_ID: {
        const char * id = context->zone != NULL
            ? gchron_zone_id(context->zone) : NULL;
        if (id == NULL) {
          return GCHRON_ERR_UNSUPPORTED;
        }
        put_string(&c, id);
        break;
      }

      case GCHRON_ITEM_OFFSET_ISO_Z:
        if (context->offset_unknown) {
          /* RFC 3339 section 4.3's unknown offset has no LDML spelling, and
           * `-00:00` is the only text that carries the meaning. */
          put_string(&c, count >= 3 ? "-00:00" : "-0000");
          break;
        }
        put_offset(&c, context->offset_sec, count, 0);
        break;

      case GCHRON_ITEM_OFFSET_ISO:
        put_offset(&c, context->offset_sec, count, 1);
        break;

      case GCHRON_ITEM_OFFSET_RFC822:
        if (context->offset_unknown) {
          put_string(&c, "-0000");
          break;
        }
        if (count == 4) {
          put_offset(&c, context->offset_sec, count, 3);
          break;
        }
        put_offset(&c, context->offset_sec, count, 2);
        break;

      case GCHRON_ITEM_OFFSET_LOCALISED:
        put_offset(&c, context->offset_sec, count, 3);
        break;

      case GCHRON_ITEM_EPOCH_SECONDS: {
        if (instant == NULL) {
          return GCHRON_ERR_UNSUPPORTED;
        }
        put_number(&c, instant->sec, 1);
        break;
      }

      default:
        return GCHRON_ERR_INTERNAL;
    }
  }

  *out_len = c.written;
  if (buf_len == 0 || c.written + 1 > buf_len) {
    return GCHRON_ERR_LIMIT;
  }
  buf[c.written] = '\0';
  return GCHRON_OK;
}

/** Fill in a context with the defaults a NULL one means. */
static void default_context(const GCHRON_FormatContext * given,
    GCHRON_FormatContext * out) {
  if (given != NULL) {
    *out = *given;
  }
  else {
    memset(out, 0, sizeof(*out));
  }
  if (out->names == NULL) {
    out->names = gchron_names_english();
  }
}

GCHRON_Result gchron_format_datetime(const GCHRON_Format * format,
    const GCHRON_DateTime * dt, const GCHRON_FormatContext * context,
    char * buf, size_t buf_len, size_t * out_len) {
  GCHRON_FormatContext resolved;
  size_t length = 0;
  GCHRON_Result result;

  default_context(context, &resolved);
  result = gchron_format_emit(format, dt, &resolved, NULL, buf, buf_len,
      &length);
  if (out_len != NULL) {
    *out_len = length;
  }
  return result;
}

GCHRON_Result gchron_format_offset(const GCHRON_Format * format,
    const GCHRON_OffsetDateTime * odt, const GCHRON_FormatContext * context,
    char * buf, size_t buf_len, size_t * out_len) {
  GCHRON_FormatContext resolved;
  GCHRON_OffsetDateTime utc;
  GCHRON_Instant instant;
  size_t length = 0;
  GCHRON_Result result;

  if (format == NULL || !gchron_offset_is_valid(odt)) {
    return GCHRON_ERR_INVALID;
  }
  if (format->in_utc) {
    /* The format states its own zone, so the value moves into it rather than
     * being printed beside a literal that contradicts it. */
    result = gchron_offset_with_offset(odt, 0, &utc);
    if (result != GCHRON_OK) {
      return result;
    }
    odt = &utc;
  }
  default_context(context, &resolved);
  resolved.offset_sec = odt->offset_sec;
  resolved.offset_unknown = odt->offset_unknown;
  result = gchron_offset_to_instant(odt, &instant);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_format_emit(format, &odt->civil, &resolved, &instant, buf,
      buf_len, &length);
  if (out_len != NULL) {
    *out_len = length;
  }
  return result;
}

GCHRON_Result gchron_format_zoned(const GCHRON_Format * format,
    const GCHRON_ZonedDateTime * zoned, const GCHRON_FormatContext * context,
    char * buf, size_t buf_len, size_t * out_len) {
  GCHRON_FormatContext resolved;
  GCHRON_DateTime civil;
  GCHRON_ZoneInfo info;
  size_t length = 0;
  GCHRON_Result result;

  if (format == NULL || zoned == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (format->in_utc) {
    /*
     * The same rule as gchron_format_offset(), from the zoned side: the zone
     * the value carries is not the zone the format writes, so the civil
     * reading has to be the one at UTC. The zone-naming letters are left with
     * nothing, which is right - `VV` beside a literal `GMT` would be two
     * answers to one question.
     */
    GCHRON_OffsetDateTime at_utc;
    result = gchron_offset_from_instant(&zoned->instant, 0, false, &at_utc);
    if (result != GCHRON_OK) {
      return result;
    }
    default_context(context, &resolved);
    resolved.offset_sec = 0;
    resolved.offset_unknown = false;
    result = gchron_format_emit(format, &at_utc.civil, &resolved,
        &zoned->instant, buf, buf_len, &length);
    if (out_len != NULL) {
      *out_len = length;
    }
    return result;
  }
  result = gchron_zoned_to_civil(zoned, &civil);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_zoned_info(zoned, &info);
  if (result != GCHRON_OK) {
    return result;
  }
  default_context(context, &resolved);
  /* Everything the zone-naming letters need, filled in from the value so
   * that a caller does not have to. */
  resolved.zone = zoned->zone;
  resolved.offset_sec = info.offset_sec;
  resolved.offset_unknown = false;
  resolved.abbreviation = info.abbreviation;
  resolved.is_dst = info.is_dst;
  result = gchron_format_emit(format, &civil, &resolved, &zoned->instant, buf,
      buf_len, &length);
  if (out_len != NULL) {
    *out_len = length;
  }
  return result;
}
