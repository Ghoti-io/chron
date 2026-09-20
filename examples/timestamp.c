/**
 * @file
 *
 * Read an RFC 3339 timestamp and say everything the library knows about it.
 *
 *     timestamp 1985-04-12T23:20:50.52Z
 *     timestamp 1998-12-31T23:59:60Z --leap
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/chron.h>
#include <stdio.h>
#include <string.h>

/** Print what went wrong, with the offending characters underlined. */
static void report(const char * text, const GCHRON_Error * error) {
  size_t i;

  fprintf(stderr, "not an RFC 3339 date-time: %s\n", error->message);
  fprintf(stderr, "  %s\n  ", text);
  for (i = 0; i < error->offset; ++i) {
    fputc(' ', stderr);
  }
  for (i = 0; i < (error->length ? error->length : 1); ++i) {
    fputc('^', stderr);
  }
  fputc('\n', stderr);
}

int main(int argc, char ** argv) {
  const char * text;
  GCHRON_ParseOptions opts;
  GCHRON_OffsetDateTime when;
  GCHRON_ParseInfo info;
  GCHRON_Error error;
  GCHRON_Instant instant;
  GCHRON_IsoWeekDate week;
  int64_t epoch_day;
  int day_of_week;
  int day_of_year;
  char buffer[GCHRON_RFC3339_DATE_TIME_MAX];
  size_t length;

  if (argc < 2) {
    fprintf(stderr, "usage: %s <rfc3339-timestamp> [--leap]\n", argv[0]);
    return 2;
  }
  text = argv[1];

  /*
   * The default refuses a leap second and a fraction longer than nanosecond
   * precision. Both are things a caller has to ask for by name, which is the
   * rule in design.md section 3.7.
   */
  gchron_parse_options_default(&opts);
  if (argc > 2 && strcmp(argv[2], "--leap") == 0) {
    opts.leap = GCHRON_LEAP_MINUTE;
    opts.fraction = GCHRON_FRACTION_TRUNCATE;
  }

  if (gchron_parse_rfc3339_date_time(text, strlen(text), &opts, &when, &info,
          &error) != GCHRON_OK) {
    report(text, &error);
    return 1;
  }

  gchron_offset_to_instant(&when, &instant);
  gchron_date_to_epoch_day(&when.civil.date, &epoch_day);
  gchron_date_day_of_week(&when.civil.date, &day_of_week);
  gchron_date_day_of_year(&when.civil.date, &day_of_year);
  gchron_date_to_iso_week(&when.civil.date, &week);

  printf("instant      %lld.%09d\n", (long long)instant.sec, instant.nsec);
  printf("epoch day    %lld\n", (long long)epoch_day);
  printf("day of week  %d (1 = Monday)\n", day_of_week);
  printf("day of year  %d\n", day_of_year);
  printf("ISO week     %d-W%02u-%u\n", week.week_year, (unsigned)week.week,
      (unsigned)week.day);
  printf("offset       %+d seconds%s\n", when.offset_sec,
      when.offset_unknown ? " (written -00:00: unknown)" : "");

  if (info.leap_second) {
    /* The value holds :59; this is the evidence that the text said :60. */
    printf("note         the text said :60; the value holds :59\n");
  }
  if (info.fraction_truncated) {
    printf("note         the fraction had %u digits and was truncated\n",
        (unsigned)info.fraction_digits);
  }

  if (gchron_write_rfc3339_date_time(&when, NULL, buffer, sizeof(buffer),
          &length) == GCHRON_OK) {
    printf("written back %s\n", buffer);
  }
  return 0;
}
