/**
 * @file
 *
 * Writing RFC 3339 section 5.6 and TOML v1.0.0 text.
 *
 * These are direct writers, not a compiled pattern. format.h's LDML compiler
 * is a separate, tier-3 route to the same text and needs a names provider;
 * producing an RFC 3339 timestamp needs neither, and a consumer whose only
 * use of this library is `text`'s should not link one.
 *
 * The output contract is stated once in parse.h and holds for every function
 * here: the buffer receives a NUL-terminated string, `out_len` is its length
 * without the NUL, and a buffer too small is GCHRON_ERR_LIMIT with `out_len`
 * still set - so a caller can ask for the length by passing a zero-length
 * buffer.
 *
 * Reference: RFC 3339 (2002) sections 4.3 and 5.6; TOML v1.0.0.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/offset.h>
#include <ghoti.io/chron/parse.h>
#include <stdio.h>
#include <string.h>

#include "../core/core_internal.h"

void gchron_write_options_default(GCHRON_WriteOptions * out) {
  if (out == NULL) {
    return;
  }
  /*
   * Zeroed rather than assigned field by field, so that a field added later
   * is the default rather than whatever was on the caller's stack. See
   * gchron_parse_options_default(), where assigning them one at a time went
   * wrong exactly that way. GCHRON_FRACTION_DIGITS_AUTO is 0, which is what
   * makes the zeroed struct the documented default and not merely an empty
   * one.
   */
  memset(out, 0, sizeof(*out));
}

/** Resolve the options a writer was called with. */
static const GCHRON_WriteOptions * effective(const GCHRON_WriteOptions * opts,
    GCHRON_WriteOptions * fallback) {
  if (opts != NULL) {
    return opts;
  }
  gchron_write_options_default(fallback);
  return fallback;
}

/** Write a fixed-width zero-padded number. */
static void put_padded(char * out, size_t * at, int value, int width) {
  int index;

  for (index = width - 1; index >= 0; --index) {
    out[*at + (size_t)index] = (char)('0' + value % 10);
    value /= 10;
  }
  *at += (size_t)width;
}

/** Write `date-fullyear "-" date-month "-" date-mday`. */
static GCHRON_Result put_full_date(char * out, size_t * at,
    const GCHRON_Date * date) {
  if (date->year < 0 || date->year > 9999) {
    /*
     * RFC 3339 has no expanded year and no sign: `date-fullyear = 4DIGIT`.
     * GCHRON_ERR_RANGE rather than a truncated or signed year, because a
     * reader of `0020-01-01` cannot tell it was meant to be 20020.
     */
    return GCHRON_ERR_RANGE;
  }
  put_padded(out, at, date->year, 4);
  out[(*at)++] = '-';
  put_padded(out, at, date->month, 2);
  out[(*at)++] = '-';
  put_padded(out, at, date->day, 2);
  return GCHRON_OK;
}

/**
 * Write `time-secfrac`, or nothing.
 *
 * GCHRON_FRACTION_DIGITS_AUTO writes the shortest of 0, 3, 6 and 9 digits
 * that loses nothing - so a whole second writes no fraction and a
 * millisecond value writes three - which is what makes
 * `parse(write(x)) == x` hold for every value this can represent.
 */
static void put_secfrac(char * out, size_t * at, int32_t nsec, int digits) {
  int index;
  int32_t scale;

  if (digits == GCHRON_FRACTION_DIGITS_NONE) {
    return;
  }
  if (digits == GCHRON_FRACTION_DIGITS_AUTO) {
    if (nsec == 0) {
      return;
    }
    digits = (nsec % 1000000 == 0) ? 3 : ((nsec % 1000 == 0) ? 6 : 9);
  }
  if (digits < 1) {
    return;
  }
  if (digits > 9) {
    digits = 9;
  }

  out[(*at)++] = '.';
  scale = 100000000;
  for (index = 0; index < digits; ++index) {
    out[(*at)++] = (char)('0' + (nsec / scale) % 10);
    scale /= 10;
  }
}

/** Write `partial-time`. */
static void put_partial_time(char * out, size_t * at, const GCHRON_Time * time,
    int fraction_digits) {
  put_padded(out, at, time->hour, 2);
  out[(*at)++] = ':';
  put_padded(out, at, time->minute, 2);
  out[(*at)++] = ':';
  put_padded(out, at, time->second, 2);
  put_secfrac(out, at, time->nsec, fraction_digits);
}

/** Write `time-offset`. */
static void put_offset(char * out, size_t * at, int32_t offset_sec,
    bool offset_unknown, const GCHRON_WriteOptions * opts) {
  int32_t magnitude;

  if (offset_unknown) {
    /*
     * The one spelling that carries the meaning. Written whatever the options
     * say, because `Z` and `+00:00` say the offset is known to be zero and
     * this value says it is not known at all (RFC 3339 section 4.3).
     */
    memcpy(out + *at, "-00:00", 6);
    *at += 6;
    return;
  }
  if (offset_sec == 0 && !opts->zero_offset_as_numeric) {
    out[(*at)++] = opts->lowercase ? 'z' : 'Z';
    return;
  }
  out[(*at)++] = (offset_sec < 0) ? '-' : '+';
  magnitude = (offset_sec < 0) ? -offset_sec : offset_sec;
  put_padded(out, at, (int)(magnitude / 3600), 2);
  out[(*at)++] = ':';
  put_padded(out, at, (int)((magnitude / 60) % 60), 2);
}

/** Copy a finished string out, or report the length it needed. */
static GCHRON_Result deliver(const char * scratch, size_t length, char * buf,
    size_t buf_len, size_t * out_len) {
  if (out_len != NULL) {
    *out_len = length;
  }
  if (buf_len < length + 1) {
    return GCHRON_ERR_LIMIT;
  }
  memcpy(buf, scratch, length);
  buf[length] = '\0';
  return GCHRON_OK;
}

GCHRON_Result gchron_write_rfc3339_full_date(const GCHRON_Date * date,
    char * buf, size_t buf_len, size_t * out_len) {
  char scratch[GCHRON_RFC3339_DATE_MAX];
  size_t at = 0;
  GCHRON_Result result;

  if ((buf == NULL && buf_len != 0) || !gchron_date_is_valid(date)) {
    return GCHRON_ERR_INVALID;
  }
  result = put_full_date(scratch, &at, date);
  if (result != GCHRON_OK) {
    return result;
  }
  return deliver(scratch, at, buf, buf_len, out_len);
}

GCHRON_Result gchron_write_rfc3339_full_time(const GCHRON_OffsetTime * ot,
    const GCHRON_WriteOptions * opts, char * buf, size_t buf_len,
    size_t * out_len) {
  char scratch[GCHRON_RFC3339_TIME_MAX];
  GCHRON_WriteOptions fallback;
  size_t at = 0;

  if ((buf == NULL && buf_len != 0) || !gchron_offset_time_is_valid(ot)) {
    return GCHRON_ERR_INVALID;
  }
  opts = effective(opts, &fallback);
  put_partial_time(scratch, &at, &ot->time, opts->fraction_digits);
  put_offset(scratch, &at, ot->offset_sec, ot->offset_unknown, opts);
  return deliver(scratch, at, buf, buf_len, out_len);
}

GCHRON_Result gchron_write_rfc3339_date_time(const GCHRON_OffsetDateTime * odt,
    const GCHRON_WriteOptions * opts, char * buf, size_t buf_len,
    size_t * out_len) {
  char scratch[GCHRON_RFC3339_DATE_TIME_MAX];
  GCHRON_WriteOptions fallback;
  size_t at = 0;
  GCHRON_Result result;

  if ((buf == NULL && buf_len != 0) || !gchron_offset_is_valid(odt)) {
    return GCHRON_ERR_INVALID;
  }
  opts = effective(opts, &fallback);
  result = put_full_date(scratch, &at, &odt->civil.date);
  if (result != GCHRON_OK) {
    return result;
  }
  scratch[at++] = opts->space_separator ? ' ' : (opts->lowercase ? 't' : 'T');
  put_partial_time(scratch, &at, &odt->civil.time, opts->fraction_digits);
  put_offset(scratch, &at, odt->offset_sec, odt->offset_unknown, opts);
  return deliver(scratch, at, buf, buf_len, out_len);
}

GCHRON_Result gchron_write_toml(const GCHRON_TomlValue * value,
    const GCHRON_WriteOptions * opts, char * buf, size_t buf_len,
    size_t * out_len) {
  char scratch[GCHRON_RFC3339_DATE_TIME_MAX];
  GCHRON_WriteOptions fallback;
  size_t at = 0;
  GCHRON_Result result;

  if (buf == NULL && buf_len != 0) {
    return GCHRON_ERR_INVALID;
  }
  if (value == NULL) {
    return GCHRON_ERR_INVALID;
  }
  opts = effective(opts, &fallback);

  switch (value->kind) {
    case GCHRON_TOML_LOCAL_TIME:
      if (!gchron_time_is_valid(&value->civil.time)) {
        return GCHRON_ERR_INVALID;
      }
      put_partial_time(scratch, &at, &value->civil.time,
          opts->fraction_digits);
      break;

    case GCHRON_TOML_LOCAL_DATE:
      if (!gchron_date_is_valid(&value->civil.date)) {
        return GCHRON_ERR_INVALID;
      }
      result = put_full_date(scratch, &at, &value->civil.date);
      if (result != GCHRON_OK) {
        return result;
      }
      break;

    case GCHRON_TOML_LOCAL_DATE_TIME:
    case GCHRON_TOML_OFFSET_DATE_TIME:
      if (!gchron_datetime_is_valid(&value->civil)) {
        return GCHRON_ERR_INVALID;
      }
      result = put_full_date(scratch, &at, &value->civil.date);
      if (result != GCHRON_OK) {
        return result;
      }
      scratch[at++] =
          opts->space_separator ? ' ' : (opts->lowercase ? 't' : 'T');
      put_partial_time(scratch, &at, &value->civil.time,
          opts->fraction_digits);
      if (value->kind == GCHRON_TOML_OFFSET_DATE_TIME) {
        if (value->offset_sec <= -GCHRON_OFFSET_LIMIT_SECONDS
            || value->offset_sec >= GCHRON_OFFSET_LIMIT_SECONDS) {
          return GCHRON_ERR_INVALID;
        }
        put_offset(scratch, &at, value->offset_sec, value->offset_unknown,
            opts);
      }
      break;

    case GCHRON_TOML_NONE:
    default:
      return GCHRON_ERR_INVALID;
  }

  return deliver(scratch, at, buf, buf_len, out_len);
}
