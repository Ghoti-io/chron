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
 * The names this library ships: the root locale, in English.
 *
 * **Nothing here reads `LC_TIME`, or any other locale setting.** `text`
 * learned in `src/text_number.c` what a locale does to `printf` - a comma
 * decimal separator turning valid JSON into invalid JSON - and a time
 * library's exposure is a hundred times wider: month names, day names, the
 * first day of the week, the era, and the day period all move under a library
 * that consults one (design.md section 8.5).
 *
 * An application that wants other languages supplies a GCHRON_Names backed by
 * CLDR, through the same seam `text`'s `GTEXT_JSON_Regex_Provider` uses: the
 * library defines the interface and ships the minimum, and the dependency
 * decision stays with the application.
 */

#include <ghoti.io/chron/format.h>
#include <ghoti.io/chron/macros.h>
#include <stddef.h>

/** Month names, indexed 1..12. */
static const char * const MONTH_WIDE[13] = {
  NULL, "January", "February", "March", "April", "May", "June", "July",
  "August", "September", "October", "November", "December"
};

static const char * const MONTH_ABBREVIATED[13] = {
  NULL, "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
  "Nov", "Dec"
};

/*
 * Narrow names are **not unique** - January, June and July are all "J" - and
 * exist for a calendar grid's column headings and nothing else. A caller that
 * parses them back cannot; no parser in this library accepts one.
 */
static const char * const MONTH_NARROW[13] = {
  NULL, "J", "F", "M", "A", "M", "J", "J", "A", "S", "O", "N", "D"
};

/** Day names, ISO-indexed: 1 is Monday, 7 is Sunday. */
static const char * const WEEKDAY_WIDE[8] = {
  NULL, "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
  "Sunday"
};

static const char * const WEEKDAY_ABBREVIATED[8] = {
  NULL, "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};

static const char * const WEEKDAY_NARROW[8] = {
  NULL, "M", "T", "W", "T", "F", "S", "S"
};

/** TR35's short width, which English CLDR spells with two letters. */
static const char * const WEEKDAY_SHORT[8] = {
  NULL, "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"
};

/*
 * "BCE" and "CE" rather than "BC" and "AD", because this library numbers
 * years astronomically - year 0 exists and is 1 BCE - and the common-era
 * spelling is the one that pairs with that numbering without implying a claim
 * the arithmetic does not make.
 */
static const char * const ERA_WIDE[2] = {
  "Before Common Era", "Common Era"
};

static const char * const ERA_ABBREVIATED[2] = { "BCE", "CE" };
static const char * const ERA_NARROW[2] = { "B", "C" };

static const char * const PERIOD_WIDE[2] = { "AM", "PM" };
static const char * const PERIOD_NARROW[2] = { "a", "p" };

static const char * english_month(const GCHRON_Names * self, int month,
    GCHRON_NameWidth width) {
  (void)self;
  if (month < 1 || month > 12) {
    return NULL;
  }
  switch (width) {
    case GCHRON_NAME_WIDE: return MONTH_WIDE[month];
    case GCHRON_NAME_NARROW: return MONTH_NARROW[month];
    /* Months have no short width in TR35; the abbreviated one is what a
     * provider falls back to. */
    case GCHRON_NAME_SHORT:
    case GCHRON_NAME_ABBREVIATED:
    default: return MONTH_ABBREVIATED[month];
  }
}

static const char * english_weekday(const GCHRON_Names * self, int weekday,
    GCHRON_NameWidth width) {
  (void)self;
  if (weekday < 1 || weekday > 7) {
    return NULL;
  }
  switch (width) {
    case GCHRON_NAME_WIDE: return WEEKDAY_WIDE[weekday];
    case GCHRON_NAME_NARROW: return WEEKDAY_NARROW[weekday];
    case GCHRON_NAME_SHORT: return WEEKDAY_SHORT[weekday];
    case GCHRON_NAME_ABBREVIATED:
    default: return WEEKDAY_ABBREVIATED[weekday];
  }
}

static const char * english_era(const GCHRON_Names * self, int era,
    GCHRON_NameWidth width) {
  (void)self;
  if (era < 0 || era > 1) {
    return NULL;
  }
  switch (width) {
    case GCHRON_NAME_WIDE: return ERA_WIDE[era];
    case GCHRON_NAME_NARROW: return ERA_NARROW[era];
    case GCHRON_NAME_ABBREVIATED:
    default: return ERA_ABBREVIATED[era];
  }
}

static const char * english_day_period(const GCHRON_Names * self, int period,
    GCHRON_NameWidth width) {
  (void)self;
  if (period < 0 || period > 1) {
    return NULL;
  }
  return (width == GCHRON_NAME_NARROW) ? PERIOD_NARROW[period]
                                       : PERIOD_WIDE[period];
}

static const GCHRON_Names ENGLISH = {
  "root",
  english_month,
  english_weekday,
  english_era,
  english_day_period,
  /* ISO 8601's week: Monday first, and week 1 is the one holding four days
   * of the new year. The root locale is the ISO one; a CLDR-backed provider
   * says something different for the United States, which is precisely the
   * kind of thing that belongs behind this seam rather than in a constant. */
  1,
  4,
  NULL
};

const GCHRON_Names * gchron_names_english(void) {
  return &ENGLISH;
}
