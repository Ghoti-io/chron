/**
 * @file
 *
 * What a zone and a database are made of, and the POSIX TZ rule the TZif
 * footer carries. Private; installed with nothing.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_SRC_ZONE_ZONE_INTERNAL_H
#define GHOTI_IO_GCHRON_SRC_ZONE_ZONE_INTERNAL_H

#include <ghoti.io/chron/allocator.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/zone.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The longest time-zone abbreviation this library will hold.
 *
 * RFC 8536 section 3.3 says a designation SHOULD be at most six characters,
 * and POSIX allows more. Sixteen is generous enough that no real zone comes
 * near it and small enough that it is a fixed array rather than an
 * allocation - which matters because a hostile TZif file chooses this length.
 */
#define GCHRON_ABBREV_MAX 16

/** How a POSIX TZ rule names the day a transition falls on. */
typedef enum {
  GCHRON_TZRULE_NONE = 0, /**< No rule; daylight saving never starts. */
  GCHRON_TZRULE_JULIAN,   /**< `Jn`: day n of 1..365, 29 February never counted. */
  GCHRON_TZRULE_ZERO,     /**< `n`: day n of 0..365, 29 February counted. */
  GCHRON_TZRULE_MONTH     /**< `Mm.w.d`: the w-th d-day of month m. */
} GCHRON_TzRuleKind;

/** One end of a POSIX TZ daylight-saving rule. */
typedef struct GCHRON_TzRuleDate {
  GCHRON_TzRuleKind kind; /**< Which of the three spellings this is. */
  int n;                  /**< `Jn` and `n`: the day number. */
  int month;              /**< `M`: 1..12. */
  int week;               /**< `M`: 1..5, where 5 means the last. */
  int day;                /**< `M`: 0 = Sunday .. 6 = Saturday, as POSIX numbers it. */

  /**
   * Seconds after midnight at which the change happens, in the local time in
   * force just before it.
   *
   * POSIX.1-2024 widened this to -167:59:59..167:59:59 - a full week either
   * way - so it can carry the transition into an adjacent day, and the
   * evaluation must not assume it lands inside the named one.
   */
  int32_t time;
} GCHRON_TzRuleDate;

/**
 * A POSIX TZ string, parsed.
 *
 * Offsets here are stored the way the rest of the library stores them:
 * **seconds ahead of UTC**. The string itself writes them the other way round
 * - `EST5EDT` means UTC-5 - and the parser flips the sign once, at the
 * boundary, so that nothing downstream has to remember which convention it is
 * looking at.
 */
typedef struct GCHRON_PosixTz {
  char std_abbrev[GCHRON_ABBREV_MAX + 1]; /**< NUL-terminated. */
  int32_t std_offset;                     /**< Seconds ahead of UTC. */
  bool has_dst;                           /**< A daylight-saving name followed. */
  char dst_abbrev[GCHRON_ABBREV_MAX + 1]; /**< NUL-terminated. */
  int32_t dst_offset;                     /**< Seconds ahead of UTC. */
  bool has_rule;                          /**< Start and end dates followed. */
  GCHRON_TzRuleDate start;                /**< When daylight saving begins. */
  GCHRON_TzRuleDate end;                  /**< When it ends. */
} GCHRON_PosixTz;

/** One local-time type out of a TZif file. */
typedef struct GCHRON_ZoneType {
  int32_t utoff;                    /**< Seconds ahead of UTC. */
  bool is_dst;                      /**< The zone called this daylight saving. */
  char abbrev[GCHRON_ABBREV_MAX + 1]; /**< NUL-terminated designation. */
} GCHRON_ZoneType;

/**
 * A zone's rules.
 *
 * Immutable once built. The database owns it; every caller borrows it.
 */
struct GCHRON_Zone {
  const GCHRON_Allocator * allocator; /**< Whose memory this is. */
  char * id;                          /**< As asked for; NULL when anonymous. */
  char * canonical_id;                /**< What a link resolved to; may equal id. */

  size_t transition_count;  /**< Entries in the two arrays below. */
  int64_t * transition_at;  /**< Strictly increasing Unix seconds. */
  uint8_t * transition_type;/**< Index into @ref types. */

  size_t type_count;        /**< Entries in @ref types; at least one. */
  GCHRON_ZoneType * types;  /**< The local-time types. */

  /**
   * The type in force before the first transition.
   *
   * RFC 8536 section 4: the first type that is not a daylight-saving one, or
   * type 0 when every type is. Resolved once at load rather than at each
   * lookup, because getting it wrong is invisible until someone asks about a
   * date before the zone's records begin.
   */
  uint8_t first_type;

  bool has_rule;            /**< A POSIX TZ footer governs the far future. */
  GCHRON_PosixTz rule;      /**< That footer, parsed. */

  bool is_fixed;            /**< One offset for all time. */
};

/**
 * Parse a TZif image into a zone.
 *
 * @param blob The bytes. Not retained; everything is copied.
 * @param len Bytes at @p blob.
 * @param limits Caps. Never NULL.
 * @param allocator Whose memory to use. Never NULL.
 * @param out Receives a zone the caller owns and frees with
 *   gchron_zone_free(); untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_CORRUPT; GCHRON_ERR_LIMIT; GCHRON_ERR_OOM.
 */
GCHRON_Result gchron_tzif_parse(const void * blob, size_t len,
    const GCHRON_Limits * limits, const GCHRON_Allocator * allocator,
    GCHRON_Zone ** out);

/**
 * Free a zone built by gchron_tzif_parse() or gchron_zone_build_fixed().
 *
 * @param zone The zone. NULL is ignored.
 */
void gchron_zone_free(GCHRON_Zone * zone);

/**
 * Build a zone that is one fixed offset for all time.
 *
 * @param offset_sec Seconds ahead of UTC.
 * @param abbrev The designation to report, or NULL to synthesise one.
 * @param id The identifier to give it, or NULL.
 * @param allocator Whose memory to use. Never NULL.
 * @param out Receives the zone; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_OOM.
 */
GCHRON_Result gchron_zone_build_fixed(int32_t offset_sec, const char * abbrev,
    const char * id, const GCHRON_Allocator * allocator, GCHRON_Zone ** out);

/**
 * Build a zone out of a POSIX TZ string alone, with no transition table.
 *
 * What `TZ=CET-1CEST,M3.5.0,M10.5.0/3` becomes.
 *
 * @param text The rule. Not assumed to be NUL-terminated.
 * @param len Bytes at @p text.
 * @param id The identifier to give it, or NULL for an anonymous zone.
 * @param allocator Whose memory to use. Never NULL.
 * @param out Receives the zone; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_FORMAT, GCHRON_ERR_INVALID or GCHRON_ERR_OOM.
 */
GCHRON_Result gchron_zone_build_posix(const char * text, size_t len,
    const char * id, const GCHRON_Allocator * allocator, GCHRON_Zone ** out);

/**
 * Parse a POSIX TZ string.
 *
 * @param text The rule. Not assumed to be NUL-terminated.
 * @param len Bytes at @p text.
 * @param out Receives the parsed rule; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_FORMAT.
 */
GCHRON_Result gchron_posixtz_parse(const char * text, size_t len,
    GCHRON_PosixTz * out);

/**
 * What a POSIX TZ rule says was in force at an instant.
 *
 * @param rule The rule.
 * @param instant Unix seconds.
 * @param out Receives the offset, the DST flag and the abbreviation.
 * @return GCHRON_OK, or GCHRON_ERR_RANGE when the instant is outside the
 *   years the rule can be evaluated over.
 */
GCHRON_Result gchron_posixtz_offset_at(const GCHRON_PosixTz * rule,
    int64_t instant, GCHRON_ZoneInfo * out);

/**
 * The first change a POSIX TZ rule makes strictly after an instant.
 *
 * @param rule The rule.
 * @param after Unix seconds.
 * @param out_at Receives when. May be NULL.
 * @param out_before Receives what was in force up to then. May be NULL.
 * @param out_after Receives what is in force from then. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_UNSUPPORTED when the rule never changes;
 *   GCHRON_ERR_RANGE.
 */
GCHRON_Result gchron_posixtz_next_transition(const GCHRON_PosixTz * rule,
    int64_t after, int64_t * out_at, GCHRON_ZoneInfo * out_before,
    GCHRON_ZoneInfo * out_after);

/**
 * The last change a POSIX TZ rule made at or before an instant.
 *
 * @param rule The rule.
 * @param before Unix seconds.
 * @param out_at Receives when. May be NULL.
 * @param out_before Receives what was in force up to then. May be NULL.
 * @param out_after Receives what is in force from then. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_UNSUPPORTED; GCHRON_ERR_RANGE.
 */
GCHRON_Result gchron_posixtz_prev_transition(const GCHRON_PosixTz * rule,
    int64_t before, int64_t * out_at, GCHRON_ZoneInfo * out_before,
    GCHRON_ZoneInfo * out_after);

/**
 * Read a whole file into memory, bounded.
 *
 * @param path The file.
 * @param max_bytes Refuse a file larger than this; 0 means no limit.
 * @param allocator Whose memory to use. Never NULL.
 * @param out_data Receives the bytes, which the caller frees.
 * @param out_len Receives how many.
 * @return GCHRON_OK; GCHRON_ERR_IO; GCHRON_ERR_LIMIT; GCHRON_ERR_OOM.
 */
GCHRON_Result gchron_zone_read_file(const char * path, size_t max_bytes,
    const GCHRON_Allocator * allocator, void ** out_data, size_t * out_len);

/**
 * Whether an identifier could name a file under a zoneinfo directory.
 *
 * Refuses an absolute path, a `..` component, a leading or doubled `/`, a
 * backslash, and any byte outside the small set real identifiers use. A zone
 * identifier reaches this library from a document - RFC 9557 puts one in a
 * timestamp - so it is untrusted input that is about to be turned into a
 * path.
 *
 * @param id The identifier.
 * @return `true` when it is safe to append to a directory.
 */
bool gchron_zone_id_is_safe(const char * id);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_SRC_ZONE_ZONE_INTERNAL_H
