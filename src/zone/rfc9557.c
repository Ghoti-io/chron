/**
 * @file
 *
 * RFC 9557: *Date and Time on the Internet: Timestamps with Additional
 * Information* - the format everybody calls IXDTF.
 *
 * This is the grammar that fixes mistake M13. RFC 3339 carries an offset and
 * not a zone, so `2026-03-08T01:30-05:00` cannot say it meant New York, and
 * an application that stores that string has lost the only thing that lets it
 * compute what the same appointment means next summer. The `[America/New_York]`
 * annotation is what carries the name, and this is the only text format in
 * the library that round-trips a GCHRON_ZonedDateTime whole.
 *
 * It lives beside the zoned type rather than in `src/parse/` with the other
 * grammars, and is declared in `zoned.h` rather than in `parse.h`, because
 * resolving a name needs a zone database. design.md section 13 marks
 * `parse.h` "tier 1 (+2 for RFC 9557 zones)"; putting the one tier-2 grammar
 * in the tier-2 header instead keeps `parse.h` free of zone declarations
 * altogether, which is the property the tier rule exists to protect - a
 * consumer that only parses timestamps never sees a zone type.
 *
 * Reference: RFC 9557 (2024), sections 4.1 and 4.2.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/offset.h>
#include <ghoti.io/chron/parse.h>
#include <ghoti.io/chron/zone.h>
#include <ghoti.io/chron/zoned.h>
#include <stdio.h>
#include <string.h>

#include "../core/core_internal.h"
#include "zone_internal.h"

/** The longest zone name an annotation may carry. */
#define ANNOTATION_MAX 128

/** Whether a byte may begin an RFC 9557 `time-zone-part`. */
static bool is_zone_initial(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.'
      || c == '_';
}

/** Whether a byte may continue one. */
static bool is_zone_char(char c) {
  return is_zone_initial(c) || (c >= '0' && c <= '9') || c == '-' || c == '+'
      || c == '/';
}

/** Whether a byte may appear in a suffix key. */
static bool is_key_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'
      || c == '_';
}

/** Whether a byte may appear in a suffix value. */
static bool is_value_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
      || (c >= '0' && c <= '9') || c == '-';
}

/** One `[...]` annotation, as read out of the text. */
typedef struct Annotation {
  bool critical;     /**< A `!` followed the opening bracket. */
  const char * key;  /**< NULL for the zone annotation, which has no key. */
  size_t key_length;
  const char * value;
  size_t value_length;
  size_t at;         /**< Where the annotation began, for the error. */
} Annotation;

/**
 * Read one `[...]` annotation.
 *
 * @return GCHRON_OK, GCHRON_ERR_FORMAT, or GCHRON_ERR_UNSUPPORTED when the
 *   text is exhausted (which is how the caller learns there are no more).
 */
static GCHRON_Result scan_annotation(const char * text, size_t len,
    size_t * pos, Annotation * out, GCHRON_Error * err) {
  size_t i = *pos;
  size_t start;

  if (i >= len || text[i] != '[') {
    return GCHRON_ERR_UNSUPPORTED;
  }
  out->at = i;
  i += 1;
  out->critical = false;
  out->key = NULL;
  out->key_length = 0;

  if (i < len && text[i] == '!') {
    out->critical = true;
    i += 1;
  }

  start = i;
  while (i < len && text[i] != ']' && text[i] != '=') {
    i += 1;
  }
  if (i >= len) {
    return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_UNEXPECTED_END, i,
        0);
  }

  if (text[i] == '=') {
    size_t key_start = start;
    size_t key_end = i;
    size_t j;

    /* `suffix-key = lcalpha *(lcalpha / DIGIT / "-" / "_")`. */
    if (key_end == key_start || text[key_start] < 'a'
        || text[key_start] > 'z') {
      return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_ANNOTATION_KEY,
          key_start, key_end - key_start);
    }
    for (j = key_start; j < key_end; ++j) {
      if (!is_key_char(text[j])) {
        return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_ANNOTATION_KEY,
            j, 1);
      }
    }
    out->key = text + key_start;
    out->key_length = key_end - key_start;

    i += 1;
    start = i;
    while (i < len && text[i] != ']') {
      if (!is_value_char(text[i])) {
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_ANNOTATION_VALUE, i, 1);
      }
      i += 1;
    }
    if (i >= len) {
      return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_UNEXPECTED_END,
          i, 0);
    }
  }

  if (i == start) {
    return gchron_fail(err, GCHRON_ERR_FORMAT, GCHRON_DIAG_ANNOTATION_VALUE,
        start, 0);
  }
  out->value = text + start;
  out->value_length = i - start;
  *pos = i + 1; /* past the ']' */
  return GCHRON_OK;
}

/** Whether an annotation's value is a well-formed zone name. */
static bool is_zone_name(const char * value, size_t length) {
  size_t i;

  if (length == 0 || length > ANNOTATION_MAX) {
    return false;
  }
  if (!is_zone_initial(value[0])) {
    return false;
  }
  for (i = 0; i < length; ++i) {
    if (!is_zone_char(value[i])) {
      return false;
    }
  }
  return true;
}

/** Whether an annotation's value is an offset rather than a name. */
static bool is_offset_annotation(const char * value, size_t length) {
  return length > 0 && (value[0] == '+' || value[0] == '-' || value[0] == 'Z'
      || value[0] == 'z');
}

GCHRON_Result gchron_parse_rfc9557(const char * text, size_t len,
    GCHRON_ZoneDb * db, const GCHRON_ParseOptions * opts,
    GCHRON_ZonedDateTime * out, GCHRON_ParseInfo * info, GCHRON_Error * err) {
  GCHRON_ParseOptions fallback;
  GCHRON_ParseOptions inner;
  GCHRON_OffsetDateTime base;
  GCHRON_ParseInfo base_info;
  GCHRON_Result result;
  size_t pos = 0;
  Annotation annotation;
  bool seen_zone = false;
  char zone_name[ANNOTATION_MAX + 1];
  const GCHRON_Zone * zone = NULL;
  GCHRON_Instant from_offset;
  GCHRON_ZoneConflict conflict;
  char calendar[GCHRON_CALENDAR_ID_MAX + 1];
  bool have_calendar = false;

  gchron_error_clear(err);
  gchron_parse_info_clear(info);
  if (text == NULL || out == NULL) {
    return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, 0);
  }
  opts = (opts != NULL) ? opts
                        : (gchron_parse_options_default(&fallback), &fallback);
  conflict = opts->zone_conflict;

  /*
   * The RFC 3339 half first, with trailing text permitted so that the
   * annotations are left for this function rather than rejected by the
   * grammar underneath it. Everything else the caller asked for - the leap
   * policy, the fraction policy, the limit - is passed through unchanged.
   */
  inner = *opts;
  inner.allow_trailing = true;
  result = gchron_parse_rfc3339_date_time(text, len, &inner, &base,
      &base_info, err);
  if (result != GCHRON_OK) {
    return result;
  }
  pos = base_info.consumed;

  while (pos < len) {
    result = scan_annotation(text, len, &pos, &annotation, err);
    if (result == GCHRON_ERR_UNSUPPORTED) {
      break; /* not an annotation; the trailing-text check below handles it */
    }
    if (result != GCHRON_OK) {
      return result;
    }

    if (annotation.key == NULL) {
      /*
       * The zone annotation. RFC 9557 section 4.1: at most one, and it comes
       * first. A second is a malformed timestamp rather than an override.
       */
      if (seen_zone) {
        return gchron_fail(err, GCHRON_ERR_FORMAT,
            GCHRON_DIAG_ANNOTATION_REPEATED, annotation.at, 1);
      }
      seen_zone = true;

      if (is_offset_annotation(annotation.value, annotation.value_length)) {
        /* `[-05:00]` or `[Z]`: a fixed-offset zone rather than a name. */
        GCHRON_OffsetTime probe;
        char buffer[16];
        if (annotation.value_length + 1 > sizeof(buffer)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_ANNOTATION_VALUE, annotation.at,
              annotation.value_length);
        }
        memcpy(buffer, annotation.value, annotation.value_length);
        buffer[annotation.value_length] = '\0';
        {
          /* Reuse the RFC 3339 offset production rather than a second copy
           * of it: two parsers for one grammar is two places to be wrong. */
          char probe_text[32];
          snprintf(probe_text, sizeof(probe_text), "00:00:00%s", buffer);
          if (gchron_parse_rfc3339_full_time(probe_text,
                  strlen(probe_text), NULL, &probe, NULL, NULL)
              != GCHRON_OK) {
            return gchron_fail(err, GCHRON_ERR_FORMAT,
                GCHRON_DIAG_ANNOTATION_VALUE, annotation.at,
                annotation.value_length);
          }
        }
        if (db == NULL) {
          return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE,
              annotation.at, annotation.value_length);
        }
        result = gchron_zonedb_fixed(db, probe.offset_sec, &zone);
        if (result != GCHRON_OK) {
          return gchron_fail(err, result, GCHRON_DIAG_NONE, annotation.at,
              annotation.value_length);
        }
      }
      else {
        if (!is_zone_name(annotation.value, annotation.value_length)) {
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_ANNOTATION_VALUE, annotation.at,
              annotation.value_length);
        }
        if (db == NULL) {
          /* There would be nothing to resolve the name against, and
           * inventing a fixed-offset zone from the timestamp's own offset
           * would throw away the very thing the annotation exists to
           * carry. */
          return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE,
              annotation.at, annotation.value_length);
        }
        memcpy(zone_name, annotation.value, annotation.value_length);
        zone_name[annotation.value_length] = '\0';
        result = gchron_zonedb_zone(db, zone_name, &zone);
        if (result != GCHRON_OK) {
          return gchron_fail(err, result, GCHRON_DIAG_NONE, annotation.at,
              annotation.value_length);
        }
      }
      continue;
    }

    /* A keyed suffix. `u-ca` is the one with a meaning here. */
    if (annotation.key_length == 4
        && memcmp(annotation.key, "u-ca", 4) == 0) {
      if (annotation.value_length > GCHRON_CALENDAR_ID_MAX) {
        /* No registered calendar identifier is anywhere near this long, so a
         * value that does not fit names nothing this library could act on. */
        return gchron_fail(err, GCHRON_ERR_UNSUPPORTED,
            GCHRON_DIAG_ANNOTATION_VALUE, annotation.at,
            annotation.value_length);
      }
      if (info != NULL) {
        /* Copied, not borrowed: the annotation points into the caller's
         * input, and a GCHRON_ParseInfo outlives the call that filled it. */
        memcpy(calendar, annotation.value, annotation.value_length);
        calendar[annotation.value_length] = '\0';
        have_calendar = true;
      }
      continue;
    }

    if (annotation.critical) {
      /*
       * The whole point of the `!` flag: the writer is saying that ignoring
       * this annotation would change what the timestamp means. RFC 9557
       * section 4.1 requires a reader that does not understand it to reject
       * the timestamp rather than proceed.
       */
      return gchron_fail(err, GCHRON_ERR_UNSUPPORTED,
          GCHRON_DIAG_ANNOTATION_CRITICAL, annotation.at,
          annotation.key_length);
    }
    /* An unknown annotation without the flag is ignorable, and is ignored. */
  }

  if (pos != len && !opts->allow_trailing) {
    return gchron_fail(err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_TRAILING_CHARACTERS, pos, len - pos);
  }

  result = gchron_offset_to_instant(&base, &from_offset);
  if (result != GCHRON_OK) {
    return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, len);
  }

  if (zone == NULL) {
    /* No annotation: a plain RFC 3339 timestamp, whose zone is the offset. */
    if (db == NULL) {
      return gchron_fail(err, GCHRON_ERR_INVALID, GCHRON_DIAG_NONE, 0, len);
    }
    result = gchron_zonedb_fixed(db, base.offset_sec, &zone);
    if (result != GCHRON_OK) {
      return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, len);
    }
  }
  else {
    GCHRON_ZoneInfo at_instant;
    result = gchron_zone_offset_at(zone, from_offset, &at_instant);
    if (result != GCHRON_OK) {
      return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, len);
    }
    if (at_instant.offset_sec != base.offset_sec) {
      /*
       * The two halves contradict each other. Something upstream is wrong -
       * a stale zone table, or a clock - and which half to believe is the
       * application's decision, not this parser's (RFC 9557 section 4.1).
       */
      if (info != NULL) {
        info->offset_disagreed_with_zone = true;
      }
      switch (conflict) {
        case GCHRON_ZONECONFLICT_PREFER_OFFSET:
          break;

        case GCHRON_ZONECONFLICT_PREFER_ZONE: {
          GCHRON_ZonedDateTime resolved;
          result = gchron_zoned_from_civil(base.civil, zone,
              GCHRON_RESOLVE_REJECT, &resolved);
          if (result != GCHRON_OK) {
            return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, len);
          }
          from_offset = resolved.instant;
          break;
        }

        case GCHRON_ZONECONFLICT_REJECT:
        default:
          return gchron_fail(err, GCHRON_ERR_FORMAT,
              GCHRON_DIAG_OFFSET_ZONE_CONFLICT, 0, len);
      }
    }
  }

  result = gchron_zoned_from_instant(from_offset, zone, out);
  if (result != GCHRON_OK) {
    return gchron_fail(err, result, GCHRON_DIAG_NONE, 0, len);
  }
  if (info != NULL) {
    info->consumed = pos;
    info->leap_second = base_info.leap_second;
    info->fraction_truncated = base_info.fraction_truncated;
    info->fraction_digits = base_info.fraction_digits;
    info->offset_unknown = base_info.offset_unknown;
    info->had_zone_annotation = seen_zone;
    if (have_calendar) {
      memcpy(info->calendar, calendar, strlen(calendar) + 1);
    }
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_write_rfc9557(const GCHRON_ZonedDateTime * zoned,
    const GCHRON_WriteOptions * opts, char * buf, size_t buf_len,
    size_t * out_len) {
  char scratch[GCHRON_RFC9557_MAX];
  GCHRON_OffsetDateTime offset;
  size_t length = 0;
  const char * id;
  GCHRON_Result result;

  if ((buf == NULL && buf_len != 0) || zoned == NULL || zoned->zone == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_zoned_to_offset(zoned, &offset);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_write_rfc3339_date_time(&offset, opts, scratch,
      sizeof(scratch), &length);
  if (result != GCHRON_OK) {
    return result;
  }

  id = gchron_zone_id(zoned->zone);
  if (id != NULL && !gchron_zone_is_fixed(zoned->zone)) {
    /*
     * An anonymous zone - built from a `TZ` rule, or read from an
     * `/etc/localtime` that is a plain file - has no name to annotate, and a
     * fixed-offset zone's name would say nothing the offset does not. Plain
     * RFC 3339 is the honest output in both cases; inventing a name is not.
     */
    size_t id_length = strlen(id);
    if (length + id_length + 3 > sizeof(scratch)) {
      /* Report the length it would have needed, as every writer here does. */
      if (out_len != NULL) {
        *out_len = length + id_length + 2;
      }
      return GCHRON_ERR_LIMIT;
    }
    scratch[length++] = '[';
    memcpy(scratch + length, id, id_length);
    length += id_length;
    scratch[length++] = ']';
    scratch[length] = '\0';
  }

  if (out_len != NULL) {
    *out_len = length;
  }
  if (buf_len < length + 1) {
    return GCHRON_ERR_LIMIT;
  }
  memcpy(buf, scratch, length + 1);
  return GCHRON_OK;
}
