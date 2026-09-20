/**
 * @file
 *
 * The leap-second table: reading `leap-seconds.list`, and answering questions
 * about what it holds.
 *
 * The format is NIST's, republished by the IERS. Comment lines begin `#`,
 * with two carrying data of their own: `#$` is when the file was last
 * written, `#@` is when it expires. Data lines are an NTP timestamp and the
 * TAI-UTC offset that begins at it.
 *
 * Reference: https://hpiers.obspm.fr/iers/bul/bulc/ntp/leap-seconds.list
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/leap.h>
#include <ghoti.io/chron/macros.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/core_internal.h"
#include "leap_internal.h"

/** The usual place, when a caller names no path. */
#ifndef GCHRON_LEAP_SECONDS_PATH
#define GCHRON_LEAP_SECONDS_PATH "/usr/share/zoneinfo/leap-seconds.list"
#endif

/* ------------------------------------------------------------------ */
/* Parsing                                                            */
/* ------------------------------------------------------------------ */

/** A cursor over one line. */
typedef struct Line {
  const char * text;
  size_t len;
  size_t pos;
} Line;

static void skip_blanks(Line * l) {
  while (l->pos < l->len && (l->text[l->pos] == ' ' || l->text[l->pos] == '\t')) {
    l->pos += 1;
  }
}

/**
 * Read an unsigned decimal.
 *
 * @return true when at least one digit was read and nothing overflowed.
 */
static bool read_u64(Line * l, int64_t * out) {
  int64_t value = 0;
  size_t start = l->pos;

  while (l->pos < l->len && l->text[l->pos] >= '0' && l->text[l->pos] <= '9') {
    int digit = l->text[l->pos] - '0';
    /*
     * The file is an untrusted input like any other (section 1.2), and an
     * NTP timestamp of ninety digits must not wrap into a plausible one.
     */
    if (value > (INT64_MAX - digit) / 10) {
      return false;
    }
    value = value * 10 + digit;
    l->pos += 1;
  }
  if (l->pos == start) {
    return false;
  }
  *out = value;
  return true;
}

/** Whether the line, from `pos`, is blank or a comment. */
static bool at_end_or_comment(const Line * l) {
  size_t i = l->pos;
  while (i < l->len && (l->text[i] == ' ' || l->text[i] == '\t')) {
    i += 1;
  }
  return i >= l->len || l->text[i] == '#';
}

GCHRON_Result gchron_leap_table_parse(const char * text, size_t len,
    const GCHRON_Allocator * allocator, GCHRON_LeapTable ** out,
    GCHRON_Error * err) {
  GCHRON_Limits defaults;
  GCHRON_LeapEntry * entries = NULL;
  GCHRON_LeapTable * table = NULL;
  size_t capacity = 0;
  size_t count = 0;
  size_t pos = 0;
  int64_t expiry_ntp = -1;
  int64_t updated_ntp = -1;
  int32_t previous_offset = 0;
  bool have_previous = false;

  if (text == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  if (allocator == NULL) {
    allocator = gchron_allocator_default();
  }
  gchron_limits_default(&defaults);

  while (pos < len) {
    size_t line_start = pos;
    size_t line_end = pos;
    Line l;

    while (line_end < len && text[line_end] != '\n') {
      line_end += 1;
    }
    pos = (line_end < len) ? line_end + 1 : len;

    /* Tolerate CRLF: the file is fetched over HTTP as often as not. */
    if (line_end > line_start && text[line_end - 1] == '\r') {
      line_end -= 1;
    }

    l.text = text;
    l.len = line_end;
    l.pos = line_start;
    skip_blanks(&l);
    if (l.pos >= l.len) {
      continue;
    }

    if (text[l.pos] == '#') {
      /*
       * `#$` and `#@` carry data; `#h` is the hash, and everything else is
       * prose. An unknown `#` directive is ignored rather than refused: the
       * file gains them over time and a reader that failed on one would stop
       * working when it did.
       */
      char kind = (l.pos + 1 < l.len) ? text[l.pos + 1] : '\0';
      if (kind != '$' && kind != '@') {
        continue;
      }
      l.pos += 2;
      skip_blanks(&l);
      {
        int64_t value = 0;
        if (!read_u64(&l, &value)) {
          gcu_allocator_free(allocator, entries);
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_LEAP_TABLE_MALFORMED, l.pos, 0);
        }
        /*
         * The data lines were checked for trailing junk from the start; these
         * two were not, and `read_u64` stops at the first non-digit. So
         * `#@ 40231296p0` quietly became an expiry of 40231296 - the right
         * shape, off by a factor of a hundred, and no complaint. An expiry
         * that is silently wrong is the one failure this module exists to
         * prevent, and a corruption that shortened it would be missed
         * entirely because everything past it simply reports EXPIRED.
         * `fuzz_leap` found it by mutating a digit into a letter.
         */
        if (!at_end_or_comment(&l)) {
          gcu_allocator_free(allocator, entries);
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_LEAP_TABLE_MALFORMED, l.pos, 0);
        }
        if (kind == '$') {
          updated_ntp = value;
        }
        else {
          expiry_ntp = value;
        }
      }
      continue;
    }

    /* A data line: an NTP timestamp, then the offset. */
    {
      int64_t ntp = 0;
      int64_t offset = 0;
      GCHRON_LeapEntry entry;

      if (!read_u64(&l, &ntp)) {
        gcu_allocator_free(allocator, entries);
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_LEAP_TABLE_MALFORMED, l.pos, 0);
      }
      skip_blanks(&l);
      if (!read_u64(&l, &offset)) {
        gcu_allocator_free(allocator, entries);
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_LEAP_TABLE_MALFORMED, l.pos, 0);
      }
      if (!at_end_or_comment(&l)) {
        gcu_allocator_free(allocator, entries);
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_LEAP_TABLE_MALFORMED, l.pos, 0);
      }
      if (offset > INT32_MAX) {
        gcu_allocator_free(allocator, entries);
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_LEAP_TABLE_MALFORMED, l.pos, 0);
      }

      memset(&entry, 0, sizeof(entry));
      entry.at.sec = ntp - GCHRON_NTP_TO_UNIX;
      entry.at.nsec = 0;
      entry.tai_minus_utc = (int32_t)offset;
      /*
       * The first row is not a leap second: it is the offset UTC started
       * with when it became a stepped scale in 1972. Only a *change* is an
       * inserted or removed second.
       */
      entry.negative = have_previous && entry.tai_minus_utc < previous_offset;

      if (count > 0 && entry.at.sec <= entries[count - 1].at.sec) {
        gcu_allocator_free(allocator, entries);
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_LEAP_TABLE_ORDER, line_start, 0);
      }
      if (count > 0) {
        /*
         * A leap second is one second. Every row after the first records a
         * single insertion or removal, so the offset moves by exactly one -
         * the first row is the baseline UTC started from and is the only one
         * that may be any value.
         *
         * This is not pedantry about a format. An offset that jumps further
         * than the gap between two rows makes the TAI timeline run backwards
         * across that boundary, and then UTC -> TAI -> UTC is not a round
         * trip because two UTC instants share a TAI second. `fuzz_leap` found
         * it with a row claiming a jump of 800,000 seconds, and the round
         * trip it broke is the property the whole module exists to provide.
         */
        int32_t step = entry.tai_minus_utc - previous_offset;
        if (step != 1 && step != -1) {
          gcu_allocator_free(allocator, entries);
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_LEAP_TABLE_STEP, line_start, 0);
        }
      }
      if (count >= defaults.max_leap_entries) {
        gcu_allocator_free(allocator, entries);
        return gchron_fail(err, GCHRON_ERR_LIMIT,
            GCHRON_DIAG_LEAP_TABLE_MALFORMED, line_start, 0);
      }

      if (count == capacity) {
        size_t next = (capacity == 0) ? 32 : capacity * 2;
        GCHRON_LeapEntry * grown = (GCHRON_LeapEntry *)gcu_allocator_realloc(
            allocator, entries, next * sizeof(*grown));
        if (grown == NULL) {
          gcu_allocator_free(allocator, entries);
          return gchron_fail(err, GCHRON_ERR_OOM, GCHRON_DIAG_NONE, 0, 0);
        }
        entries = grown;
        capacity = next;
      }
      entries[count++] = entry;
      previous_offset = entry.tai_minus_utc;
      have_previous = true;
    }
  }

  if (count == 0) {
    gcu_allocator_free(allocator, entries);
    return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_LEAP_TABLE_EMPTY,
        0, 0);
  }
  if (expiry_ntp < 0) {
    /*
     * Refused rather than defaulted. A table with no expiry would answer
     * every conversion forever, which is precisely the failure section 5.1
     * item 3 describes: knowledge that goes stale without saying so.
     */
    gcu_allocator_free(allocator, entries);
    return gchron_fail(err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_LEAP_TABLE_NO_EXPIRY, 0, 0);
  }

  table = (GCHRON_LeapTable *)gcu_allocator_malloc(allocator, sizeof(*table));
  if (table == NULL) {
    gcu_allocator_free(allocator, entries);
    return gchron_fail(err, GCHRON_ERR_OOM, GCHRON_DIAG_NONE, 0, 0);
  }
  memset(table, 0, sizeof(*table));
  table->allocator = allocator;
  table->entries = entries;
  table->count = count;
  table->expiry_sec = expiry_ntp - GCHRON_NTP_TO_UNIX;
  table->updated_sec = (updated_ntp < 0)
      ? entries[count - 1].at.sec
      : updated_ntp - GCHRON_NTP_TO_UNIX;
  table->is_builtin = false;
  *out = table;
  return GCHRON_OK;
}

GCHRON_Result gchron_leap_table_file(const char * path,
    const GCHRON_Allocator * allocator, GCHRON_LeapTable ** out,
    GCHRON_Error * err) {
  FILE * file = NULL;
  char * buffer = NULL;
  size_t size = 0;
  size_t filled = 0;
  GCHRON_Result result;

  if (out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  if (allocator == NULL) {
    allocator = gchron_allocator_default();
  }

  if (path == NULL) {
    /* $TZDIR, exactly as the zone database honours it. */
    const char * tzdir = getenv("TZDIR");
    if (tzdir != NULL && tzdir[0] != '\0') {
      size_t dir_len = strlen(tzdir);
      static const char name[] = "/leap-seconds.list";
      char * joined = (char *)gcu_allocator_malloc(allocator,
          dir_len + sizeof(name));
      if (joined == NULL) {
        return gchron_fail(err, GCHRON_ERR_OOM, GCHRON_DIAG_NONE, 0, 0);
      }
      memcpy(joined, tzdir, dir_len);
      memcpy(joined + dir_len, name, sizeof(name));
      file = fopen(joined, "rb");
      gcu_allocator_free(allocator, joined);
    }
    if (file == NULL) {
      file = fopen(GCHRON_LEAP_SECONDS_PATH, "rb");
    }
  }
  else {
    file = fopen(path, "rb");
  }
  if (file == NULL) {
    return gchron_fail(err, GCHRON_ERR_IO, GCHRON_DIAG_NONE, 0, 0);
  }

  /* The file is about five kilobytes; the cap is generous and finite. */
  size = 1024 * 1024;
  buffer = (char *)gcu_allocator_malloc(allocator, size);
  if (buffer == NULL) {
    fclose(file);
    return gchron_fail(err, GCHRON_ERR_OOM, GCHRON_DIAG_NONE, 0, 0);
  }
  filled = fread(buffer, 1, size, file);
  if (ferror(file) != 0) {
    fclose(file);
    gcu_allocator_free(allocator, buffer);
    return gchron_fail(err, GCHRON_ERR_IO, GCHRON_DIAG_NONE, 0, 0);
  }
  if (filled == size) {
    /* Larger than any real copy of this file; refuse rather than truncate. */
    fclose(file);
    gcu_allocator_free(allocator, buffer);
    return gchron_fail(err, GCHRON_ERR_LIMIT, GCHRON_DIAG_INPUT_TOO_LONG,
        0, 0);
  }
  fclose(file);

  result = gchron_leap_table_parse(buffer, filled, allocator, out, err);
  gcu_allocator_free(allocator, buffer);
  return result;
}

void gchron_leap_table_destroy(GCHRON_LeapTable * table) {
  if (table == NULL || table->is_builtin) {
    return;
  }
  gcu_allocator_free(table->allocator, (void *)table->entries);
  gcu_allocator_free(table->allocator, table);
}

/* ------------------------------------------------------------------ */
/* Queries                                                            */
/* ------------------------------------------------------------------ */

size_t gchron_leap_table_count(const GCHRON_LeapTable * table) {
  return (table == NULL) ? 0 : table->count;
}

GCHRON_Result gchron_leap_table_entry(const GCHRON_LeapTable * table,
    size_t index, GCHRON_LeapEntry * out) {
  if (table == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (index >= table->count) {
    return GCHRON_ERR_RANGE;
  }
  *out = table->entries[index];
  return GCHRON_OK;
}

GCHRON_Result gchron_leap_table_expiry(const GCHRON_LeapTable * table,
    GCHRON_Instant * out) {
  if (table == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  out->sec = table->expiry_sec;
  out->nsec = 0;
  return GCHRON_OK;
}

GCHRON_Result gchron_leap_table_updated(const GCHRON_LeapTable * table,
    GCHRON_Instant * out) {
  if (table == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  out->sec = table->updated_sec;
  out->nsec = 0;
  return GCHRON_OK;
}

size_t gchron_leap_index_at(const GCHRON_LeapTable * table, int64_t unix_sec) {
  size_t low = 0;
  size_t high = table->count;

  if (table->count == 0 || unix_sec < table->entries[0].at.sec) {
    return SIZE_MAX;
  }
  /* The last row whose `at` is at or before unix_sec. */
  while (low + 1 < high) {
    size_t mid = low + (high - low) / 2;
    if (table->entries[mid].at.sec <= unix_sec) {
      low = mid;
    }
    else {
      high = mid;
    }
  }
  return low;
}

GCHRON_Result gchron_leap_check_span(const GCHRON_LeapTable * table,
    int64_t unix_sec) {
  if (table == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (table->count == 0 || unix_sec < table->entries[0].at.sec) {
    /*
     * Before 1972 UTC ran on "rubber seconds" - the second itself was
     * stretched, and TAI-UTC was not an integer - so there is no offset to
     * report rather than a wrong one.
     */
    return GCHRON_ERR_RANGE;
  }
  if (unix_sec >= table->expiry_sec) {
    return GCHRON_ERR_EXPIRED;
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_leap_offset_at(const GCHRON_LeapTable * table,
    GCHRON_Instant utc, int32_t * out) {
  GCHRON_Result result;
  size_t index;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_leap_check_span(table, utc.sec);
  if (result != GCHRON_OK) {
    return result;
  }
  index = gchron_leap_index_at(table, utc.sec);
  *out = table->entries[index].tai_minus_utc;
  return GCHRON_OK;
}

GCHRON_Result gchron_leap_is_leap_day(const GCHRON_LeapTable * table,
    const GCHRON_Date * date, bool * out) {
  int64_t epoch_day = 0;
  int64_t end_of_day;
  GCHRON_Result result;
  size_t i;

  if (table == NULL || date == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_date_to_epoch_day(date, &epoch_day);
  if (result != GCHRON_OK) {
    return result;
  }
  /*
   * A leap second is inserted at the *end* of a day, and the table records
   * the offset that begins at the midnight after it. So the day in question
   * is a leap day exactly when a row starts at the following midnight and
   * that row raised the offset.
   */
  end_of_day = (epoch_day + 1) * GCHRON_SECONDS_PER_DAY;

  result = gchron_leap_check_span(table, end_of_day - 1);
  if (result != GCHRON_OK) {
    return result;
  }

  *out = false;
  i = gchron_leap_index_at(table, end_of_day);
  if (i != SIZE_MAX && i > 0 && table->entries[i].at.sec == end_of_day
      && table->entries[i].tai_minus_utc
          > table->entries[i - 1].tai_minus_utc) {
    *out = true;
  }
  return GCHRON_OK;
}

void gchron_leap_table_dump(const GCHRON_LeapTable * table, FILE * stream) {
  size_t i;

  if (stream == NULL) {
    stream = stdout;
  }
  if (table == NULL) {
    fprintf(stream, "leap table: none\n");
    return;
  }
  fprintf(stream, "leap table: %s, %zu entries\n",
      table->is_builtin ? "built in" : "loaded", table->count);
  fprintf(stream, "  updated %" PRId64 "  expires %" PRId64 " (Unix seconds)\n",
      table->updated_sec, table->expiry_sec);
  for (i = 0; i < table->count; ++i) {
    GCHRON_Date date;
    int64_t day = gchron_floor_div(table->entries[i].at.sec,
        GCHRON_SECONDS_PER_DAY);
    if (gchron_date_from_epoch_day(day, &date) == GCHRON_OK) {
      fprintf(stream, "  %04d-%02d-%02d  TAI-UTC %d%s\n", (int)date.year,
          date.month, date.day, (int)table->entries[i].tai_minus_utc,
          table->entries[i].negative ? "  (negative leap)" : "");
    }
  }
}
