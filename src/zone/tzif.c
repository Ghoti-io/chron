/**
 * @file
 *
 * The TZif reader.
 *
 * A TZif file is a binary format read from disk, and design.md section 1.2
 * counts it among the three untrusted inputs: a corrupt or hostile one must
 * produce GCHRON_ERR_CORRUPT or GCHRON_ERR_LIMIT, never a crash and never a
 * zone that silently answers wrongly. Every length here is checked against
 * the remaining bytes before it is used, every count against a limit before
 * it is multiplied, and every index against the table it indexes.
 *
 * Reference: RFC 8536, *The Time Zone Information Format (TZif)* (2019);
 * `tzfile(5)` for the version-4 additions.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/allocator.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/offset.h>
#include <ghoti.io/chron/zone.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/cutil/safemath.h>

#include "../core/core_internal.h"
#include "zone_internal.h"

/** Bytes in a TZif header, magic and reserved block included. */
#define TZIF_HEADER_BYTES 44

/** Bytes in one `ttinfo` record: a 4-byte offset, a flag and an index. */
#define TZIF_TTINFO_BYTES 6

/**
 * A cursor over the image.
 *
 * Every read goes through this, and every one of them checks. Reading a
 * binary format by casting a struct over the bytes is what section 8 of
 * CONVENTIONS.md forbids and what this exists instead of.
 */
typedef struct Reader {
  const uint8_t * data;
  size_t len;
  size_t pos;
} Reader;

/** Whether @p count more bytes are there to be read. */
static bool have(const Reader * r, size_t count) {
  return r->len - r->pos >= count;
}

/** Read a big-endian unsigned 32-bit value. */
static bool read_u32(Reader * r, uint32_t * out) {
  if (!have(r, 4)) {
    return false;
  }
  *out = ((uint32_t)r->data[r->pos] << 24)
      | ((uint32_t)r->data[r->pos + 1] << 16)
      | ((uint32_t)r->data[r->pos + 2] << 8)
      | (uint32_t)r->data[r->pos + 3];
  r->pos += 4;
  return true;
}

/** Read a big-endian signed 32-bit value, two's complement. */
static bool read_i32(Reader * r, int32_t * out) {
  uint32_t raw;
  if (!read_u32(r, &raw)) {
    return false;
  }
  /* Through uint32_t and an explicit fold: a cast of a value above INT32_MAX
   * to int32_t is implementation-defined, and this library has no
   * implementation-defined arithmetic in it. */
  *out = (raw & 0x80000000u) ? -(int32_t)(~raw) - 1 : (int32_t)raw;
  return true;
}

/** Read a big-endian signed 64-bit value, two's complement. */
static bool read_i64(Reader * r, int64_t * out) {
  uint64_t raw = 0;
  int i;
  if (!have(r, 8)) {
    return false;
  }
  for (i = 0; i < 8; ++i) {
    raw = (raw << 8) | r->data[r->pos + (size_t)i];
  }
  r->pos += 8;
  *out = (raw & UINT64_C(0x8000000000000000)) ? -(int64_t)(~raw) - 1
                                              : (int64_t)raw;
  return true;
}

/**
 * The length of a NUL-terminated string, bounded.
 *
 * `strnlen` is POSIX and not C17, and this library compiles with
 * `-std=c17 -pedantic-errors`. Six lines of our own beat a feature-test macro
 * that changes what every other header in the translation unit declares.
 */
static size_t bounded_strlen(const char * s, size_t max) {
  size_t n = 0;
  while (n < max && s[n] != '\0') {
    n += 1;
  }
  return n;
}

/** Read one byte. */
static bool read_u8(Reader * r, uint8_t * out) {
  if (!have(r, 1)) {
    return false;
  }
  *out = r->data[r->pos++];
  return true;
}

/** The six counts a TZif header carries. */
typedef struct Header {
  uint8_t version;   /**< 0 for version 1, else the ASCII digit's value. */
  uint32_t isutcnt;
  uint32_t isstdcnt;
  uint32_t leapcnt;
  uint32_t timecnt;
  uint32_t typecnt;
  uint32_t charcnt;
} Header;

/**
 * Read and validate a header.
 *
 * The three constraints RFC 8536 section 3.1 states are checked here rather
 * than at the point each count is used, so that a file which fails them is
 * refused before anything has been sized from it.
 */
static GCHRON_Result read_header(Reader * r, Header * out) {
  uint8_t version_byte;
  size_t i;

  if (!have(r, TZIF_HEADER_BYTES)) {
    return GCHRON_ERR_CORRUPT;
  }
  if (memcmp(r->data + r->pos, "TZif", 4) != 0) {
    return GCHRON_ERR_CORRUPT;
  }
  r->pos += 4;
  if (!read_u8(r, &version_byte)) {
    return GCHRON_ERR_CORRUPT;
  }
  if (version_byte == 0) {
    out->version = 1;
  }
  else if (version_byte >= '2' && version_byte <= '4') {
    out->version = (uint8_t)(version_byte - '0');
  }
  else {
    /* A version this reader does not know. Refusing rather than guessing:
     * a later version may change the meaning of a field this one reads. */
    return GCHRON_ERR_UNSUPPORTED;
  }

  /* Fifteen reserved bytes. RFC 8536 says they MUST be zero; readers are not
   * told to check, and an implementation that did would reject files a later
   * version is entitled to write. Skipped, not validated. */
  for (i = 0; i < 15; ++i) {
    uint8_t ignored;
    if (!read_u8(r, &ignored)) {
      return GCHRON_ERR_CORRUPT;
    }
  }

  if (!read_u32(r, &out->isutcnt) || !read_u32(r, &out->isstdcnt)
      || !read_u32(r, &out->leapcnt) || !read_u32(r, &out->timecnt)
      || !read_u32(r, &out->typecnt) || !read_u32(r, &out->charcnt)) {
    return GCHRON_ERR_CORRUPT;
  }

  /* RFC 8536 section 3.1. */
  if (out->typecnt == 0 || out->charcnt == 0) {
    return GCHRON_ERR_CORRUPT;
  }
  if (out->isutcnt != 0 && out->isutcnt != out->typecnt) {
    return GCHRON_ERR_CORRUPT;
  }
  if (out->isstdcnt != 0 && out->isstdcnt != out->typecnt) {
    return GCHRON_ERR_CORRUPT;
  }
  return GCHRON_OK;
}

/**
 * How many bytes a data block with these counts occupies.
 *
 * Computed with overflow-checked size math, because every one of the six
 * counts is a 32-bit number a hostile file chose, and on a 32-bit `size_t`
 * the product of two of them wraps.
 */
static bool block_bytes(const Header * h, size_t time_size, size_t * out) {
  size_t total = 0;
  size_t part;

  if (!gcu_safe_mul_size(h->timecnt, time_size, &part)
      || !gcu_safe_add_size(total, part, &total)) {
    return false;
  }
  if (!gcu_safe_add_size(total, h->timecnt, &total)) {
    return false;
  }
  if (!gcu_safe_mul_size(h->typecnt, TZIF_TTINFO_BYTES, &part)
      || !gcu_safe_add_size(total, part, &total)) {
    return false;
  }
  if (!gcu_safe_add_size(total, h->charcnt, &total)) {
    return false;
  }
  if (!gcu_safe_mul_size(h->leapcnt, time_size + 4, &part)
      || !gcu_safe_add_size(total, part, &total)) {
    return false;
  }
  if (!gcu_safe_add_size(total, h->isstdcnt, &total)
      || !gcu_safe_add_size(total, h->isutcnt, &total)) {
    return false;
  }
  *out = total;
  return true;
}

/**
 * Refuse a header whose counts describe more data than the file contains.
 *
 * **This has to happen before anything is sized from those counts.** The
 * counts are six 32-bit numbers a hostile file chose, and `read_block()`
 * allocates from `timecnt` and `typecnt` directly - so a seventy-four byte
 * file claiming 987,654,144 transitions asked for eight gigabytes before
 * discovering it had ten bytes left. `tests/fuzz/fuzz_tzif.cpp` found exactly
 * that file; `tests/unit/test_tzif.cpp` keeps it.
 *
 * Checking the total block length is what makes every count bounded at once,
 * and bounded by something no header can inflate: each transition costs nine
 * bytes and each type six, so a file can only claim what it can carry.
 */
static GCHRON_Result check_block_fits(const Reader * r, const Header * h,
    size_t time_size) {
  size_t needed;

  if (!block_bytes(h, time_size, &needed)) {
    return GCHRON_ERR_CORRUPT;
  }
  if (r->len - r->pos < needed) {
    return GCHRON_ERR_CORRUPT;
  }
  return GCHRON_OK;
}

/** Check the counts against the caller's limits. */
static GCHRON_Result check_limits(const Header * h,
    const GCHRON_Limits * limits) {
  if (limits->max_transitions != 0 && h->timecnt > limits->max_transitions) {
    return GCHRON_ERR_LIMIT;
  }
  if (limits->max_zone_types != 0 && h->typecnt > limits->max_zone_types) {
    return GCHRON_ERR_LIMIT;
  }
  return GCHRON_OK;
}

/**
 * Read one data block into a zone.
 *
 * @param r The cursor, positioned at the start of the block.
 * @param h The counts from the header that introduced it.
 * @param time_size 4 for a version-1 block, 8 for a version-2 one.
 * @param zone The zone to fill in; its arrays are allocated here.
 */
static GCHRON_Result read_block(Reader * r, const Header * h, size_t time_size,
    GCHRON_Zone * zone) {
  const GCHRON_Allocator * alloc = zone->allocator;
  size_t i;
  size_t chars_at;
  int64_t previous = 0;
  bool have_previous = false;

  if (h->timecnt > 0) {
    zone->transition_at = (int64_t *)gcu_allocator_calloc(alloc, h->timecnt,
        sizeof(int64_t));
    zone->transition_type = (uint8_t *)gcu_allocator_calloc(alloc, h->timecnt,
        sizeof(uint8_t));
    if (zone->transition_at == NULL || zone->transition_type == NULL) {
      return GCHRON_ERR_OOM;
    }
  }
  zone->transition_count = h->timecnt;

  for (i = 0; i < h->timecnt; ++i) {
    int64_t at;
    if (time_size == 8) {
      if (!read_i64(r, &at)) {
        return GCHRON_ERR_CORRUPT;
      }
    }
    else {
      int32_t narrow;
      if (!read_i32(r, &narrow)) {
        return GCHRON_ERR_CORRUPT;
      }
      at = narrow;
    }
    /* RFC 8536 section 3.2: the times are sorted in strictly ascending order.
     * A file that breaks that would make the binary search below silently
     * return the wrong type rather than fail, which is the "answers wrongly"
     * case the threat model forbids. */
    if (have_previous && at <= previous) {
      return GCHRON_ERR_CORRUPT;
    }
    previous = at;
    have_previous = true;
    zone->transition_at[i] = at;
  }

  for (i = 0; i < h->timecnt; ++i) {
    uint8_t type;
    if (!read_u8(r, &type)) {
      return GCHRON_ERR_CORRUPT;
    }
    if (type >= h->typecnt) {
      return GCHRON_ERR_CORRUPT;
    }
    zone->transition_type[i] = type;
  }

  zone->types = (GCHRON_ZoneType *)gcu_allocator_calloc(alloc, h->typecnt,
      sizeof(GCHRON_ZoneType));
  if (zone->types == NULL) {
    return GCHRON_ERR_OOM;
  }
  zone->type_count = h->typecnt;

  /* The designations follow the ttinfo records, and each record indexes into
   * them, so where they start has to be known before they are read. */
  chars_at = r->pos + (size_t)h->typecnt * TZIF_TTINFO_BYTES;
  if (chars_at > r->len || r->len - chars_at < h->charcnt) {
    return GCHRON_ERR_CORRUPT;
  }

  for (i = 0; i < h->typecnt; ++i) {
    int32_t utoff;
    uint8_t is_dst;
    uint8_t index;
    const char * designation;
    size_t available;
    size_t length;

    if (!read_i32(r, &utoff) || !read_u8(r, &is_dst) || !read_u8(r, &index)) {
      return GCHRON_ERR_CORRUPT;
    }
    /* RFC 8536 section 3.2: utoff SHOULD be in [-89999, 93599] and MUST NOT
     * be -2^31. The wider bound is what this library can hold and what
     * GCHRON_OffsetDateTime promises. */
    if (utoff <= -GCHRON_OFFSET_LIMIT_SECONDS
        || utoff >= GCHRON_OFFSET_LIMIT_SECONDS) {
      return GCHRON_ERR_CORRUPT;
    }
    if (is_dst > 1) {
      return GCHRON_ERR_CORRUPT;
    }
    if (index >= h->charcnt) {
      return GCHRON_ERR_CORRUPT;
    }

    designation = (const char *)r->data + chars_at + index;
    available = h->charcnt - index;
    length = bounded_strlen(designation, available);
    if (length == available) {
      /* Not NUL-terminated inside the block. Reading it as a string would
       * run off the end of the designations and into whatever follows. */
      return GCHRON_ERR_CORRUPT;
    }
    if (length > GCHRON_ABBREV_MAX) {
      return GCHRON_ERR_LIMIT;
    }

    zone->types[i].utoff = utoff;
    zone->types[i].is_dst = (is_dst != 0);
    memcpy(zone->types[i].abbrev, designation, length);
    zone->types[i].abbrev[length] = '\0';
  }

  /* The designations, the leap-second records and the two indicator arrays
   * are all skipped: the abbreviations have been copied out already, and
   * design.md section 5.1 keeps leap seconds out of the instant entirely, so
   * a TZif file's leap table is not this library's authority for them -
   * leap.h's own table is (phase 4). */
  r->pos = chars_at + h->charcnt;
  {
    size_t skip;
    if (!gcu_safe_mul_size(h->leapcnt, time_size + 4, &skip)) {
      return GCHRON_ERR_CORRUPT;
    }
    if (!have(r, skip)) {
      return GCHRON_ERR_CORRUPT;
    }
    r->pos += skip;
    if (!have(r, (size_t)h->isstdcnt + (size_t)h->isutcnt)) {
      return GCHRON_ERR_CORRUPT;
    }
    r->pos += (size_t)h->isstdcnt + (size_t)h->isutcnt;
  }

  /* RFC 8536 section 4: for a timestamp before the first transition, use the
   * first type that is not a daylight-saving one, or type 0 when every type
   * is. Resolved once, here, because getting it wrong is invisible until
   * somebody asks about a date before the zone's records begin. */
  zone->first_type = 0;
  for (i = 0; i < zone->type_count; ++i) {
    if (!zone->types[i].is_dst) {
      zone->first_type = (uint8_t)i;
      break;
    }
  }
  return GCHRON_OK;
}

/**
 * Read the version-2 footer: a newline, a POSIX TZ string, a newline.
 *
 * An empty string between the newlines is legal and means the zone has no
 * rule for times after its last transition - which is what a zone that has
 * stopped changing looks like.
 */
static GCHRON_Result read_footer(Reader * r, GCHRON_Zone * zone) {
  size_t start;
  size_t end;

  zone->has_rule = false;
  if (!have(r, 1) || r->data[r->pos] != '\n') {
    /* No footer at all. Legal: the far future is then the last transition's
     * type, which on a "fat" file runs to 2037 and then stops changing. */
    return GCHRON_OK;
  }
  r->pos += 1;
  start = r->pos;
  end = start;
  while (end < r->len && r->data[end] != '\n') {
    end += 1;
  }
  if (end >= r->len) {
    return GCHRON_ERR_CORRUPT;
  }
  r->pos = end + 1;

  if (end == start) {
    return GCHRON_OK;
  }
  if (gchron_posixtz_parse((const char *)r->data + start, end - start,
          &zone->rule) != GCHRON_OK) {
    /* The footer is the second untrusted input of section 1.2, and a
     * malformed one is a corrupt file rather than a zone with no rule: a
     * reader that shrugged it off would answer every question past 2037 with
     * the wrong offset and say nothing. */
    return GCHRON_ERR_CORRUPT;
  }
  zone->has_rule = true;
  return GCHRON_OK;
}

void gchron_zone_free(GCHRON_Zone * zone) {
  const GCHRON_Allocator * alloc;

  if (zone == NULL) {
    return;
  }
  alloc = zone->allocator;
  gcu_allocator_free(alloc, zone->transition_at);
  gcu_allocator_free(alloc, zone->transition_type);
  gcu_allocator_free(alloc, zone->types);
  gcu_allocator_free(alloc, zone->id);
  if (zone->canonical_id != zone->id) {
    gcu_allocator_free(alloc, zone->canonical_id);
  }
  gcu_allocator_free(alloc, zone);
}

GCHRON_Result gchron_tzif_parse(const void * blob, size_t len,
    const GCHRON_Limits * limits, const GCHRON_Allocator * allocator,
    GCHRON_Zone ** out) {
  Reader r;
  Header v1;
  Header v2;
  GCHRON_Zone * zone;
  GCHRON_Result result;
  size_t v1_bytes;

  if (blob == NULL || out == NULL || limits == NULL || allocator == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (limits->max_tzif_bytes != 0 && len > limits->max_tzif_bytes) {
    return GCHRON_ERR_LIMIT;
  }

  r.data = (const uint8_t *)blob;
  r.len = len;
  r.pos = 0;

  result = read_header(&r, &v1);
  if (result != GCHRON_OK) {
    return result;
  }

  zone = (GCHRON_Zone *)gcu_allocator_calloc(allocator, 1,
      sizeof(GCHRON_Zone));
  if (zone == NULL) {
    return GCHRON_ERR_OOM;
  }
  zone->allocator = allocator;

  if (v1.version >= 2) {
    /*
     * A version-2 file repeats everything: a 32-bit block for readers that
     * only know version 1, then a second header and a 64-bit block. The
     * 32-bit block is skipped outright rather than parsed and discarded -
     * its transition times are truncated to 2038 and its content is
     * redundant, and parsing data nothing will use is surface a hostile file
     * can aim at.
     */
    if (!block_bytes(&v1, 4, &v1_bytes) || !have(&r, v1_bytes)) {
      result = GCHRON_ERR_CORRUPT;
      goto fail;
    }
    r.pos += v1_bytes;

    result = read_header(&r, &v2);
    if (result != GCHRON_OK) {
      goto fail;
    }
    result = check_block_fits(&r, &v2, 8);
    if (result != GCHRON_OK) {
      goto fail;
    }
    result = check_limits(&v2, limits);
    if (result != GCHRON_OK) {
      goto fail;
    }
    result = read_block(&r, &v2, 8, zone);
    if (result != GCHRON_OK) {
      goto fail;
    }
    result = read_footer(&r, zone);
    if (result != GCHRON_OK) {
      goto fail;
    }
  }
  else {
    result = check_block_fits(&r, &v1, 4);
    if (result != GCHRON_OK) {
      goto fail;
    }
    result = check_limits(&v1, limits);
    if (result != GCHRON_OK) {
      goto fail;
    }
    result = read_block(&r, &v1, 4, zone);
    if (result != GCHRON_OK) {
      goto fail;
    }
  }

  zone->is_fixed = (zone->transition_count == 0 && zone->type_count == 1
      && !zone->has_rule);
  *out = zone;
  return GCHRON_OK;

fail:
  gchron_zone_free(zone);
  return result;
}

GCHRON_Result gchron_zone_build_fixed(int32_t offset_sec, const char * abbrev,
    const char * id, const GCHRON_Allocator * allocator, GCHRON_Zone ** out) {
  GCHRON_Zone * zone;
  char synthesised[GCHRON_ABBREV_MAX + 1];

  if (out == NULL || allocator == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (offset_sec <= -GCHRON_OFFSET_LIMIT_SECONDS
      || offset_sec >= GCHRON_OFFSET_LIMIT_SECONDS) {
    return GCHRON_ERR_INVALID;
  }

  zone = (GCHRON_Zone *)gcu_allocator_calloc(allocator, 1,
      sizeof(GCHRON_Zone));
  if (zone == NULL) {
    return GCHRON_ERR_OOM;
  }
  zone->allocator = allocator;
  zone->types = (GCHRON_ZoneType *)gcu_allocator_calloc(allocator, 1,
      sizeof(GCHRON_ZoneType));
  if (zone->types == NULL) {
    gchron_zone_free(zone);
    return GCHRON_ERR_OOM;
  }
  zone->type_count = 1;
  zone->types[0].utoff = offset_sec;
  zone->types[0].is_dst = false;

  if (abbrev == NULL) {
    /* The spelling the tzdb itself uses for a synthesised fixed zone, so
     * that "+05:30" reads the way `zdump` prints it rather than inventing a
     * second convention. */
    int32_t magnitude = offset_sec < 0 ? -offset_sec : offset_sec;
    int hours = (int)(magnitude / 3600);
    int minutes = (int)((magnitude / 60) % 60);
    int seconds = (int)(magnitude % 60);
    if (offset_sec == 0) {
      memcpy(synthesised, "UTC", 4);
    }
    else if (seconds != 0) {
      snprintf(synthesised, sizeof(synthesised), "%c%02d%02d%02d",
          offset_sec < 0 ? '-' : '+', hours, minutes, seconds);
    }
    else if (minutes != 0) {
      snprintf(synthesised, sizeof(synthesised), "%c%02d%02d",
          offset_sec < 0 ? '-' : '+', hours, minutes);
    }
    else {
      snprintf(synthesised, sizeof(synthesised), "%c%02d",
          offset_sec < 0 ? '-' : '+', hours);
    }
    abbrev = synthesised;
  }
  if (strlen(abbrev) > GCHRON_ABBREV_MAX) {
    gchron_zone_free(zone);
    return GCHRON_ERR_INVALID;
  }
  strcpy(zone->types[0].abbrev, abbrev);

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

  zone->first_type = 0;
  zone->is_fixed = true;
  *out = zone;
  return GCHRON_OK;
}
