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
 * Usage: gchron_zone [--db system|embedded|default] [--list | --info]
 *
 * `--db` chooses which database answers. The default is `system`, because
 * that is what the zone differential has always asked and a driver that
 * quietly changed databases would silently move which population was being
 * checked. `embedded` is the table the build compiled in, which is the one
 * Windows uses and the one nothing outside this library used to compare.
 *
 * `--list` prints `<id> <TAB> <canonical>` for every identifier the database
 * enumerates, `-` where the name is not a link. A differential against the
 * embedded table needs this: its population is not on the filesystem, so
 * unlike the system database's there is nothing to scan for it.
 *
 * `--info` prints `<source> <TAB> <version>`, so that a differential can
 * refuse to compare two tzdata releases as though a difference between them
 * were a defect.
 *
 * Built by `make tools`; not installed, and not part of the library.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/chron.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  MODE_ASK,
  MODE_LIST,
  MODE_INFO
} Mode;

static const char * source_name(GCHRON_ZoneSource source) {
  switch (source) {
    case GCHRON_ZONE_SOURCE_SYSTEM: return "system";
    case GCHRON_ZONE_SOURCE_EMBEDDED: return "embedded";
    case GCHRON_ZONE_SOURCE_DIRECTORY: return "directory";
    case GCHRON_ZONE_SOURCE_MEMORY: return "memory";
    case GCHRON_ZONE_SOURCE_FIXED: return "fixed";
    case GCHRON_ZONE_SOURCE_NONE: break;
  }
  return "none";
}

static int list(GCHRON_ZoneDb * db) {
  const char * const * ids = NULL;
  size_t count = 0;
  size_t index;
  GCHRON_Result result = gchron_zonedb_list(db, &ids, &count);

  if (result != GCHRON_OK) {
    fprintf(stderr, "gchron_zone: this database cannot enumerate itself: %s\n",
        gchron_result_string(result));
    return 2;
  }
  for (index = 0; index < count; ++index) {
    const GCHRON_Zone * zone = NULL;
    const char * canonical = NULL;

    if (gchron_zonedb_zone(db, ids[index], &zone) != GCHRON_OK) {
      /*
       * A name the database lists and cannot open is a defect, not something
       * to omit: printing it with no canonical name would make it look like
       * an ordinary zone.
       */
      printf("%s\tERR\n", ids[index]);
      continue;
    }
    canonical = gchron_zone_canonical_id(zone);
    if (canonical == NULL || strcmp(canonical, ids[index]) == 0) {
      printf("%s\t-\n", ids[index]);
    }
    else {
      printf("%s\t%s\n", ids[index], canonical);
    }
  }
  return 0;
}

int main(int argc, char ** argv) {
  GCHRON_ZoneDb * db = NULL;
  char line[512];
  GCHRON_Result result;
  Mode mode = MODE_ASK;
  const char * which = "system";
  int index;

  for (index = 1; index < argc; ++index) {
    if (strcmp(argv[index], "--db") == 0 && index + 1 < argc) {
      which = argv[++index];
    }
    else if (strcmp(argv[index], "--list") == 0) {
      mode = MODE_LIST;
    }
    else if (strcmp(argv[index], "--info") == 0) {
      mode = MODE_INFO;
    }
    else {
      fprintf(stderr, "gchron_zone: unknown argument `%s'\n"
          "usage: gchron_zone [--db system|embedded|default] "
          "[--list | --info]\n", argv[index]);
      return 2;
    }
  }

  if (strcmp(which, "system") == 0) {
    result = gchron_zonedb_system(NULL, NULL, &db);
  }
  else if (strcmp(which, "embedded") == 0) {
    result = gchron_zonedb_embedded(NULL, NULL, &db);
  }
  else if (strcmp(which, "default") == 0) {
    result = gchron_zonedb_default(NULL, NULL, &db);
  }
  else {
    fprintf(stderr, "gchron_zone: no such database `%s'; "
        "expected system, embedded or default\n", which);
    return 2;
  }
  if (result != GCHRON_OK) {
    fprintf(stderr, "gchron_zone: no %s zone database: %s\n", which,
        gchron_result_string(result));
    return 2;
  }

  if (mode == MODE_INFO) {
    const char * version = gchron_zonedb_version(db);
    printf("%s\t%s\n", source_name(gchron_zonedb_source(db)),
        version == NULL ? "unknown" : version);
    gchron_zonedb_destroy(db);
    return 0;
  }
  if (mode == MODE_LIST) {
    int status = list(db);
    gchron_zonedb_destroy(db);
    return status;
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
