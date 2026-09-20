/**
 * @file
 *
 * A driver that puts this library behind a line protocol, so that an oracle
 * written in another language can ask it the same questions it asks itself.
 *
 * Reads `<zone> <TAB> <unix seconds>` on standard input and writes
 * `<zone> <TAB> <unix seconds> <TAB> <utoff> <TAB> <isdst> <TAB> <abbr>
 * <TAB> <local civil>` on standard output, or `ERR <code>` where the library
 * refused. One line in, one line out, so a diff against the oracle's output
 * names the exact input that disagreed.
 *
 * Built by `make tools`; not installed, and not part of the library.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/chron.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  GCHRON_ZoneDb * db = NULL;
  char line[512];
  GCHRON_Result result;

  result = gchron_zonedb_system(NULL, NULL, &db);
  if (result != GCHRON_OK) {
    fprintf(stderr, "gchron_zone: no system zone database: %s\n",
        gchron_result_string(result));
    return 2;
  }

  while (fgets(line, sizeof(line), stdin) != NULL) {
    char * tab = strchr(line, '\t');
    char * end = NULL;
    const GCHRON_Zone * zone = NULL;
    GCHRON_Instant instant;
    GCHRON_ZoneInfo info;
    GCHRON_ZonedDateTime zoned;
    GCHRON_DateTime civil;
    long long seconds;

    if (tab == NULL) {
      continue;
    }
    *tab = '\0';
    seconds = strtoll(tab + 1, &end, 10);

    result = gchron_zonedb_zone(db, line, &zone);
    if (result != GCHRON_OK) {
      printf("%s\t%lld\tERR\t%s\n", line, seconds,
          gchron_result_string(result));
      continue;
    }
    if (gchron_instant_create((int64_t)seconds, 0, &instant) != GCHRON_OK
        || gchron_zone_offset_at(zone, instant, &info) != GCHRON_OK
        || gchron_zoned_from_instant(instant, zone, &zoned) != GCHRON_OK
        || gchron_zoned_to_civil(&zoned, &civil) != GCHRON_OK) {
      printf("%s\t%lld\tERR\tunrepresentable\n", line, seconds);
      continue;
    }
    printf("%s\t%lld\t%d\t%d\t%s\t%04d-%02u-%02uT%02u:%02u:%02u\n", line,
        seconds, (int)info.offset_sec, info.is_dst ? 1 : 0, info.abbreviation,
        civil.date.year, (unsigned)civil.date.month,
        (unsigned)civil.date.day, (unsigned)civil.time.hour,
        (unsigned)civil.time.minute, (unsigned)civil.time.second);
  }

  gchron_zonedb_destroy(db);
  return 0;
}
