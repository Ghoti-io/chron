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
 * The leap-second table: reading `leap-seconds.list`, and answering questions
 * about what it holds.
 *
 * The format is NIST's, republished by the IERS. Comment lines begin `#`,
 * with two carrying data of their own: `#$` is when the file was last
 * written, `#@` is when it expires. Data lines are an NTP timestamp and the
 * TAI-UTC offset that begins at it.
 *
 * Reference: https://hpiers.obspm.fr/iers/bul/bulc/ntp/leap-seconds.list
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/leap.h>
#include <ghoti.io/chron/macros.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/cutil/file.h>
#include <ghoti.io/cutil/path.h>
#include <ghoti.io/cutil/safemath.h>

#include "../core/core_internal.h"
#include "leap_internal.h"

/** The usual place, when a caller names no path. */
#ifndef GCHRON_LEAP_SECONDS_PATH
#define GCHRON_LEAP_SECONDS_PATH "/usr/share/zoneinfo/leap-seconds.list"
#endif

/** The name inside a zoneinfo directory. */
#define GCHRON_LEAP_SECONDS_NAME "leap-seconds.list"

/**
 * The most this file is allowed to be.
 *
 * The real one is about five kilobytes; the cap is generous and finite. It is
 * a refusal rather than a truncation - half a leap-second table parses
 * perfectly well and answers wrongly.
 */
#define GCHRON_LEAP_SECONDS_MAX ((size_t)(1024 * 1024))

/* ------------------------------------------------------------------ */
/* Parsing                                                            */
/* ------------------------------------------------------------------ */

/** A cursor over one line of the leap-second file. */
typedef struct LeapCursor {
  const char * text;
  size_t len;
  size_t pos;
} LeapCursor;

static void skip_blanks(LeapCursor * l) {
  while (l->pos < l->len && (l->text[l->pos] == ' ' || l->text[l->pos] == '\t')) {
    l->pos += 1;
  }
}

/**
 * Read an unsigned decimal.
 *
 * @return true when at least one digit was read and nothing overflowed.
 */
static bool read_u64(LeapCursor * l, int64_t * out) {
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
static bool at_end_or_comment(const LeapCursor * l) {
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
    LeapCursor l;

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

/**
 * `<dir>/leap-seconds.list`, allocated, or NULL.
 *
 * cutil's join owns the separator question, which matters more here than it
 * looks: the string that was concatenated by hand always wrote one, so a
 * `TZDIR` ending in a slash produced a doubled one. POSIX collapses that and
 * the file still opened, which is why nobody noticed - but the same hand-
 * written rule is the one that has to work on Windows, where the separator is
 * not `/` at all.
 */
static char * leap_path_in(const char * dir,
    const GCHRON_Allocator * allocator) {
  size_t length = 0;
  size_t size;
  char * joined;

  if (dir == NULL || dir[0] == '\0') {
    return NULL;
  }
  if (gcu_path_join(GCU_PATH_NATIVE, dir, GCHRON_LEAP_SECONDS_NAME, NULL, 0,
          &length) != GCU_PATH_OK
      || !gcu_safe_add_size(length, 1, &size)) {
    return NULL;
  }
  joined = (char *)gcu_allocator_malloc(allocator, size);
  if (joined == NULL) {
    return NULL;
  }
  if (gcu_path_join(GCU_PATH_NATIVE, dir, GCHRON_LEAP_SECONDS_NAME, joined,
          size, NULL) != GCU_PATH_OK) {
    gcu_allocator_free(allocator, joined);
    return NULL;
  }
  return joined;
}

GCHRON_Result gchron_leap_table_file(const char * path,
    const GCHRON_Allocator * allocator, GCHRON_LeapTable ** out,
    GCHRON_Error * err) {
  void * data = NULL;
  size_t len = 0;
  GCU_File_Result read;
  GCHRON_Result result;

  if (out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  if (allocator == NULL) {
    allocator = gchron_allocator_default();
  }

  if (path != NULL) {
    read = gcu_file_read(path, GCHRON_LEAP_SECONDS_MAX, allocator, &data,
        &len);
  }
  else {
    /* $TZDIR, exactly as the zone database honours it. */
    const char * tzdir = getenv("TZDIR");

    /*
     * No $TZDIR is not a failure to find anything - it is not having asked,
     * which is the ordinary case and goes straight on to the usual place.
     */
    read = GCU_FILE_ERR_NOT_FOUND;
    if (tzdir != NULL && tzdir[0] != '\0') {
      char * joined = leap_path_in(tzdir, allocator);
      if (joined == NULL) {
        /*
         * $TZDIR is set and the path could not be built. Nothing was learned
         * about what is on disk, so this deliberately does *not* match the
         * condition below: running out of memory is reported rather than
         * answered by reading a different file, and a caller told "here is
         * the system table" would have no way to learn that the copy it
         * asked for was never looked at. Allocation is the only way to get
         * here that a running program can reach - joining a non-empty
         * directory to a fixed file name fails otherwise only on a size
         * overflow.
         */
        read = GCU_FILE_ERR_OOM;
      }
      else {
        read = gcu_file_read(joined, GCHRON_LEAP_SECONDS_MAX, allocator,
            &data, &len);
        gcu_allocator_free(allocator, joined);
      }
    }
    /*
     * **Failing to obtain the file sends the search on. A failure about the
     * file's contents, or about us, is the answer.**
     *
     * Three spellings of the first: nothing was there, it was there and this
     * process could not open it, and the path cannot name anything on this
     * filesystem. cutil distinguishes them and a search path has no use for
     * the distinction - each means this location did not yield a file and the
     * next one is where to look. The library already treats `$TZDIR` naming a
     * directory that does not exist, or a plain file, or a loop of symbolic
     * links as an ordinary miss; a path too long is the fourth spelling of
     * the same misconfiguration, and refusing only that one would be a rule
     * with nothing behind it. zonedb.c calls the same errno an environment
     * fact rather than a caller's error for the same reason.
     *
     * GCU_FILE_ERR_OOM is deliberately outside the set, and is the one that
     * looks like it belongs. Running out of memory is not a fact about the
     * filesystem at all - we never got to ask - and going on to the next
     * location means allocating more under memory pressure and then handing
     * back a table the caller did not ask for, with no way to learn that the
     * copy it named was never read. It is the same judgement the sentinel
     * above makes about a path that could not be built.
     *
     * GCU_FILE_ERR_LIMIT and a plain GCU_FILE_ERR_IO are answers too: the
     * file was obtained and is too large, or the device failed part way
     * through. Note that this is the first cutil that can tell those from an
     * absent file - the comment here has described this rule since before the
     * vocabulary existed to express it, and every failure to open used to
     * arrive as GCU_FILE_ERR_IO and fall through.
     */
    if (read == GCU_FILE_ERR_NOT_FOUND || read == GCU_FILE_ERR_ACCESS
        || read == GCU_FILE_ERR_INVALID) {
      read = gcu_file_read(GCHRON_LEAP_SECONDS_PATH, GCHRON_LEAP_SECONDS_MAX,
          allocator, &data, &len);
    }
  }

  switch (read) {
    case GCU_FILE_OK:
      break;
    case GCU_FILE_ERR_LIMIT:
      return gchron_fail(err, GCHRON_ERR_LIMIT, GCHRON_DIAG_INPUT_TOO_LONG,
          0, 0);
    case GCU_FILE_ERR_OOM:
      return gchron_fail(err, GCHRON_ERR_OOM, GCHRON_DIAG_NONE, 0, 0);
    /* A default for the reason zonedb.c's says: a value cutil adds later
     * is still a read that failed, and spelling out the list would make it a
     * build failure or an invented internal error instead. */
    default:
      return gchron_fail(err, GCHRON_ERR_IO, GCHRON_DIAG_NONE, 0, 0);
  }

  result = gchron_leap_table_parse((const char *)data, len, allocator, out,
      err);
  gcu_file_free(allocator, data);
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
