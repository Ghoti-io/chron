/**
 * @file
 *
 * HTTP-date and RFC 5322: the two formats that predate RFC 3339 and that
 * everybody still has to read.
 *
 * Both have a two-digit-year form, and both therefore need to know what year
 * it is now - which is why they take a GCHRON_Clock. design.md, mistake M18:
 * a library that called the system clock for this would have a rule nobody
 * could test until the year it mattered.
 *
 * Reference: RFC 9110 section 5.6.7; RFC 5322 sections 3.3 and 4.3.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/clock.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/format.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/offset.h>
#include <string.h>

#include "../core/core_internal.h"

/** Month names, as both formats spell them. */
static const char * const MONTHS[13] = {
  NULL, "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
  "Nov", "Dec"
};

/** Day names, ISO-indexed. */
static const char * const DAYS[8] = {
  NULL, "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};

/** The long day names RFC 850 uses. */
static const char * const LONG_DAYS[8] = {
  NULL, "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
  "Sunday"
};

/** A cursor over the input. */
typedef struct Cursor {
  const char * text;
  size_t len;
  size_t pos;
} Cursor;

static bool at_end(const Cursor * c) {
  return c->pos >= c->len;
}

static void skip_spaces(Cursor * c) {
  while (!at_end(c) && (c->text[c->pos] == ' ' || c->text[c->pos] == '\t')) {
    c->pos += 1;
  }
}

/** Match a literal, case-insensitively for the alphabetic parts. */
static bool take_literal(Cursor * c, const char * text) {
  size_t len = strlen(text);
  size_t i;

  if (c->len - c->pos < len) {
    return false;
  }
  for (i = 0; i < len; ++i) {
    char a = c->text[c->pos + i];
    char b = text[i];
    if (a >= 'A' && a <= 'Z') {
      a = (char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
      b = (char)(b - 'A' + 'a');
    }
    if (a != b) {
      return false;
    }
  }
  c->pos += len;
  return true;
}

/** Read exactly @p count digits. */
static bool take_digits(Cursor * c, int count, int * out) {
  int value = 0;
  int i;

  if (c->len - c->pos < (size_t)count) {
    return false;
  }
  for (i = 0; i < count; ++i) {
    char ch = c->text[c->pos + (size_t)i];
    if (ch < '0' || ch > '9') {
      return false;
    }
    value = value * 10 + (ch - '0');
  }
  c->pos += (size_t)count;
  *out = value;
  return true;
}

/** Read one or two digits, which RFC 5322 permits for the day. */
static bool take_1_or_2_digits(Cursor * c, int * out) {
  if (take_digits(c, 2, out)) {
    return true;
  }
  return take_digits(c, 1, out);
}

/** Read a three-letter month name. */
static bool take_month(Cursor * c, int * out) {
  int month;
  for (month = 1; month <= 12; ++month) {
    size_t saved = c->pos;
    if (take_literal(c, MONTHS[month])) {
      *out = month;
      return true;
    }
    c->pos = saved;
  }
  return false;
}

/** Read a day name, long or short, and discard it. */
static bool take_day_name(Cursor * c, bool long_form) {
  int day;
  for (day = 1; day <= 7; ++day) {
    size_t saved = c->pos;
    if (take_literal(c, long_form ? LONG_DAYS[day] : DAYS[day])) {
      return true;
    }
    c->pos = saved;
  }
  return false;
}

/** Read `HH:MM:SS`. */
static bool take_time(Cursor * c, GCHRON_Time * out) {
  int hour;
  int minute;
  int second;

  if (!take_digits(c, 2, &hour) || at_end(c) || c->text[c->pos] != ':') {
    return false;
  }
  c->pos += 1;
  if (!take_digits(c, 2, &minute) || at_end(c) || c->text[c->pos] != ':') {
    return false;
  }
  c->pos += 1;
  if (!take_digits(c, 2, &second)) {
    return false;
  }
  /* A leap second in an HTTP header is not a thing anybody sends, and
   * GCHRON_Time holds 0..59; clamping matches what the parsers do with `:60`
   * elsewhere, without a flag to record it because these formats have no way
   * to say it either. */
  return gchron_time_create(hour, minute, second > 59 ? 59 : second, 0, out)
      == GCHRON_OK;
}

/**
 * Expand a two-digit year, per RFC 9110 section 5.6.7.
 *
 * "Recipients of a timestamp value in rfc850-date format, which uses a
 * two-digit year, MUST interpret a timestamp that appears to be more than 50
 * years in the future as representing the most recent year in the past that
 * had the same last two digits."
 *
 * Which needs to know what year it is now, and is exactly why this function -
 * and the two parsers that call it - take a clock rather than reading one.
 */
static GCHRON_Result expand_two_digit_year(int two_digits,
    const GCHRON_Clock * clock, int32_t * out) {
  GCHRON_Instant now;
  GCHRON_DateTime civil;
  int32_t century;
  int32_t candidate;
  GCHRON_Result result;

  if (clock == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_clock_now(clock, &now);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_instant_to_utc(&now, &civil);
  if (result != GCHRON_OK) {
    return result;
  }
  century = (int32_t)(gchron_floor_div(civil.date.year, 100) * 100);
  candidate = century + two_digits;
  if (candidate - civil.date.year > 50) {
    candidate -= 100;
  }
  else if (civil.date.year - candidate > 50) {
    candidate += 100;
  }
  *out = candidate;
  return GCHRON_OK;
}

GCHRON_Result gchron_parse_http_date(const char * text, size_t len,
    const GCHRON_Clock * clock, const GCHRON_ParseOptions * opts,
    GCHRON_OffsetDateTime * out, GCHRON_ParseInfo * info,
    GCHRON_Error * err) {
  Cursor c;
  GCHRON_DateTime civil;
  int day = 0;
  int month = 0;
  int year = 0;
  size_t saved;

  gchron_error_clear(err);
  gchron_parse_info_clear(info);
  (void)opts;
  if (text == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  c.text = text;
  c.len = len;
  c.pos = 0;

  /* 1. IMF-fixdate: `Sun, 06 Nov 1994 08:49:37 GMT`. What a sender must
   *    produce, and what a recipient sees almost always. */
  saved = c.pos;
  if (take_day_name(&c, false) && !at_end(&c) && c.text[c.pos] == ','
      && (c.pos += 1, skip_spaces(&c), take_digits(&c, 2, &day))
      && !at_end(&c) && c.text[c.pos] == ' '
      && (c.pos += 1, take_month(&c, &month)) && !at_end(&c)
      && c.text[c.pos] == ' '
      && (c.pos += 1, take_digits(&c, 4, &year)) && !at_end(&c)
      && c.text[c.pos] == ' '
      && (c.pos += 1, take_time(&c, &civil.time))) {
    skip_spaces(&c);
    if (take_literal(&c, "GMT") && c.pos == c.len) {
      if (gchron_date_create((int32_t)year, month, day, &civil.date)
          != GCHRON_OK) {
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_DAY_OUT_OF_RANGE, 0, len);
      }
      if (info != NULL) {
        info->consumed = c.pos;
      }
      return gchron_offset_create(&civil, 0, false, out);
    }
  }
  c.pos = saved;

  /* 2. RFC 850: `Sunday, 06-Nov-94 08:49:37 GMT`. Obsolete, and a recipient
   *    must still accept it. */
  if (take_day_name(&c, true) && !at_end(&c) && c.text[c.pos] == ','
      && (c.pos += 1, skip_spaces(&c), take_digits(&c, 2, &day))
      && !at_end(&c) && c.text[c.pos] == '-'
      && (c.pos += 1, take_month(&c, &month)) && !at_end(&c)
      && c.text[c.pos] == '-'
      && (c.pos += 1, take_digits(&c, 2, &year)) && !at_end(&c)
      && c.text[c.pos] == ' '
      && (c.pos += 1, take_time(&c, &civil.time))) {
    int32_t expanded = 0;
    GCHRON_Result result;
    skip_spaces(&c);
    if (take_literal(&c, "GMT") && c.pos == c.len) {
      result = expand_two_digit_year(year, clock, &expanded);
      if (result != GCHRON_OK) {
        return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, len);
      }
      if (gchron_date_create(expanded, month, day, &civil.date) != GCHRON_OK) {
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_DAY_OUT_OF_RANGE, 0, len);
      }
      if (info != NULL) {
        info->consumed = c.pos;
      }
      return gchron_offset_create(&civil, 0, false, out);
    }
  }
  c.pos = saved;

  /* 3. `asctime`: `Sun Nov  6 08:49:37 1994`. The day is space-padded, and
   *    there is no zone at all - the RFC says to read it as GMT anyway. */
  if (take_day_name(&c, false) && !at_end(&c) && c.text[c.pos] == ' '
      && (c.pos += 1, take_month(&c, &month)) && !at_end(&c)
      && c.text[c.pos] == ' ') {
    c.pos += 1;
    skip_spaces(&c);
    if (take_1_or_2_digits(&c, &day) && !at_end(&c) && c.text[c.pos] == ' '
        && (c.pos += 1, take_time(&c, &civil.time)) && !at_end(&c)
        && c.text[c.pos] == ' '
        && (c.pos += 1, take_digits(&c, 4, &year)) && c.pos == c.len) {
      if (gchron_date_create((int32_t)year, month, day, &civil.date)
          != GCHRON_OK) {
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_DAY_OUT_OF_RANGE, 0, len);
      }
      if (info != NULL) {
        info->consumed = c.pos;
      }
      return gchron_offset_create(&civil, 0, false, out);
    }
  }

  return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_NONE, 0, len);
}

GCHRON_Result gchron_write_http_date(const GCHRON_OffsetDateTime * odt,
    char * buf, size_t buf_len, size_t * out_len) {
  GCHRON_OffsetDateTime gmt;
  GCHRON_Result result;
  int weekday = 0;
  char scratch[GCHRON_HTTP_DATE_MAX];
  size_t length;

  if ((buf == NULL && buf_len != 0) || !gchron_offset_is_valid(odt)) {
    return GCHRON_ERR_INVALID;
  }
  /* Always GMT, as RFC 9110 requires of a sender, so a caller may pass a
   * value with any offset and get the right answer. */
  result = gchron_offset_with_offset(odt, 0, &gmt);
  if (result != GCHRON_OK) {
    return result;
  }
  if (gmt.civil.date.year < 0 || gmt.civil.date.year > 9999) {
    return GCHRON_ERR_RANGE;
  }
  result = gchron_date_day_of_week(&gmt.civil.date, &weekday);
  if (result != GCHRON_OK) {
    return result;
  }

  length = (size_t)snprintf(scratch, sizeof(scratch),
      "%s, %02u %s %04d %02u:%02u:%02u GMT", DAYS[weekday],
      (unsigned)gmt.civil.date.day, MONTHS[gmt.civil.date.month],
      gmt.civil.date.year, (unsigned)gmt.civil.time.hour,
      (unsigned)gmt.civil.time.minute, (unsigned)gmt.civil.time.second);
  if (out_len != NULL) {
    *out_len = length;
  }
  if (buf_len < length + 1) {
    return GCHRON_ERR_LIMIT;
  }
  memcpy(buf, scratch, length + 1);
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * RFC 5322
 *--------------------------------------------------------------------------*/

/** Skip folding whitespace and comments, which RFC 5322 permits anywhere. */
static void skip_cfws(Cursor * c) {
  for (;;) {
    if (at_end(c)) {
      return;
    }
    if (c->text[c->pos] == ' ' || c->text[c->pos] == '\t'
        || c->text[c->pos] == '\r' || c->text[c->pos] == '\n') {
      c->pos += 1;
      continue;
    }
    if (c->text[c->pos] == '(') {
      /* A comment, which may nest. Bounded by the input length, so a
       * malformed one ends the scan rather than looping. */
      int depth = 0;
      while (!at_end(c)) {
        if (c->text[c->pos] == '(') {
          depth += 1;
        }
        else if (c->text[c->pos] == ')') {
          depth -= 1;
          if (depth == 0) {
            c->pos += 1;
            break;
          }
        }
        else if (c->text[c->pos] == '\\' && c->pos + 1 < c->len) {
          c->pos += 1;
        }
        c->pos += 1;
      }
      continue;
    }
    return;
  }
}

/** RFC 5322 section 4.3's obsolete zone names. */
typedef struct ObsoleteZone {
  const char * name;
  int32_t offset_sec;
} ObsoleteZone;

static const ObsoleteZone OBSOLETE_ZONES[] = {
  { "UT", 0 }, { "GMT", 0 },
  { "EST", -5 * 3600 }, { "EDT", -4 * 3600 },
  { "CST", -6 * 3600 }, { "CDT", -5 * 3600 },
  { "MST", -7 * 3600 }, { "MDT", -6 * 3600 },
  { "PST", -8 * 3600 }, { "PDT", -7 * 3600 },
};

GCHRON_Result gchron_parse_rfc5322(const char * text, size_t len,
    const GCHRON_Clock * clock, const GCHRON_ParseOptions * opts,
    GCHRON_OffsetDateTime * out, GCHRON_ParseInfo * info,
    GCHRON_Error * err) {
  Cursor c;
  GCHRON_DateTime civil;
  int day = 0;
  int month = 0;
  int year = 0;
  int32_t offset_sec = 0;
  bool offset_unknown = false;
  size_t saved;

  gchron_error_clear(err);
  gchron_parse_info_clear(info);
  (void)opts;
  if (text == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  c.text = text;
  c.len = len;
  c.pos = 0;

  skip_cfws(&c);
  /* The day name is optional in the grammar, and is advisory when present -
   * a `Date:` whose weekday disagrees with its date is still that date. */
  saved = c.pos;
  if (take_day_name(&c, false)) {
    skip_cfws(&c);
    if (!at_end(&c) && c.text[c.pos] == ',') {
      c.pos += 1;
    }
    else {
      c.pos = saved;
    }
  }
  skip_cfws(&c);

  if (!take_1_or_2_digits(&c, &day)) {
    return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_EXPECTED_DIGIT,
        c.pos, 0);
  }
  skip_cfws(&c);
  if (!take_month(&c, &month)) {
    return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_MONTH_OUT_OF_RANGE,
        c.pos, 0);
  }
  skip_cfws(&c);

  {
    /* Four digits, or the obsolete two or three. */
    size_t before = c.pos;
    if (take_digits(&c, 4, &year)) {
      /* done */
    }
    else if ((c.pos = before, take_digits(&c, 3, &year))) {
      /* RFC 5322 section 4.3: a three-digit year is 1900 + the value. */
      year += 1900;
    }
    else if ((c.pos = before, take_digits(&c, 2, &year))) {
      int32_t expanded = 0;
      /*
       * Section 4.3's own rule, which is not the HTTP one: 00-49 is 2000-2049
       * and 50-99 is 1950-1999. It needs no clock, which is why an RFC 5322
       * date parses with a NULL one where an RFC 850 HTTP-date does not.
       */
      expanded = (year < 50) ? 2000 + year : 1900 + year;
      year = expanded;
      (void)clock;
    }
    else {
      return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_EXPECTED_DIGIT,
          c.pos, 0);
    }
  }

  skip_cfws(&c);
  if (!take_time(&c, &civil.time)) {
    return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_EXPECTED_DIGIT,
        c.pos, 0);
  }
  skip_cfws(&c);

  if (!at_end(&c) && (c.text[c.pos] == '+' || c.text[c.pos] == '-')) {
    bool negative = (c.text[c.pos] == '-');
    int hours = 0;
    int minutes = 0;
    c.pos += 1;
    if (!take_digits(&c, 2, &hours) || !take_digits(&c, 2, &minutes)) {
      return gchron_fail(err, GCHRON_ERR_FORMAT,
          GCHRON_DIAG_OFFSET_OUT_OF_RANGE, c.pos, 0);
    }
    if (minutes > 59 || hours > 23) {
      return gchron_fail(err, GCHRON_ERR_FORMAT,
          GCHRON_DIAG_OFFSET_OUT_OF_RANGE, c.pos, 0);
    }
    offset_sec = (int32_t)((hours * 3600 + minutes * 60)
        * (negative ? -1 : 1));
    /* `-0000` means the offset is unknown, exactly as RFC 3339's `-00:00`
     * does - and RFC 5322 section 3.3 says so in as many words (mistake
     * M14). */
    offset_unknown = (negative && offset_sec == 0);
  }
  else {
    size_t i;
    bool matched = false;
    for (i = 0; i < sizeof(OBSOLETE_ZONES) / sizeof(OBSOLETE_ZONES[0]); ++i) {
      size_t before = c.pos;
      if (take_literal(&c, OBSOLETE_ZONES[i].name)) {
        offset_sec = OBSOLETE_ZONES[i].offset_sec;
        matched = true;
        break;
      }
      c.pos = before;
    }
    if (!matched) {
      if (!at_end(&c)
          && ((c.text[c.pos] >= 'A' && c.text[c.pos] <= 'Z')
              || (c.text[c.pos] >= 'a' && c.text[c.pos] <= 'z'))) {
        /*
         * A military zone. Section 4.3: they "SHOULD be considered equivalent
         * to -0000 unless there is out-of-band information confirming their
         * meaning", because they were so widely got backwards. Treated as an
         * unknown offset, which is exactly what -0000 means.
         */
        c.pos += 1;
        offset_sec = 0;
        offset_unknown = true;
      }
      else {
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_EXPECTED_OFFSET, c.pos, 0);
      }
    }
  }

  skip_cfws(&c);
  if (c.pos != c.len) {
    return gchron_fail(err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_TRAILING_CHARACTERS, c.pos, c.len - c.pos);
  }
  if (gchron_date_create((int32_t)year, month, day, &civil.date)
      != GCHRON_OK) {
    return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_DAY_OUT_OF_RANGE,
        0, len);
  }
  if (info != NULL) {
    info->consumed = c.pos;
    info->offset_unknown = offset_unknown;
  }
  return gchron_offset_create(&civil, offset_sec, offset_unknown, out);
}

GCHRON_Result gchron_write_rfc5322(const GCHRON_OffsetDateTime * odt,
    char * buf, size_t buf_len, size_t * out_len) {
  char scratch[GCHRON_RFC5322_MAX];
  int weekday = 0;
  size_t length;
  int32_t magnitude;
  GCHRON_Result result;

  if ((buf == NULL && buf_len != 0) || !gchron_offset_is_valid(odt)) {
    return GCHRON_ERR_INVALID;
  }
  if (odt->civil.date.year < 0 || odt->civil.date.year > 9999) {
    return GCHRON_ERR_RANGE;
  }
  result = gchron_date_day_of_week(&odt->civil.date, &weekday);
  if (result != GCHRON_OK) {
    return result;
  }

  magnitude = odt->offset_sec < 0 ? -odt->offset_sec : odt->offset_sec;
  length = (size_t)snprintf(scratch, sizeof(scratch),
      "%s, %02u %s %04d %02u:%02u:%02u %c%02d%02d", DAYS[weekday],
      (unsigned)odt->civil.date.day, MONTHS[odt->civil.date.month],
      odt->civil.date.year, (unsigned)odt->civil.time.hour,
      (unsigned)odt->civil.time.minute, (unsigned)odt->civil.time.second,
      /* An unknown offset writes `-0000`, which is the whole of what that
       * spelling means. */
      (odt->offset_unknown || odt->offset_sec < 0) ? '-' : '+',
      (int)(magnitude / 3600), (int)((magnitude / 60) % 60));
  if (out_len != NULL) {
    *out_len = length;
  }
  if (buf_len < length + 1) {
    return GCHRON_ERR_LIMIT;
  }
  memcpy(buf, scratch, length + 1);
  return GCHRON_OK;
}
