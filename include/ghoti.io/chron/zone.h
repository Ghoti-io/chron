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
 * Named time zones: what offset a place was on at an instant, when that last
 * changed, and what instants a wall-clock reading could have named.
 *
 * This is the first header that needs data on
 * disk. A consumer that only parses timestamps never includes it and never
 * loads a byte of time-zone data.
 *
 * **There is no process-wide zone.** Every operation here takes a zone handle,
 * every zone comes from a database the caller created, and the two functions
 * that read the environment say so in their names (design.md, mistake M2).
 * `TZ`, `TZDIR`, `/etc/localtime` and the Windows registry are read by
 * gchron_zonedb_local() and by nothing else.
 *
 * Reference: RFC 8536, *The Time Zone Information Format (TZif)* (2019), and
 * `tzfile(5)` for the version-4 additions; POSIX.1-2024, *Environment
 * Variables*, `TZ`.
 */

#ifndef GHOTI_IO_GCHRON_ZONE_H
#define GHOTI_IO_GCHRON_ZONE_H

#include <ghoti.io/chron/allocator.h>
#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A time-zone database: a set of zones, loaded lazily and cached.
 *
 * Created by the caller, destroyed by the caller, and **internally
 * synchronised** - one database serves a whole process. That is a stronger
 * promise than CONVENTIONS.md section 5's default, and it is made because the
 * alternative is every application inventing a lock around it.
 */
typedef struct GCHRON_ZoneDb GCHRON_ZoneDb;

/**
 * @brief One zone's rules.
 *
 * Immutable once returned, **borrowed** from the database that produced it,
 * and valid until that database is destroyed. Never freed by the caller.
 */
typedef struct GCHRON_Zone GCHRON_Zone;

/** @brief Where a database's data came from. */
typedef enum {
  GCHRON_ZONE_SOURCE_NONE = 0, ///< Not loaded.
  GCHRON_ZONE_SOURCE_SYSTEM,   ///< `$TZDIR`, or the platform's zoneinfo.
  GCHRON_ZONE_SOURCE_EMBEDDED, ///< The table the build generated.
  GCHRON_ZONE_SOURCE_DIRECTORY,///< A directory the caller named.
  GCHRON_ZONE_SOURCE_MEMORY,   ///< A blob the caller supplied.
  GCHRON_ZONE_SOURCE_FIXED     ///< Synthesised: a fixed offset, or UTC.
} GCHRON_ZoneSource;

/**
 * @brief What a zone was doing at some instant.
 *
 * `abbreviation` is borrowed from the zone and lives as long as it does.
 * It is **output-only**: no parser in this library accepts an abbreviation as
 * a zone, because `CST` is China, Cuba and Central, and `IST` is India, Israel
 * and Ireland (design.md, mistake M12).
 */
typedef struct GCHRON_ZoneInfo {
  int32_t offset_sec; ///< Seconds ahead of UTC.

  /**
   * The zone considered this its daylight-saving time.
   *
   * Not "the offset is one hour larger". Europe/Dublin runs Irish Standard
   * Time in summer and marks *winter* as the daylight-saving type, with a
   * negative offset from standard; a caller that infers the offset from this
   * flag gets Ireland backwards.
   */
  bool is_dst;

  /** The abbreviation in force, e.g. `"EST"`. Borrowed; never NULL. */
  const char * abbreviation;
} GCHRON_ZoneInfo;

/**
 * @brief A moment a zone's rules changed, and what they were on each side.
 *
 * "When does the next change happen" is what a cron daemon, a countdown and a
 * log rotator all ask, and answering it by probing instants in a loop is how
 * each of them gets it wrong.
 */
typedef struct GCHRON_Transition {
  GCHRON_Instant at;       ///< The first instant the new rules apply.
  GCHRON_ZoneInfo before;  ///< What was in force up to @ref at.
  GCHRON_ZoneInfo after;   ///< What is in force from @ref at.
} GCHRON_Transition;

/**
 * @brief What instants a wall-clock reading could have named in a zone.
 *
 * Zero of them (the reading fell in a gap), one (the ordinary case), or two
 * (it fell in an overlap and happened twice).
 *
 * Public, and not just a private step inside gchron_zoned_from_civil(),
 * because a scheduler or a calendar interface needs to **show** the ambiguity
 * - "1:30 happens twice that night; which did you mean?" - rather than have a
 * policy resolve it out of sight.
 */
typedef struct GCHRON_CivilOffsets {
  /** 0 for a gap, 1 for the ordinary case, 2 for an overlap. */
  int count;

  /** The candidates, @ref count of them, earliest first. */
  GCHRON_ZoneInfo options[2];

  /** The instant each candidate names, @ref count of them. */
  GCHRON_Instant instants[2];

  /**
   * The transition that made this a gap or an overlap.
   *
   * Only meaningful when @ref count is not 1.
   */
  GCHRON_Instant transition;

  /**
   * How long the gap is, in seconds; 0 unless @ref count is 0.
   *
   * This is what `GCHRON_RESOLVE_COMPATIBLE` pushes a reading forward by.
   */
  int32_t gap_seconds;
} GCHRON_CivilOffsets;

/**
 * @brief What to do when a wall-clock reading names no instant, or two.
 *
 * design.md, mistake M4: `struct tm`'s `tm_isdst` is a tri-state where the
 * third state means "you figure it out", and every caller figured it out
 * differently. This is the policy that replaces it, and it is a parameter on
 * the conversion rather than a setting on the process - because
 * gchron_zoned_start_of_day() wants `LATER` for a gap specifically, and a
 * library with one process-wide policy could not say so (mistake M11).
 */
typedef enum {
  /**
   * A gap is `GCHRON_ERR_GAP`; an overlap is `GCHRON_ERR_AMBIGUOUS`.
   *
   * Zero, per design.md section 3.7. Temporal's default is `COMPATIBLE`, and
   * a caller who wants what Temporal does writes the word.
   */
  GCHRON_RESOLVE_REJECT = 0,

  /** Overlap: the first occurrence. Gap: the instant just before it. */
  GCHRON_RESOLVE_EARLIER,

  /** Overlap: the second occurrence. Gap: the instant just after it. */
  GCHRON_RESOLVE_LATER,

  /**
   * Overlap: the first occurrence. Gap: push the reading forward by the
   * length of the gap, which is what `java.time` and legacy JavaScript
   * `Date` do - so `02:30` on a spring-forward morning becomes `03:30`.
   */
  GCHRON_RESOLVE_COMPATIBLE
} GCHRON_Resolve;

/*--------------------------------------------------------------------------*
 * Creating a database
 *--------------------------------------------------------------------------*/

/**
 * @brief Open whichever database this machine has, without an opinion.
 *
 * What an application that has not thought about it should call. It prefers
 * the operating system's copy, because that is the one somebody is updating,
 * and falls back to the table the build embedded where there is no system
 * database at all - which today means Windows.
 *
 * Where both exist, the newer by version wins: tzdata releases are `YYYYx`
 * and order lexically. A system database whose version cannot be determined
 * is preferred anyway. Whichever was chosen, gchron_zonedb_source() and
 * gchron_zonedb_version() say so, and gchron_zonedb_dump() prints both - a
 * fallback that cannot be seen is a defect, and this one can be seen.
 *
 * The choice is made once, for the whole database; zones are never mixed from
 * two sources.
 *
 * @param allocator Allocator for the database and its zones. NULL means
 *   gchron_allocator_default().
 * @param limits Caps on what a zone file may contain. NULL means
 *   gchron_limits_default().
 * @param out Receives the database on success; untouched on failure. Destroy
 *   it with gchron_zonedb_destroy().
 * @return GCHRON_OK; GCHRON_ERR_UNSUPPORTED when this build has neither a
 *   system database nor an embedded table; GCHRON_ERR_OOM; GCHRON_ERR_IO.
 */
GCHRON_API GCHRON_Result gchron_zonedb_default(const GCHRON_Allocator * allocator,
    const GCHRON_Limits * limits, GCHRON_ZoneDb ** out);

/**
 * @brief Open the operating system's zoneinfo directory.
 *
 * `$TZDIR` if it is set, otherwise the platform's location. On Windows there
 * is none, and this returns GCHRON_ERR_UNSUPPORTED with a message saying to
 * use gchron_zonedb_embedded() or gchron_zonedb_default(): the explicit call
 * does not quietly fall back, because a caller who named the system database
 * wanted the system database.
 *
 * @param allocator Allocator. NULL means the default.
 * @param limits Caps. NULL means the default.
 * @param out Receives the database on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_UNSUPPORTED; GCHRON_ERR_IO; GCHRON_ERR_OOM.
 */
GCHRON_API GCHRON_Result gchron_zonedb_system(const GCHRON_Allocator * allocator,
    const GCHRON_Limits * limits, GCHRON_ZoneDb ** out);

/**
 * @brief Open a directory of TZif files the caller names.
 *
 * For a container that ships its own copy, or a test that ships a fixture.
 *
 * @param path The directory. Copied; the caller may free it.
 * @param allocator Allocator. NULL means the default.
 * @param limits Caps. NULL means the default.
 * @param out Receives the database on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_IO when the directory cannot be read;
 *   GCHRON_ERR_INVALID; GCHRON_ERR_OOM.
 */
GCHRON_API GCHRON_Result gchron_zonedb_directory(const char * path,
    const GCHRON_Allocator * allocator, const GCHRON_Limits * limits,
    GCHRON_ZoneDb ** out);

/**
 * @brief Open the table the build embedded.
 *
 * Phase 4 generates that table from an IANA source tree; until then this
 * returns GCHRON_ERR_UNSUPPORTED rather than an empty database, because a
 * database with no zones in it answers every lookup with
 * GCHRON_ERR_UNSUPPORTED anyway and hides the reason.
 *
 * @param allocator Allocator. NULL means the default.
 * @param limits Caps. NULL means the default.
 * @param out Receives the database on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_UNSUPPORTED.
 */
GCHRON_API GCHRON_Result gchron_zonedb_embedded(
    const GCHRON_Allocator * allocator, const GCHRON_Limits * limits,
    GCHRON_ZoneDb ** out);

/**
 * @brief Open a single TZif image held in memory, as one anonymous zone.
 *
 * What a fuzz harness hands the reader, and what a caller with a zone file in
 * a resource bundle uses.
 *
 * @param blob The TZif bytes. Copied; the caller may free them.
 * @param len Bytes at @p blob.
 * @param id An identifier to give the zone, or NULL for an anonymous one.
 *   Copied.
 * @param allocator Allocator. NULL means the default.
 * @param limits Caps. NULL means the default.
 * @param out Receives the database on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_CORRUPT when the bytes are not TZif;
 *   GCHRON_ERR_LIMIT; GCHRON_ERR_INVALID; GCHRON_ERR_OOM.
 */
GCHRON_API GCHRON_Result gchron_zonedb_memory(const void * blob, size_t len,
    const char * id, const GCHRON_Allocator * allocator,
    const GCHRON_Limits * limits, GCHRON_ZoneDb ** out);

/**
 * @brief Destroy a database and every zone it handed out.
 *
 * Every `const GCHRON_Zone *` that came from this database becomes invalid.
 *
 * @param db The database. NULL is ignored.
 */
GCHRON_API void gchron_zonedb_destroy(GCHRON_ZoneDb * db);

/*--------------------------------------------------------------------------*
 * Asking a database for a zone
 *--------------------------------------------------------------------------*/

/**
 * @brief Look a zone up by its IANA identifier.
 *
 * Zones are loaded on first use and cached, so the second call is a hash
 * lookup. Backward-compatibility links resolve - `US/Eastern` and
 * `Asia/Calcutta` both work - and gchron_zone_canonical_id() says what they
 * resolved to.
 *
 * @param db The database.
 * @param id An identifier such as `"Europe/Paris"`. An abbreviation such as
 *   `"CST"` is **not** an identifier and is not accepted (mistake M12).
 * @param out Receives a borrowed zone on success; untouched on failure. Valid
 *   until the database is destroyed; never freed by the caller.
 * @return GCHRON_OK; GCHRON_ERR_UNSUPPORTED when the database has no such
 *   zone; GCHRON_ERR_INVALID for an identifier that could not name a file;
 *   GCHRON_ERR_CORRUPT; GCHRON_ERR_LIMIT; GCHRON_ERR_IO; GCHRON_ERR_OOM.
 */
GCHRON_API GCHRON_Result gchron_zonedb_zone(GCHRON_ZoneDb * db, const char * id,
    const GCHRON_Zone ** out);

/**
 * @brief A zone that is one fixed offset for all time.
 *
 * A fixed offset is a zone too, so that every code path is one code path.
 *
 * @param db The database, which caches it.
 * @param offset_sec Seconds ahead of UTC, strictly between -86400 and 86400.
 * @param out Receives a borrowed zone on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_OOM.
 */
GCHRON_API GCHRON_Result gchron_zonedb_fixed(GCHRON_ZoneDb * db,
    int32_t offset_sec, const GCHRON_Zone ** out);

/**
 * @brief UTC, as a zone.
 *
 * @param db The database.
 * @param out Receives a borrowed zone on success; untouched on failure.
 * @return GCHRON_OK or GCHRON_ERR_OOM.
 */
GCHRON_API GCHRON_Result gchron_zonedb_utc(GCHRON_ZoneDb * db,
    const GCHRON_Zone ** out);

/**
 * @brief A zone built from a POSIX `TZ` rule string.
 *
 * `EST5EDT,M3.2.0,M11.1.0`, or `CET-1CEST,M3.5.0,M10.5.0/3`. This is the
 * grammar in the footer of every version-2 TZif file and in the `TZ`
 * environment variable, and it is what a system with no zoneinfo directory
 * has instead of one - an embedded target, or a container built from
 * scratch.
 *
 * A rule carries no history: it describes one pair of offsets and one
 * recurring changeover, applied to every year alike. Where a real zone file
 * exists, prefer it - `gchron_zonedb_zone()` gives the zone's actual past,
 * which no rule string can.
 *
 * A rule with no daylight-saving half (`UTC0`, `MST7`) is a fixed-offset
 * zone, and comes back reporting gchron_zone_is_fixed().
 *
 * @param db The database, which caches the result by rule text.
 * @param rule The rule. NUL-terminated.
 * @param out Receives a borrowed zone on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_FORMAT when the rule is not this grammar;
 *   GCHRON_ERR_INVALID; GCHRON_ERR_OOM.
 */
GCHRON_API GCHRON_Result gchron_zonedb_posix(GCHRON_ZoneDb * db,
    const char * rule, const GCHRON_Zone ** out);

/**
 * @brief The zone this machine is set to.
 *
 * **The one function in this library that reads the environment.** In order:
 *
 * 1. `TZ`, if set. Both forms are accepted: an IANA identifier
 *    (`Europe/Paris`, with or without the leading colon), and a POSIX rule
 *    string (`CET-1CEST,M3.5.0,M10.5.0/3`), which becomes an anonymous zone.
 * 2. `/etc/localtime`. A symlink gives the identifier from its target's path
 *    under `zoneinfo`; a regular file is read as an anonymous TZif zone, and
 *    Debian's `/etc/timezone` is consulted for the name.
 * 3. On Windows, `GetDynamicTimeZoneInformation()` and the CLDR
 *    `windowsZones.xml` mapping, read from the `TimeZoneKeyName` registry
 *    value rather than the localised display name. A zone Windows added
 *    after the mapping's CLDR release has no identifier here and is refused
 *    with `GCHRON_ERR_RANGE` rather than guessed at;
 *    gchron_zone_windows_mapping_version() names that release.
 *
 * Nothing else in the library calls this. A GCHRON_ZonedDateTime never has an
 * implicit zone.
 *
 * @param db The database, which caches the result.
 * @param out Receives a borrowed zone on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_UNSUPPORTED when the machine's zone cannot be
 *   determined; the loader's errors otherwise.
 */
GCHRON_API GCHRON_Result gchron_zonedb_local(GCHRON_ZoneDb * db,
    const GCHRON_Zone ** out);

/*--------------------------------------------------------------------------*
 * What the data says about itself
 *--------------------------------------------------------------------------*/

/**
 * @brief Where this database's data came from.
 *
 * @param db The database.
 * @return The source, or GCHRON_ZONE_SOURCE_NONE for NULL.
 */
GCHRON_API GCHRON_ZoneSource gchron_zonedb_source(const GCHRON_ZoneDb * db);

/**
 * @brief The tzdata release this database holds, e.g. `"2026c"`.
 *
 * When a timestamp in a log is disputed, the first question is which rules
 * produced it, and a library that cannot answer has made the dispute
 * unresolvable (design.md section 6.6).
 *
 * @param db The database.
 * @return A borrowed string, or NULL when the version cannot be determined.
 */
GCHRON_API const char * gchron_zonedb_version(const GCHRON_ZoneDb * db);

/**
 * @brief The identifiers this database can supply.
 *
 * @param db The database.
 * @param out_ids Receives a borrowed array of borrowed strings, valid until
 *   the database is destroyed. May be NULL.
 * @param out_count Receives how many. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_UNSUPPORTED for a database that cannot
 *   enumerate itself; GCHRON_ERR_OOM.
 */
GCHRON_API GCHRON_Result gchron_zonedb_list(GCHRON_ZoneDb * db,
    const char * const ** out_ids, size_t * out_count);

/**
 * @brief Write a human-readable description of a database to a stream.
 *
 * Prints the source and the version - both of them, when
 * gchron_zonedb_default() had two to choose between.
 *
 * @param db The database. NULL prints as such.
 * @param stream Where to write. NULL is ignored.
 */
GCHRON_API void gchron_zonedb_dump(const GCHRON_ZoneDb * db, FILE * stream);

/*--------------------------------------------------------------------------*
 * What a zone answers
 *--------------------------------------------------------------------------*/

/**
 * @brief The longest zone identifier this library will carry, without its NUL.
 *
 * The longest the tzdb ships is `America/Argentina/ComodRivadavia` at 32
 * bytes; 128 leaves room for a name it has not invented yet while keeping
 * every buffer that holds one a stack allocation.
 */
#define GCHRON_ZONE_ID_MAX ((size_t)128)

/**
 * @brief The identifier a zone was asked for, or NULL for an anonymous one.
 *
 * @param zone The zone.
 * @return A borrowed string, or NULL.
 */
GCHRON_API const char * gchron_zone_id(const GCHRON_Zone * zone);

/**
 * @brief What a backward-compatibility link resolved to.
 *
 * `US/Eastern` resolves to `America/New_York`, and this says so.
 *
 * @param zone The zone.
 * @return A borrowed string, or NULL when the zone is anonymous.
 */
GCHRON_API const char * gchron_zone_canonical_id(const GCHRON_Zone * zone);

/**
 * @brief Whether a zone is one fixed offset for all time.
 *
 * @param zone The zone.
 * @return `true` for a fixed-offset zone or UTC.
 */
GCHRON_API bool gchron_zone_is_fixed(const GCHRON_Zone * zone);

/**
 * @brief The offset, DST flag and abbreviation in force at an instant.
 *
 * @param zone The zone.
 * @param instant The instant.
 * @param out Receives the answer on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE when the instant
 *   falls outside the years the rules can be evaluated over.
 */
GCHRON_API GCHRON_Result gchron_zone_offset_at(const GCHRON_Zone * zone,
    GCHRON_Instant instant, GCHRON_ZoneInfo * out);

/**
 * @brief The first rule change strictly after an instant.
 *
 * @param zone The zone.
 * @param after The instant to search from.
 * @param out Receives the transition on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_UNSUPPORTED when the zone never changes
 *   again - a fixed-offset zone, or one whose rules have no further
 *   transitions; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_zone_next_transition(const GCHRON_Zone * zone,
    GCHRON_Instant after, GCHRON_Transition * out);

/**
 * @brief The last rule change at or before an instant.
 *
 * @param zone The zone.
 * @param before The instant to search back from.
 * @param out Receives the transition on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_UNSUPPORTED when the zone has no earlier
 *   change; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_zone_prev_transition(const GCHRON_Zone * zone,
    GCHRON_Instant before, GCHRON_Transition * out);

/**
 * @brief What instants a wall-clock reading could have named in this zone.
 *
 * The primitive under gchron_zoned_from_civil(), public so that a caller can
 * show the ambiguity rather than resolve it out of sight.
 *
 * @param zone The zone.
 * @param civil A valid civil date-time.
 * @param out Receives the candidates on success; untouched on failure.
 * @return GCHRON_OK - **including** for a gap, which is a count of zero and
 *   not an error here; GCHRON_ERR_INVALID; GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_zone_offsets_for_civil(
    const GCHRON_Zone * zone, GCHRON_DateTime civil,
    GCHRON_CivilOffsets * out);

/**
 * @brief Write a human-readable description of a zone to a stream.
 *
 * @param zone The zone. NULL prints as such.
 * @param stream Where to write. NULL is ignored.
 */
GCHRON_API void gchron_zone_dump(const GCHRON_Zone * zone, FILE * stream);


/**
 * @brief The IANA identifier a Windows time-zone name means.
 *
 * Windows names its zones its own way - `"Pacific Standard Time"` where the
 * tzdb says `America/Los_Angeles` - and `GetDynamicTimeZoneInformation()`
 * reports the Windows name. CLDR publishes the mapping; this is that table.
 *
 * Public because a caller reading a Windows registry value, a `.ics` file
 * written by Outlook, or a Windows-shaped configuration needs the same
 * translation gchron_zonedb_local() needs, and on any platform.
 *
 * Pass the **registry key name** (`TimeZoneKeyName`), not the display name:
 * the display name is localised, and on a French machine reads "Heure du
 * Pacifique".
 *
 * The table is generated from a fixed CLDR release, which
 * gchron_zone_windows_mapping_version() names - so a name it does not carry
 * is a zone Windows added after that release, and the version is what makes
 * that diagnosable rather than mysterious.
 *
 * @param windows_name The Windows zone key name.
 * @param out Receives a borrowed, static identifier; never freed. Untouched
 *   on failure.
 * @return GCHRON_OK; GCHRON_ERR_RANGE when the table does not carry that
 *   name; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_zone_id_from_windows(const char * windows_name,
    const char ** out);

/**
 * @brief The CLDR release the Windows mapping came from.
 *
 * @return The version, e.g. `"release-48 (tzdata 2021a)"`. Never NULL.
 */
GCHRON_API const char * gchron_zone_windows_mapping_version(void);

/**
 * @brief How many Windows names the mapping carries.
 *
 * @return The count.
 */
GCHRON_API size_t gchron_zone_windows_mapping_count(void);

/**
 * @brief One row of the Windows mapping, by position.
 *
 * For a caller building a picker, or checking the whole table against a zone
 * database. Sorted by the Windows name.
 *
 * @param index 0 .. gchron_zone_windows_mapping_count() - 1.
 * @param out_windows_name Receives the Windows key name; borrowed and static.
 *   May be NULL.
 * @param out_id Receives the IANA identifier; borrowed and static. May be
 *   NULL.
 * @return GCHRON_OK, or GCHRON_ERR_RANGE when @p index is past the end.
 */
GCHRON_API GCHRON_Result gchron_zone_windows_mapping_at(size_t index,
    const char ** out_windows_name, const char ** out_id);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_ZONE_H
