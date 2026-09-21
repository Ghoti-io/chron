/**
 * @file
 *
 * The chron side of the LDML *parse* differential: can this library read back
 * the text ICU wrote?
 *
 * design.md section 8.7. The formatting differential compares two writers;
 * this compares a reader against the only writer whose output is the
 * definition. The text on the input line is ICU's own, produced by
 * tools/oracle/icu_format.cpp - so a disagreement here is this library
 * failing to read what the reference implementation emits, which is the thing
 * a log reader actually has to do.
 *
 * Reads `<pattern> <TAB> <zone> <TAB> <unix millis> <TAB> <ICU's text>` and
 * writes the same three fields followed by the millis this library recovers,
 * or `ERR` where it refused. One line in, one line out.
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


/** Read one tab-separated field, advancing the cursor past its separator. */
static char * field(char ** cursor) {
  char * start = *cursor;
  char * tab = strchr(start, '\t');
  if (tab == NULL) {
    *cursor = start + strlen(start);
    return start;
  }
  *tab = '\0';
  *cursor = tab + 1;
  return start;
}

int main(void) {
  GCHRON_ZoneDb * db = NULL;
  char line[4096];

  if (gchron_zonedb_default(NULL, NULL, &db) != GCHRON_OK) {
    fprintf(stderr, "gchron_scan: no zone database\n");
    return 1;
  }

  while (fgets(line, sizeof(line), stdin) != NULL) {
    char * cursor = line;
    char * pattern;
    char * zone;
    char * millis_text;
    char * text;
    size_t length = strlen(line);
    GCHRON_Format * format = NULL;
    GCHRON_PatternContext context;
    GCHRON_ParsedFields fields;
    GCHRON_OffsetDateTime odt;
    GCHRON_Instant instant;
    const GCHRON_Zone * zone_handle = NULL;
    long long recovered = 0;
    bool ok = false;

    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
      line[--length] = '\0';
    }
    pattern = field(&cursor);
    zone = field(&cursor);
    millis_text = field(&cursor);
    text = cursor;
    if (*pattern == '\0') {
      continue;
    }

    /*
     * ICU refused the pattern itself, so there is nothing to read back and
     * nothing to compare. Passed through rather than dropped, so that the two
     * sides stay line for line.
     */
    if (strcmp(text, "ERR") == 0) {
      printf("%s\t%s\t%s\tERR\n", pattern, zone, millis_text);
      continue;
    }

    memset(&context, 0, sizeof(context));
    context.names = &ROOT_NAMES;

    if (gchron_format_compile(pattern, strlen(pattern), GCHRON_FORMAT_LDML,
            NULL, NULL, &format, NULL) == GCHRON_OK
        && gchron_format_parse(format, text, strlen(text), &context, &fields,
            NULL) == GCHRON_OK) {
      /*
       * The offset is what turns a civil reading back into an instant. A
       * pattern that carried one uses it; one that named only a zone asks the
       * database what that zone's offset was - which is the arrangement a
       * caller reading `VV` timestamps has to build anyway, and section 8.7
       * declines to hide inside a `_to_zoned()` that would need a
       * disambiguation policy of its own.
       */
      if (gchron_parsed_to_offset(&fields, &context, &odt, NULL) == GCHRON_OK
          && gchron_offset_to_instant(&odt, &instant) == GCHRON_OK) {
        ok = true;
      }
      else if ((fields.present & GCHRON_FIELD_ZONE_ID) != 0
          && gchron_zonedb_zone(db, fields.zone_id, &zone_handle)
              == GCHRON_OK) {
        GCHRON_DateTime civil;
        GCHRON_CivilOffsets offsets;
        if (gchron_parsed_to_datetime(&fields, &context, &civil, NULL)
                == GCHRON_OK
            && gchron_zone_offsets_for_civil(zone_handle, civil, &offsets)
                == GCHRON_OK
            && offsets.count > 0) {
          /*
           * The *last* candidate, which is GCHRON_RESOLVE_LATER and is what
           * ICU does: its calendar defaults to UCAL_WALLTIME_LAST for a
           * repeated wall time. An earlier draft of this tool took the first
           * and said in a comment that ICU did the same; the differential
           * disagreed on exactly one line out of 576 -
           * `2026-11-01T01:30:00[America/New_York]`, an hour that happens
           * twice - which is the whole reason an oracle beats a confident
           * comment.
           *
           * A count of zero is a gap and stays a refusal: no instant carries
           * that civil reading at all.
           */
          instant = offsets.instants[offsets.count - 1];
          ok = true;
        }
      }
    }
    gchron_format_destroy(format);

    if (!ok) {
      printf("%s\t%s\t%s\tERR\n", pattern, zone, millis_text);
      continue;
    }
    recovered = (long long)instant.sec * 1000
        + (long long)(instant.nsec / 1000000);
    printf("%s\t%s\t%s\t%lld\n", pattern, zone, millis_text, recovered);
  }

  gchron_zonedb_destroy(db);
  return 0;
}
