/**
 * @file
 *
 * The chron side of the YAML 1.1 timestamp differential: one line of input,
 * one line of verdict, so that a generator in another language can put
 * hundreds of thousands of strings through this library and PyYAML alike.
 *
 * The line protocol reads a hex-encoded scalar per line - hex because the
 * corpus deliberately contains tabs, NULs and bytes that are not UTF-8, none
 * of which survive a line-oriented file as themselves - and writes either
 *
 *     ok <year> <month> <day> <hour> <minute> <second> <nsec> <kind> <offset> <flags>
 *
 * or `no <diagnostic>`. Every field is what this library read, so the
 * generator can compare values and not merely verdicts.
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

  while (fgets(line, sizeof(line), stdin) != NULL) {
    char text[4096];
    size_t len = 0;
    size_t i;
    size_t line_len = strlen(line);
    GCHRON_YamlValue value;
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

    result = gchron_parse_yaml_timestamp(text, len, NULL, &value, &info, &err);
    if (result != GCHRON_OK) {
      printf("no %s\n", gchron_diag_string(err.diag));
      fflush(stdout);
      continue;
    }
    printf("ok %d %d %d %d %d %d %d %d %d %d %d %d\n",
        (int)value.civil.date.year, (int)value.civil.date.month,
        (int)value.civil.date.day, (int)value.civil.time.hour,
        (int)value.civil.time.minute, (int)value.civil.time.second,
        (int)value.civil.time.nsec, (int)value.kind, (int)value.offset_sec,
        value.offset_unknown ? 1 : 0, value.offset_is_z ? 1 : 0,
        info.leap_second ? 1 : 0);
    fflush(stdout);
  }
  return 0;
}
