/**
 * @file
 *
 * The chron side of the LDML differential: the same line protocol as
 * tools/oracle/icu_format.cpp, answered by this library.
 *
 * Built by `make tools`; not installed, and not part of the library.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/chron.h>
#include <ghoti.io/chron/format.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * CLDR's own root locale, as a GCHRON_Names.
 *
 * The differential exists to measure the *formatter*, not the name tables, so
 * the names have to be held equal on both sides. ICU's root locale has no
 * real month or day names - it falls back to "M06" and to abbreviated
 * weekdays at every width - and its week rules are CLDR's defaults, which are
 * Monday-first with one day in the first week rather than ISO 8601's four.
 * gchron_names_english() ships the ISO rules because GCHRON_NAMED_ISO_WEEK
 * has to produce an ISO week date; this provider is what root actually says,
 * and using it here is the provider seam of design.md section 8.5 doing
 * exactly the job it exists for.
 */
static const char * root_month(const GCHRON_Names * self, int month,
    GCHRON_NameWidth width) {
  static char buffer[8];
  (void)self;
  if (month < 1 || month > 12) {
    return NULL;
  }
  if (width == GCHRON_NAME_NARROW) {
    snprintf(buffer, sizeof(buffer), "%d", month);
  }
  else {
    snprintf(buffer, sizeof(buffer), "M%02d", month);
  }
  return buffer;
}

static const char * root_weekday(const GCHRON_Names * self, int weekday,
    GCHRON_NameWidth width) {
  /* Root has abbreviated and narrow weekday names but no wide ones, so the
   * wide width falls back to the abbreviated. The narrow ones are not
   * unique - Tuesday and Thursday are both "T" - which is why no parser
   * anywhere accepts them. */
  static const char * const ABBREVIATED[8] = {
    NULL, "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
  };
  static const char * const NARROW[8] = {
    NULL, "M", "T", "W", "T", "F", "S", "S"
  };
  (void)self;
  if (weekday < 1 || weekday > 7) {
    return NULL;
  }
  /* Root has no short width either, so it falls back to abbreviated - which
   * is what makes `EEEEEE` print "Mon" and not "Mo". */
  return (width == GCHRON_NAME_NARROW) ? NARROW[weekday]
                                       : ABBREVIATED[weekday];
}

static const char * root_era(const GCHRON_Names * self, int era,
    GCHRON_NameWidth width) {
  (void)self;
  (void)width;
  return (era == 1) ? "CE" : ((era == 0) ? "BCE" : NULL);
}

static const char * root_day_period(const GCHRON_Names * self, int period,
    GCHRON_NameWidth width) {
  (void)self;
  (void)width;
  return (period == 0) ? "AM" : ((period == 1) ? "PM" : NULL);
}

/*
 * Sunday-first, one day in the first week. Not CLDR's "001" default, which is
 * Monday-first with four: ICU resolves an empty root locale through likely
 * subtags to `en_Latn_US` and takes its week data from the United States. The
 * numbers here are what ICU actually uses, which is what a differential needs
 * - discovered by watching `e` report 2 for a Monday and working backwards.
 */
static const GCHRON_Names ROOT_NAMES = {
  "root", root_month, root_weekday, root_era, root_day_period, 7, 1, NULL
};

int main(void) {
  GCHRON_ZoneDb * db = NULL;
  GCHRON_FormatContext context;
  char line[1024];

  memset(&context, 0, sizeof(context));
  context.names = &ROOT_NAMES;

  if (gchron_zonedb_system(NULL, NULL, &db) != GCHRON_OK) {
    fprintf(stderr, "gchron_format: no system zone database\n");
    return 2;
  }

  while (fgets(line, sizeof(line), stdin) != NULL) {
    char * first = strchr(line, '\t');
    char * second;
    char * newline;
    const GCHRON_Zone * zone = NULL;
    GCHRON_Format * format = NULL;
    GCHRON_Instant instant;
    GCHRON_ZonedDateTime zoned;
    char out[512];
    size_t length = 0;
    double millis;

    if (first == NULL) {
      continue;
    }
    *first = '\0';
    second = strchr(first + 1, '\t');
    if (second == NULL) {
      continue;
    }
    *second = '\0';
    newline = strchr(second + 1, '\n');
    if (newline != NULL) {
      *newline = '\0';
    }
    millis = strtod(second + 1, NULL);

    if (gchron_format_compile(line, strlen(line), GCHRON_FORMAT_LDML, NULL,
            NULL, &format, NULL) != GCHRON_OK) {
      printf("%s\t%s\t%.0f\tERR\n", line, first + 1, millis);
      continue;
    }
    if (gchron_zonedb_zone(db, first + 1, &zone) != GCHRON_OK
        || gchron_instant_from_unix_millis((int64_t)millis, &instant)
            != GCHRON_OK
        || gchron_zoned_from_instant(instant, zone, &zoned) != GCHRON_OK
        || gchron_format_zoned(format, &zoned, &context, out, sizeof(out),
               &length) != GCHRON_OK) {
      printf("%s\t%s\t%.0f\tERR\n", line, first + 1, millis);
      gchron_format_destroy(format);
      continue;
    }
    printf("%s\t%s\t%.0f\t%s\n", line, first + 1, millis, out);
    gchron_format_destroy(format);
  }

  gchron_zonedb_destroy(db);
  return 0;
}
