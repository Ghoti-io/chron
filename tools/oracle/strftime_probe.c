/**
 * @file
 *
 * The `strftime` differential: this library's `GCHRON_FORMAT_STRFTIME`
 * compiler against the C library's own `strftime`, in the C locale.
 *
 * design.md section 8.5 says the compound specifiers - `%c`, `%x`, `%X`,
 * `%r` - mean what the C locale says they mean, and the library hardcodes
 * those expansions because it never calls `setlocale`. That makes the C
 * library the authority on its own definition, and the only honest way to
 * check the expansions is to ask it.
 *
 * Unlike the `gchron_*` drivers this one does not put the library behind a
 * line protocol for a differ written in another language: the oracle is
 * `libc`, which is already linked, so the comparison happens here.
 *
 * Years below 1000 are a **deliberate deviation** rather than a skip, and
 * are checked as one - see `the_year_padding_deviation` below and design.md
 * section 8.8.
 *
 * Built by `make tools`, run by `make check-oracle-strftime`; not installed,
 * and not part of the library.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#define _GNU_SOURCE
#include <ghoti.io/chron/chron.h>
#include <ghoti.io/chron/format.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Every specifier the compiler accepts whose meaning the C library also
 * defines. `%n` and `%t` are here for completeness; `%U` and `%W` are not,
 * because the library refuses them on purpose (they are not the ISO week and
 * have no LDML letter), and `%Z` is not, because an offset date-time has no
 * zone to name.
 */
static const char * const SPECS[] = {
  "%a", "%A", "%b", "%B", "%c", "%C", "%d", "%D", "%e", "%F", "%g", "%G",
  "%h", "%H", "%I", "%j", "%k", "%l", "%m", "%M", "%n", "%p", "%r", "%R",
  "%S", "%s", "%t", "%T", "%u", "%V", "%w", "%x", "%X", "%y", "%Y", "%z",
  "%%",
  /* `%E` and `%O` are the locale-alternative modifiers; in the C locale they
   * mean the unmodified specifier, which is what both sides should say. */
  "%Ey", "%EY", "%Ec", "%Od", "%OH", "%OM",
  /* A compound pattern, so that a run of items and literals is compared and
   * not only one item at a time. */
  "%Y-%m-%dT%H:%M:%S%z", "[%a %e %b] %I:%M:%S %p",
};
#define NSPECS ((int)(sizeof(SPECS) / sizeof(SPECS[0])))

static GCHRON_Format * FORMATS[NSPECS];
static long compared;
static long differed;
static int reported;

/** Build the civil time in `tm` as an offset date-time at UTC. */
static int as_offset(const struct tm * tm, GCHRON_OffsetDateTime * out) {
  GCHRON_DateTime dt;

  memset(&dt, 0, sizeof(dt));
  if (gchron_date_create(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
          &dt.date) != GCHRON_OK) {
    return 0;
  }
  if (gchron_time_create(tm->tm_hour, tm->tm_min, tm->tm_sec, 0, &dt.time)
      != GCHRON_OK) {
    return 0;
  }
  return gchron_offset_create(&dt, 0, false, out) == GCHRON_OK;
}

static void compare_all(struct tm * tm) {
  GCHRON_OffsetDateTime odt;
  int i;

  if (!as_offset(tm, &odt)) {
    return;
  }
  for (i = 0; i < NSPECS; ++i) {
    char theirs[512];
    char ours[512];
    size_t length = 0;
    size_t n = strftime(theirs, sizeof(theirs), SPECS[i], tm);

    theirs[n] = '\0';
    if (gchron_format_offset(FORMATS[i], &odt, NULL, ours, sizeof(ours),
            &length) != GCHRON_OK) {
      printf("REFUSED %-8s %04d-%02d-%02d %02d:%02d:%02d\n", SPECS[i],
          tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour,
          tm->tm_min, tm->tm_sec);
      differed += 1;
      continue;
    }
    ours[length] = '\0';
    compared += 1;
    if (strcmp(theirs, ours) != 0) {
      differed += 1;
      if (reported < 40) {
        reported += 1;
        printf("DIFFER  %-8s %04d-%02d-%02d %02d:%02d:%02d  libc=[%s] "
            "chron=[%s]\n", SPECS[i], tm->tm_year + 1900, tm->tm_mon + 1,
            tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, theirs, ours);
      }
    }
  }
}

/** Fill `tm` from a civil date-time, or return 0 if there is no such date. */
static int civil_tm(struct tm * tm, int year, int mon, int day, int hour,
    int minute, int second) {
  memset(tm, 0, sizeof(*tm));
  tm->tm_year = year - 1900;
  tm->tm_mon = mon - 1;
  tm->tm_mday = day;
  tm->tm_hour = hour;
  tm->tm_min = minute;
  tm->tm_sec = second;
  if (timegm(tm) == (time_t)-1) {
    return 0;
  }
  /* timegm normalises, so Feb 30 comes back as Mar 2 and is not a date. */
  return tm->tm_mday == day && tm->tm_mon == mon - 1;
}

/**
 * The one class of disagreement, checked rather than skipped.
 *
 * Below year 1000 this library writes the year to its nominal width and
 * glibc writes it as a plain decimal: `0001-06-15` against `1-06-15` for
 * `%F`. The library is following ISO 8601, which `%F` is defined *as*, and
 * POSIX's `[00,99]` for `%C`; glibc is following "a decimal number" for
 * `%Y` and letting `%F` inherit it. design.md section 8.8 argues it.
 *
 * Asserting the difference means that a C library which starts padding is
 * noticed rather than absorbed - the same reason a tzdata upgrade fails the
 * zone differential instead of being picked up silently.
 */
static int the_year_padding_deviation(void) {
  static const struct { int year; const char * spec; const char * theirs;
      const char * ours; } CASES[] = {
    { 1,   "%Y", "1",   "0001" },
    { 1,   "%F", "1-06-15", "0001-06-15" },
    { 1,   "%C", "0",   "00" },
    { 9,   "%Y", "9",   "0009" },
    { 99,  "%Y", "99",  "0099" },
    { 100, "%Y", "100", "0100" },
    { 100, "%C", "1",   "01" },
    { 999, "%Y", "999", "0999" },
    { 999, "%G", "999", "0999" },
    { 1000, "%Y", "1000", "1000" },   /* and from here they agree */
  };
  size_t c;
  int wrong = 0;

  for (c = 0; c < sizeof(CASES) / sizeof(CASES[0]); ++c) {
    struct tm tm;
    GCHRON_OffsetDateTime odt;
    GCHRON_Format * format = NULL;
    char theirs[64];
    char ours[64];
    size_t length = 0;
    size_t n;

    if (!civil_tm(&tm, CASES[c].year, 6, 15, 6, 5, 4)
        || !as_offset(&tm, &odt)) {
      printf("DEVIATION  year %d: no such date\n", CASES[c].year);
      wrong += 1;
      continue;
    }
    n = strftime(theirs, sizeof(theirs), CASES[c].spec, &tm);
    theirs[n] = '\0';
    if (gchron_format_compile(CASES[c].spec, strlen(CASES[c].spec),
            GCHRON_FORMAT_STRFTIME, NULL, NULL, &format, NULL) != GCHRON_OK) {
      printf("DEVIATION  %s: will not compile\n", CASES[c].spec);
      wrong += 1;
      continue;
    }
    if (gchron_format_offset(format, &odt, NULL, ours, sizeof(ours), &length)
        == GCHRON_OK) {
      ours[length] = '\0';
      if (strcmp(theirs, CASES[c].theirs) != 0) {
        printf("DEVIATION  %s year %d: this C library says [%s], not the "
            "[%s] this deviation was written against\n", CASES[c].spec,
            CASES[c].year, theirs, CASES[c].theirs);
        wrong += 1;
      }
      if (strcmp(ours, CASES[c].ours) != 0) {
        printf("DEVIATION  %s year %d: chron says [%s], expected [%s]\n",
            CASES[c].spec, CASES[c].year, ours, CASES[c].ours);
        wrong += 1;
      }
    }
    else {
      printf("DEVIATION  %s year %d: chron refused\n", CASES[c].spec,
          CASES[c].year);
      wrong += 1;
    }
    gchron_format_destroy(format);
  }
  return wrong;
}

int main(int argc, char ** argv) {
  int from_year = 1000;
  int to_year = 2200;
  int i;
  int year;
  int deviation_wrong;

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--from-year") == 0 && i + 1 < argc) {
      from_year = atoi(argv[++i]);
    }
    else if (strcmp(argv[i], "--to-year") == 0 && i + 1 < argc) {
      to_year = atoi(argv[++i]);
    }
    else {
      fprintf(stderr, "usage: %s [--from-year N] [--to-year N]\n", argv[0]);
      return 2;
    }
  }

  /*
   * The oracle is the C locale, so a machine that cannot provide it has no
   * oracle. Saying so beats comparing against whatever locale was inherited
   * and calling the result agreement.
   */
  if (setlocale(LC_ALL, "C") == NULL) {
    fprintf(stderr, "strftime_probe: the C locale is not available; the "
        "oracle is absent rather than weaker.\n");
    return 1;
  }
  /* `%s` and `%z` read the zone glibc is configured with, not the tm. */
  if (setenv("TZ", "UTC", 1) != 0) {
    fprintf(stderr, "strftime_probe: cannot set TZ=UTC.\n");
    return 1;
  }
  tzset();

  for (i = 0; i < NSPECS; ++i) {
    if (gchron_format_compile(SPECS[i], strlen(SPECS[i]),
            GCHRON_FORMAT_STRFTIME, NULL, NULL, &FORMATS[i], NULL)
        != GCHRON_OK) {
      fprintf(stderr, "strftime_probe: %s will not compile\n", SPECS[i]);
      return 1;
    }
  }

  /* Every day of every year in the range, at one time of day. */
  for (year = from_year; year <= to_year; ++year) {
    int mon;
    for (mon = 1; mon <= 12; ++mon) {
      int day;
      for (day = 1; day <= 31; ++day) {
        struct tm tm;
        if (civil_tm(&tm, year, mon, day, 13, 7, 9)) {
          compare_all(&tm);
        }
      }
    }
  }

  /*
   * Every hour on days chosen for what they make hard: the ISO week year
   * differing from the calendar year at both ends, a leap day, and the two
   * sides of a week boundary. The hour matters for %I, %l and %p.
   */
  {
    static const int DAYS[][3] = {
      { 2026, 1, 1 }, { 2026, 12, 31 }, { 2000, 2, 29 }, { 1999, 12, 31 },
      { 2021, 1, 3 }, { 2021, 1, 4 }, { 2020, 12, 31 }, { 2024, 2, 29 }
    };
    size_t d;
    for (d = 0; d < sizeof(DAYS) / sizeof(DAYS[0]); ++d) {
      int hour;
      for (hour = 0; hour < 24; ++hour) {
        int minute;
        for (minute = 0; minute < 60; minute += 7) {
          struct tm tm;
          if (civil_tm(&tm, DAYS[d][0], DAYS[d][1], DAYS[d][2], hour, minute,
                  hour % 60)) {
            compare_all(&tm);
          }
        }
      }
    }
  }

  deviation_wrong = the_year_padding_deviation();

  for (i = 0; i < NSPECS; ++i) {
    gchron_format_destroy(FORMATS[i]);
  }

  printf("strftime: %ld comparisons over years %d-%d, %ld differed; "
      "the year-padding deviation holds in %s.\n", compared, from_year,
      to_year, differed, deviation_wrong ? "NO case" : "every case");
  return (differed != 0 || deviation_wrong != 0) ? 2 : 0;
}
