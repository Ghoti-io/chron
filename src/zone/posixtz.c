/**
 * @file
 *
 * The POSIX `TZ` string: `EST5EDT,M3.2.0,M11.1.0`.
 *
 * This is not an optional extra. Since `zic` 2020b the default is `-b slim`,
 * where a zone's transition table ends at its most recent rule change and
 * this string carries the rule forward for ever after; without it every zone
 * on such a system stops working at that point. On a `-b fat` system - which
 * is what Debian ships, and what this machine has - the tables run to 2037
 * and every zone stops there instead. Either way the footer is what answers
 * a question about next year.
 *
 * It arrives in the footer of every version-2 TZif file and in the `TZ`
 * environment variable, so design.md section 1.2 counts it among the
 * untrusted inputs and it is fuzzed.
 *
 * Reference: POSIX.1-2024, *Environment Variables*, `TZ`, including the
 * extended -167..167 hour range for transition times; RFC 8536 section 3.3.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/zone.h>
#include <string.h>

#include <ghoti.io/cutil/allocator.h>

#include "../core/core_internal.h"
#include "zone_internal.h"

/** Seconds a daylight-saving offset runs ahead of standard when unstated. */
#define DEFAULT_DST_SHIFT INT32_C(3600)

/** Seconds after local midnight a transition falls at when unstated. */
#define DEFAULT_RULE_TIME INT32_C(7200)

/** The widest offset POSIX permits: 24:59:59 either way. */
#define MAX_TZ_OFFSET (INT32_C(24) * 3600 + 59 * 60 + 59)

/**
 * The widest transition time POSIX.1-2024 permits: 167:59:59 either way.
 *
 * A full week in each direction, so a transition may be named as falling in
 * one week and actually land in another. The evaluation below must not assume
 * it lands inside the day the rule names, and that is the whole reason this
 * bound is worth stating.
 */
#define MAX_TZ_RULE_TIME (INT32_C(167) * 3600 + 59 * 60 + 59)

/** A cursor over the rule string. */
typedef struct Cursor {
  const char * text;
  size_t len;
  size_t pos;
} Cursor;

static bool at_end(const Cursor * c) {
  return c->pos >= c->len;
}

static char peek(const Cursor * c) {
  return at_end(c) ? '\0' : c->text[c->pos];
}

static bool is_digit(char ch) {
  return ch >= '0' && ch <= '9';
}

/**
 * Read a time-zone abbreviation.
 *
 * Two spellings: three or more alphabetic characters, or anything at all
 * inside angle brackets - which is how a zone whose abbreviation is `+05` or
 * `-04` writes it, since a bare `+` would be read as an offset sign.
 */
static GCHRON_Result scan_abbrev(Cursor * c, char * out) {
  size_t start;
  size_t length;

  if (peek(c) == '<') {
    c->pos += 1;
    start = c->pos;
    while (!at_end(c) && c->text[c->pos] != '>') {
      char ch = c->text[c->pos];
      bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
          || is_digit(ch) || ch == '+' || ch == '-';
      if (!ok) {
        return GCHRON_ERR_FORMAT;
      }
      c->pos += 1;
    }
    if (at_end(c)) {
      return GCHRON_ERR_FORMAT;
    }
    length = c->pos - start;
    c->pos += 1; /* the '>' */
  }
  else {
    start = c->pos;
    while (!at_end(c)) {
      char ch = c->text[c->pos];
      if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))) {
        break;
      }
      c->pos += 1;
    }
    length = c->pos - start;
  }

  /* POSIX requires at least three characters, and every abbreviation in the
   * tzdb has them. Accepting fewer would make `E5` parse as an abbreviation
   * `E` with offset 5 rather than as the malformed string it is. */
  if (length < 3) {
    return GCHRON_ERR_FORMAT;
  }
  if (length > GCHRON_ABBREV_MAX) {
    return GCHRON_ERR_FORMAT;
  }
  memcpy(out, c->text + start, length);
  out[length] = '\0';
  return GCHRON_OK;
}

/** Read `1*DIGIT` with an upper bound, so a hostile string cannot spin. */
static GCHRON_Result scan_number(Cursor * c, int max, int * out) {
  int value = 0;
  size_t digits = 0;

  while (!at_end(c) && is_digit(c->text[c->pos])) {
    value = value * 10 + (c->text[c->pos] - '0');
    digits += 1;
    c->pos += 1;
    if (value > max || digits > 6) {
      return GCHRON_ERR_FORMAT;
    }
  }
  if (digits == 0) {
    return GCHRON_ERR_FORMAT;
  }
  *out = value;
  return GCHRON_OK;
}

/**
 * Read `[+|-]hh[:mm[:ss]]`.
 *
 * @param max_hours The largest hour field this position permits: 24 for an
 *   offset, 167 for a transition time.
 * @param out Receives the value in seconds, **with the sign as written**.
 */
static GCHRON_Result scan_hms(Cursor * c, int max_hours, int32_t * out) {
  bool negative = false;
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  GCHRON_Result result;

  if (peek(c) == '+' || peek(c) == '-') {
    negative = (peek(c) == '-');
    c->pos += 1;
  }
  result = scan_number(c, max_hours, &hours);
  if (result != GCHRON_OK) {
    return result;
  }
  if (peek(c) == ':') {
    c->pos += 1;
    result = scan_number(c, 59, &minutes);
    if (result != GCHRON_OK) {
      return result;
    }
    if (peek(c) == ':') {
      c->pos += 1;
      result = scan_number(c, 59, &seconds);
      if (result != GCHRON_OK) {
        return result;
      }
    }
  }
  *out = (int32_t)(hours * 3600 + minutes * 60 + seconds);
  if (negative) {
    *out = -*out;
  }
  return GCHRON_OK;
}

/** Read one end of the rule: `Jn`, `n`, or `Mm.w.d`, with an optional time. */
static GCHRON_Result scan_rule_date(Cursor * c, GCHRON_TzRuleDate * out) {
  GCHRON_Result result;
  int value;

  memset(out, 0, sizeof(*out));
  out->time = DEFAULT_RULE_TIME;

  if (peek(c) == 'J') {
    c->pos += 1;
    result = scan_number(c, 365, &value);
    if (result != GCHRON_OK) {
      return result;
    }
    /* `Jn` is 1..365 and never counts 29 February, so there is no J366. */
    if (value < 1) {
      return GCHRON_ERR_FORMAT;
    }
    out->kind = GCHRON_TZRULE_JULIAN;
    out->n = value;
  }
  else if (peek(c) == 'M') {
    int month;
    int week;
    int day;
    c->pos += 1;
    result = scan_number(c, 12, &month);
    if (result != GCHRON_OK || month < 1) {
      return GCHRON_ERR_FORMAT;
    }
    if (peek(c) != '.') {
      return GCHRON_ERR_FORMAT;
    }
    c->pos += 1;
    result = scan_number(c, 5, &week);
    if (result != GCHRON_OK || week < 1) {
      return GCHRON_ERR_FORMAT;
    }
    if (peek(c) != '.') {
      return GCHRON_ERR_FORMAT;
    }
    c->pos += 1;
    result = scan_number(c, 6, &day);
    if (result != GCHRON_OK) {
      return result;
    }
    out->kind = GCHRON_TZRULE_MONTH;
    out->month = month;
    out->week = week;
    out->day = day;
  }
  else if (is_digit(peek(c))) {
    result = scan_number(c, 365, &value);
    if (result != GCHRON_OK) {
      return result;
    }
    /* Bare `n` is 0..365 and does count 29 February. */
    out->kind = GCHRON_TZRULE_ZERO;
    out->n = value;
  }
  else {
    return GCHRON_ERR_FORMAT;
  }

  if (peek(c) == '/') {
    int32_t when;
    c->pos += 1;
    result = scan_hms(c, 167, &when);
    if (result != GCHRON_OK) {
      return result;
    }
    if (when < -MAX_TZ_RULE_TIME || when > MAX_TZ_RULE_TIME) {
      return GCHRON_ERR_FORMAT;
    }
    out->time = when;
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_posixtz_parse(const char * text, size_t len,
    GCHRON_PosixTz * out) {
  Cursor c;
  GCHRON_Result result;
  int32_t written;

  if (text == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  memset(out, 0, sizeof(*out));

  c.text = text;
  c.len = len;
  c.pos = 0;

  /* A leading colon is the implementation-defined form, which on every Unix
   * means "a file path" rather than a rule. It is the caller's to resolve,
   * not this parser's. */
  if (len > 0 && text[0] == ':') {
    return GCHRON_ERR_FORMAT;
  }

  result = scan_abbrev(&c, out->std_abbrev);
  if (result != GCHRON_OK) {
    return result;
  }
  result = scan_hms(&c, 24, &written);
  if (result != GCHRON_OK) {
    return result;
  }
  if (written < -MAX_TZ_OFFSET || written > MAX_TZ_OFFSET) {
    return GCHRON_ERR_FORMAT;
  }
  /*
   * The sign flips exactly here, once, at the boundary. `EST5` means "add
   * five hours to local to get UTC", which is UTC-5; every offset elsewhere
   * in this library is seconds *ahead* of UTC, and nothing downstream should
   * have to remember which convention it is holding.
   */
  out->std_offset = -written;

  if (at_end(&c)) {
    return GCHRON_OK;
  }

  result = scan_abbrev(&c, out->dst_abbrev);
  if (result != GCHRON_OK) {
    return result;
  }
  out->has_dst = true;

  if (!at_end(&c) && peek(&c) != ',') {
    result = scan_hms(&c, 24, &written);
    if (result != GCHRON_OK) {
      return result;
    }
    if (written < -MAX_TZ_OFFSET || written > MAX_TZ_OFFSET) {
      return GCHRON_ERR_FORMAT;
    }
    out->dst_offset = -written;
  }
  else {
    /* POSIX: one hour ahead of standard time when the string does not say. */
    out->dst_offset = out->std_offset + DEFAULT_DST_SHIFT;
  }

  if (at_end(&c)) {
    /*
     * A daylight-saving name with no rule. POSIX leaves the transition dates
     * implementation-defined here, and the implementations disagree; glibc
     * falls back to a United States rule, which is a guess about geography
     * that this library has no business making. Refused, so the caller hears
     * about it.
     */
    return GCHRON_ERR_FORMAT;
  }
  if (peek(&c) != ',') {
    return GCHRON_ERR_FORMAT;
  }
  c.pos += 1;
  result = scan_rule_date(&c, &out->start);
  if (result != GCHRON_OK) {
    return result;
  }
  if (peek(&c) != ',') {
    return GCHRON_ERR_FORMAT;
  }
  c.pos += 1;
  result = scan_rule_date(&c, &out->end);
  if (result != GCHRON_OK) {
    return result;
  }
  if (!at_end(&c)) {
    return GCHRON_ERR_FORMAT;
  }
  out->has_rule = true;
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * Evaluating a rule
 *--------------------------------------------------------------------------*/

/**
 * The epoch day one end of a rule falls on in a given year.
 *
 * @return `true` on success; `false` when the year is outside what the
 *   calendar can label.
 */
static bool rule_epoch_day(const GCHRON_TzRuleDate * rule, int32_t year,
    int64_t * out) {
  GCHRON_Date date;

  switch (rule->kind) {
    case GCHRON_TZRULE_JULIAN: {
      /*
       * `Jn` never counts 29 February, so in a leap year every day from 1
       * March onwards is one further along the ordinal count than its J
       * number. Day 60 is 1 March in a common year and would be 29 February
       * in an ordinal count, which is exactly the day J skips.
       */
      bool leap = false;
      int ordinal;
      if (gchron_year_is_leap(year, &leap) != GCHRON_OK) {
        return false;
      }
      ordinal = rule->n + ((leap && rule->n >= 60) ? 1 : 0);
      if (gchron_date_from_ordinal(year, ordinal, &date) != GCHRON_OK) {
        return false;
      }
      break;
    }

    case GCHRON_TZRULE_ZERO:
      /* Bare `n` is zero-based and does count 29 February. */
      if (gchron_date_from_ordinal(year, rule->n + 1, &date) != GCHRON_OK) {
        return false;
      }
      break;

    case GCHRON_TZRULE_MONTH: {
      /*
       * POSIX numbers the weekday 0 = Sunday; this library numbers it
       * ISO 8601's way, 1 = Monday .. 7 = Sunday, everywhere (mistake M7).
       * The conversion happens here and nowhere else.
       *
       * Week 5 means "the last such weekday in the month", which may be the
       * fourth or the fifth - which is exactly what an nth of -1 asks for.
       */
      int weekday = (rule->day == 0) ? GCHRON_SUNDAY : rule->day;
      int nth = (rule->week == 5) ? -1 : rule->week;
      if (gchron_date_nth_weekday(year, rule->month, weekday, nth, &date)
          != GCHRON_OK) {
        return false;
      }
      break;
    }

    case GCHRON_TZRULE_NONE:
    default:
      return false;
  }

  return gchron_date_to_epoch_day(&date, out) == GCHRON_OK;
}

/**
 * When one end of a rule happens, in Unix seconds.
 *
 * @param offset_sec The offset in force **just before** the change, which is
 *   what POSIX says the transition time is expressed in: standard time at the
 *   start of daylight saving, daylight-saving time at the end.
 */
static bool rule_instant(const GCHRON_TzRuleDate * rule, int32_t year,
    int32_t offset_sec, int64_t * out) {
  int64_t day;
  int64_t local;

  if (!rule_epoch_day(rule, year, &day)) {
    return false;
  }
  if (!gchron_mul_i64(day, GCHRON_SECONDS_PER_DAY, &local)) {
    return false;
  }
  /*
   * The named time is added after the day is in seconds, not folded into the
   * day, because POSIX.1-2024 lets it reach a week either side of midnight -
   * so the transition genuinely can land in a different day from the one the
   * rule names.
   */
  if (!gchron_add_i64(local, rule->time, &local)) {
    return false;
  }
  return gchron_sub_i64(local, offset_sec, out);
}

/** Fill in a GCHRON_ZoneInfo from one side of a rule. */
static void info_from_rule(const GCHRON_PosixTz * rule, bool dst,
    GCHRON_ZoneInfo * out) {
  out->offset_sec = dst ? rule->dst_offset : rule->std_offset;
  out->is_dst = dst;
  out->abbreviation = dst ? rule->dst_abbrev : rule->std_abbrev;
}

/**
 * The local year an instant falls in, under a rule's standard offset.
 *
 * Approximate on purpose: it is only used to pick which years' transitions to
 * compute, and the caller checks the neighbours either side.
 */
static bool approximate_year(const GCHRON_PosixTz * rule, int64_t instant,
    int32_t * out) {
  int64_t local;
  int64_t day;
  GCHRON_Date date;

  if (!gchron_add_i64(instant, rule->std_offset, &local)) {
    return false;
  }
  day = gchron_floor_div(local, GCHRON_SECONDS_PER_DAY);
  if (gchron_date_from_epoch_day(day, &date) != GCHRON_OK) {
    return false;
  }
  *out = date.year;
  return true;
}

/**
 * Both of a year's transitions, in Unix seconds.
 *
 * @param out_start Receives when daylight saving begins.
 * @param out_end Receives when it ends.
 * @return `true` when both could be computed.
 */
static bool year_transitions(const GCHRON_PosixTz * rule, int32_t year,
    int64_t * out_start, int64_t * out_end) {
  return rule_instant(&rule->start, year, rule->std_offset, out_start)
      && rule_instant(&rule->end, year, rule->dst_offset, out_end);
}

/** Whether an instant is inside a year's daylight-saving span. */
static bool in_dst_span(int64_t instant, int64_t start, int64_t end) {
  if (start <= end) {
    return instant >= start && instant < end;
  }
  /*
   * Southern hemisphere: daylight saving begins in one calendar year and ends
   * in the next, so the span wraps rather than nests. Australia/Sydney's
   * `M10.1.0,M4.1.0` is this shape, and a reader that assumed start < end
   * gets every Australian summer inverted.
   */
  return instant >= start || instant < end;
}

GCHRON_Result gchron_posixtz_offset_at(const GCHRON_PosixTz * rule,
    int64_t instant, GCHRON_ZoneInfo * out) {
  int32_t year;
  int64_t start;
  int64_t end;

  if (rule == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!rule->has_dst || !rule->has_rule) {
    info_from_rule(rule, false, out);
    return GCHRON_OK;
  }
  if (!approximate_year(rule, instant, &year)) {
    return GCHRON_ERR_RANGE;
  }
  if (!year_transitions(rule, year, &start, &end)) {
    return GCHRON_ERR_RANGE;
  }
  info_from_rule(rule, in_dst_span(instant, start, end), out);
  return GCHRON_OK;
}

/**
 * Collect a year's two transitions in chronological order.
 *
 * A rule may name two changeovers that land on the **same instant**, and then
 * the daylight-saving span is empty and nothing ever changes.
 * `BST5CDT,M1.1.0/0,M1.1.0/1` is such a rule - daylight saving would begin at
 * midnight on the first Sunday of January and end an hour later by the clock
 * that is by then an hour fast, which is the same moment - and
 * `tests/fuzz/fuzz_posix_tz.cpp` found it.
 *
 * It matters because `in_dst_span()` already answers "no" for an empty span,
 * so a transition search that reported the pair anyway would contradict the
 * offset lookup sitting beside it: one would say the zone changes to daylight
 * saving at that instant and the other would say it never does. Reporting no
 * transition is the answer that agrees with both.
 *
 * @param out_at Receives the two instants, earliest first.
 * @param out_to_dst Receives, for each, whether it turns daylight saving on.
 * @return `true` when both could be computed **and** they differ.
 */
static bool ordered_transitions(const GCHRON_PosixTz * rule, int32_t year,
    int64_t out_at[2], bool out_to_dst[2]) {
  int64_t start;
  int64_t end;

  if (!year_transitions(rule, year, &start, &end)) {
    return false;
  }
  if (start == end) {
    return false;
  }
  if (start < end) {
    out_at[0] = start;
    out_to_dst[0] = true;
    out_at[1] = end;
    out_to_dst[1] = false;
  }
  else {
    out_at[0] = end;
    out_to_dst[0] = false;
    out_at[1] = start;
    out_to_dst[1] = true;
  }
  return true;
}

/**
 * Whether a candidate instant really is a transition, and what is in force
 * either side of it.
 *
 * **The two sides are read back out of gchron_posixtz_offset_at() rather than
 * deduced from which end of the rule produced the candidate.** Deducing them
 * looks equivalent and is not: a rule may name both changeovers in the same
 * week, so which of them comes first flips from year to year, and the span
 * then wraps a year boundary that the offset lookup and the transition search
 * were computing on opposite sides of. `BSTST5CDT1,M1.1.0/0,M1.1.1` is such a
 * rule - daylight saving from the first Sunday of January to the first
 * *Monday* of January - and `tests/fuzz/fuzz_posix_tz.cpp` found it by
 * asserting exactly the invariant this restores: the offsets a transition
 * reports are the offsets the lookup gives a second either side of it.
 *
 * Asking the lookup makes the two agree by construction, which is worth more
 * than the two subtractions it costs.
 *
 * @return `true` when something actually changes at @p at.
 */
static bool describe(const GCHRON_PosixTz * rule, int64_t at,
    GCHRON_ZoneInfo * out_before, GCHRON_ZoneInfo * out_after) {
  GCHRON_ZoneInfo before;
  GCHRON_ZoneInfo after;
  int64_t previous;

  if (!gchron_sub_i64(at, 1, &previous)) {
    return false;
  }
  if (gchron_posixtz_offset_at(rule, previous, &before) != GCHRON_OK
      || gchron_posixtz_offset_at(rule, at, &after) != GCHRON_OK) {
    return false;
  }
  if (before.offset_sec == after.offset_sec
      && before.is_dst == after.is_dst) {
    /* A candidate the rule produced that changes nothing. A transition that
     * changes nothing is not a transition, and reporting it would have a
     * caller's countdown fire for no reason. */
    return false;
  }
  if (out_before != NULL) {
    *out_before = before;
  }
  if (out_after != NULL) {
    *out_after = after;
  }
  return true;
}

GCHRON_Result gchron_posixtz_next_transition(const GCHRON_PosixTz * rule,
    int64_t after, int64_t * out_at, GCHRON_ZoneInfo * out_before,
    GCHRON_ZoneInfo * out_after) {
  int32_t year;
  int offset;

  if (rule == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!rule->has_dst || !rule->has_rule) {
    /* The rule never changes: this zone is one offset for ever. */
    return GCHRON_ERR_UNSUPPORTED;
  }
  if (!approximate_year(rule, after, &year)) {
    return GCHRON_ERR_RANGE;
  }

  /*
   * A transition time may be a week either side of the day its rule names, so
   * the answer for an instant in the last days of a year can belong to the
   * year before or after. Three years is more than that slack needs.
   */
  for (offset = -1; offset <= 1; ++offset) {
    int64_t at[2];
    bool to_dst[2];
    int i;

    if (year > GCHRON_YEAR_MAX - 1 || year < GCHRON_YEAR_MIN + 1) {
      return GCHRON_ERR_RANGE;
    }
    if (!ordered_transitions(rule, year + offset, at, to_dst)) {
      /* Either the year cannot be labelled, or the rule's two changeovers
       * coincide and it has no daylight-saving span at all. Both mean there
       * is no transition here to report. */
      continue;
    }
    for (i = 0; i < 2; ++i) {
      if (at[i] > after && describe(rule, at[i], out_before, out_after)) {
        if (out_at != NULL) {
          *out_at = at[i];
        }
        return GCHRON_OK;
      }
    }
    (void)to_dst;
  }
  return GCHRON_ERR_UNSUPPORTED;
}

GCHRON_Result gchron_posixtz_prev_transition(const GCHRON_PosixTz * rule,
    int64_t before, int64_t * out_at, GCHRON_ZoneInfo * out_before,
    GCHRON_ZoneInfo * out_after) {
  int32_t year;
  int offset;

  if (rule == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!rule->has_dst || !rule->has_rule) {
    return GCHRON_ERR_UNSUPPORTED;
  }
  if (!approximate_year(rule, before, &year)) {
    return GCHRON_ERR_RANGE;
  }

  for (offset = 1; offset >= -1; --offset) {
    int64_t at[2];
    bool to_dst[2];
    int i;

    if (year > GCHRON_YEAR_MAX - 1 || year < GCHRON_YEAR_MIN + 1) {
      return GCHRON_ERR_RANGE;
    }
    if (!ordered_transitions(rule, year + offset, at, to_dst)) {
      continue;
    }
    for (i = 1; i >= 0; --i) {
      if (at[i] <= before && describe(rule, at[i], out_before, out_after)) {
        if (out_at != NULL) {
          *out_at = at[i];
        }
        return GCHRON_OK;
      }
    }
    (void)to_dst;
  }
  return GCHRON_ERR_UNSUPPORTED;
}

GCHRON_Result gchron_zone_build_posix(const char * text, size_t len,
    const char * id, const GCHRON_Allocator * allocator, GCHRON_Zone ** out) {
  GCHRON_PosixTz rule;
  GCHRON_Zone * zone;
  GCHRON_Result result;

  if (text == NULL || out == NULL || allocator == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_posixtz_parse(text, len, &rule);
  if (result != GCHRON_OK) {
    return result;
  }

  if (!rule.has_dst) {
    /* A string with no daylight-saving half is a fixed offset, and saying so
     * lets every lookup take the fixed path. */
    return gchron_zone_build_fixed(rule.std_offset, rule.std_abbrev, id,
        allocator, out);
  }

  zone = (GCHRON_Zone *)gcu_allocator_calloc(allocator, 1,
      sizeof(GCHRON_Zone));
  if (zone == NULL) {
    return GCHRON_ERR_OOM;
  }
  zone->allocator = allocator;
  zone->types = (GCHRON_ZoneType *)gcu_allocator_calloc(allocator, 2,
      sizeof(GCHRON_ZoneType));
  if (zone->types == NULL) {
    gchron_zone_free(zone);
    return GCHRON_ERR_OOM;
  }
  zone->type_count = 2;
  zone->types[0].utoff = rule.std_offset;
  zone->types[0].is_dst = false;
  strcpy(zone->types[0].abbrev, rule.std_abbrev);
  zone->types[1].utoff = rule.dst_offset;
  zone->types[1].is_dst = true;
  strcpy(zone->types[1].abbrev, rule.dst_abbrev);
  zone->first_type = 0;
  zone->has_rule = true;
  zone->rule = rule;
  zone->is_fixed = false;

  if (id != NULL) {
    size_t length = strlen(id) + 1;
    zone->id = (char *)gcu_allocator_malloc(allocator, length);
    if (zone->id == NULL) {
      gchron_zone_free(zone);
      return GCHRON_ERR_OOM;
    }
    memcpy(zone->id, id, length);
    zone->canonical_id = zone->id;
  }

  *out = zone;
  return GCHRON_OK;
}
