/**
 * @file
 *
 * The chron side of the RFC 9557 differential: what does this library make of
 * a timestamp string?
 *
 * design.md section 12 names test262 and Temporal as the oracle for this
 * grammar. Temporal's string format *is* RFC 9557 - the `[America/New_York]`
 * suffix was standardised for it - and V8's implementation is the one test262
 * exercises, so asking V8 directly is the same authority without a corpus to
 * fetch.
 *
 * Reads one hex-encoded string per line, because a differential over a text
 * grammar has to be able to send bytes a shell would eat. Writes one verdict
 * per line:
 *
 *     ok <epoch seconds> <nanoseconds> <offset seconds> <zone id>
 *     no <diagnostic>
 *
 * The options are set to match what the driver on the other side asks for:
 * `offset: "reject"` is GCHRON_ZONECONFLICT_REJECT, which is already the
 * zero value, and `disambiguation: "reject"` is what this library does with
 * a gap or an overlap when the offset does not settle it.
 *
 * Built by `make tools`; not installed, and not part of the library.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/chron.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** One hex digit, or -1. */
static int unhex(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int main(void) {
  char line[8192];
  GCHRON_ZoneDb * db = NULL;
  GCHRON_ParseOptions opts;

  if (gchron_zonedb_system(NULL, NULL, &db) != GCHRON_OK) {
    fprintf(stderr, "gchron_iso: no system zone database; the differential "
        "needs one to resolve an annotation against.\n");
    return 1;
  }
  gchron_parse_options_default(&opts);

  while (fgets(line, sizeof(line), stdin) != NULL) {
    char text[4096];
    size_t len = 0;
    size_t i;
    size_t line_len = strlen(line);
    GCHRON_ZonedDateTime value;
    GCHRON_ParseInfo info;
    GCHRON_Error err;
    GCHRON_Result result;

    while (line_len > 0
        && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
      line_len -= 1;
    }
    if (line_len % 2 != 0 || line_len / 2 >= sizeof(text)) {
      printf("bad-input\n");
      fflush(stdout);
      continue;
    }
    for (i = 0; i < line_len; i += 2) {
      int hi = unhex((unsigned char)line[i]);
      int lo = unhex((unsigned char)line[i + 1]);
      if (hi < 0 || lo < 0) {
        break;
      }
      text[len++] = (char)((hi << 4) | lo);
    }
    if (i != line_len) {
      printf("bad-input\n");
      fflush(stdout);
      continue;
    }

    memset(&value, 0, sizeof(value));
    result = gchron_parse_rfc9557(text, len, db, &opts, &value, &info, &err);
    if (result != GCHRON_OK) {
      printf("no %s\n", gchron_diag_string(err.diag));
      fflush(stdout);
      continue;
    }
    printf("ok %lld %d %d %s\n", (long long)value.instant.sec,
        (int)value.instant.nsec, (int)value.offset_sec,
        gchron_zone_id(value.zone) ? gchron_zone_id(value.zone) : "?");
    fflush(stdout);
  }
  gchron_zonedb_destroy(db);
  return 0;
}
