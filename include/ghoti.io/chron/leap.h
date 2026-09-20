/**
 * @file
 *
 * Leap seconds, and the one time scale that counts them.
 *
 * Tier 1 (design.md section 3), and **optional**: nothing else in this
 * library includes this header, and an application that does not convert to
 * TAI never links a byte of it. That separation is the whole design (mistake
 * M15). Section 5.1 says why `GCHRON_Instant` is Unix time and not TAI - no
 * clock you can read counts leap seconds, no text format you will parse
 * counts them, and the table that would define them expires - and this
 * header is where the callers who genuinely need the count go.
 *
 * The data is IANA's `leap-seconds.list`, which ships beside the zone files
 * (on this machine, `/usr/share/zoneinfo/leap-seconds.list`) and is also
 * published by the IERS. It carries an expiry, and **every conversion past
 * that expiry is GCHRON_ERR_EXPIRED rather than a guess**: after the expiry
 * the file cannot say whether a leap second has since occurred, and a
 * conversion that assumed "none" would be silently wrong for six months at a
 * time.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_LEAP_H
#define GHOTI_IO_GCHRON_LEAP_H

#include <ghoti.io/chron/allocator.h>
#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An instant on the international atomic time scale.
 *
 * Seconds since 1970-01-01T00:00:00 **TAI**, which is not the same instant as
 * 1970-01-01T00:00:00 UTC: at the epoch TAI was already ahead of UTC, and the
 * table's first entry records it as ten seconds by 1972. The epoch is chosen
 * to match GCHRON_Instant's so that the difference between the two `sec`
 * fields *is* the leap offset, which is the only arithmetic anybody wants to
 * do with them.
 *
 * Unlike UTC, this scale has no repeated and no skipped seconds, so the
 * difference between two GCHRON_TaiInstant values is the true elapsed time.
 * That is the entire reason the type exists.
 */
typedef struct GCHRON_TaiInstant {
  int64_t sec;   ///< Seconds since the TAI epoch.
  int32_t nsec;  ///< 0 .. 999,999,999. Never negative.
} GCHRON_TaiInstant;

/**
 * @brief One row of the leap-second table.
 *
 * `at` is the UTC instant at which the new offset takes effect - always
 * midnight beginning a month - and `tai_minus_utc` is the offset from that
 * instant onward.
 */
typedef struct GCHRON_LeapEntry {
  GCHRON_Instant at;     ///< When this offset began, as a UTC instant.
  int32_t tai_minus_utc; ///< Seconds TAI is ahead of UTC from `at` onward.
  /**
   * Whether this row *removed* a second rather than inserting one.
   *
   * No negative leap second has ever been issued. The field exists because
   * the file format can express one and a reader that ignored the
   * possibility would misreport the day if one ever were - and because
   * section 5.1 item 5 is explicit that the library must not be built around
   * assuming it cannot happen.
   */
  bool negative;
} GCHRON_LeapEntry;

/**
 * @brief A parsed leap-second table.
 *
 * Immutable once built, and safe to share across threads (`CONVENTIONS.md`
 * section 5). The caller owns it and frees it with
 * gchron_leap_table_destroy().
 */
typedef struct GCHRON_LeapTable GCHRON_LeapTable;

/**
 * @brief Parse a `leap-seconds.list`.
 *
 * The NIST/IERS format: comment lines beginning `#`, a `#$` line carrying
 * the last-update time, a `#@` line carrying the expiry, and data lines of
 * an **NTP** timestamp (seconds since 1900-01-01) and the TAI-UTC offset
 * that begins then.
 *
 * @param text The file's bytes. Not assumed to be NUL-terminated.
 * @param len Bytes of text.
 * @param allocator NULL means gchron_allocator_default().
 * @param out Receives the table on success; untouched on failure.
 * @param err Receives the failure and its position. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_FORMAT when the file does not parse;
 *   GCHRON_ERR_LIMIT; GCHRON_ERR_OOM; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_leap_table_parse(const char * text, size_t len,
    const GCHRON_Allocator * allocator, GCHRON_LeapTable ** out,
    GCHRON_Error * err);

/**
 * @brief Read a `leap-seconds.list` from a file.
 *
 * @param path The file. NULL means the platform's usual location, and
 *   `$TZDIR` is honoured exactly as gchron_zonedb_system() honours it.
 * @param allocator NULL means gchron_allocator_default().
 * @param out Receives the table on success; untouched on failure.
 * @param err Receives the failure. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_IO when the file cannot be read;
 *   GCHRON_ERR_FORMAT; GCHRON_ERR_OOM.
 */
GCHRON_API GCHRON_Result gchron_leap_table_file(const char * path,
    const GCHRON_Allocator * allocator, GCHRON_LeapTable ** out,
    GCHRON_Error * err);

/**
 * @brief The table compiled into this library.
 *
 * Every leap second issued up to the release this was built from. It is a
 * fallback for a machine with no `leap-seconds.list`, and it **expires like
 * any other copy** - its expiry is the one the source file carried, so a
 * binary built years ago reports GCHRON_ERR_EXPIRED rather than pretending
 * its knowledge is current. That is the point of section 5.1 item 3.
 *
 * @return A borrowed, immutable table that lives for the life of the
 *   program. Never freed, and gchron_leap_table_destroy() ignores it.
 */
GCHRON_API const GCHRON_LeapTable * gchron_leap_table_builtin(void);

/**
 * @brief Free a table.
 *
 * @param table The table. NULL is ignored, and so is the builtin one.
 */
GCHRON_API void gchron_leap_table_destroy(GCHRON_LeapTable * table);

/**
 * @brief How many rows the table has.
 *
 * @param table The table. NULL reports 0.
 * @return The row count.
 */
GCHRON_API size_t gchron_leap_table_count(const GCHRON_LeapTable * table);

/**
 * @brief Read one row.
 *
 * @param table The table.
 * @param index 0 .. gchron_leap_table_count() - 1, in time order.
 * @param out Receives the row.
 * @return GCHRON_OK; GCHRON_ERR_RANGE when @p index is past the end;
 *   GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_leap_table_entry(
    const GCHRON_LeapTable * table, size_t index, GCHRON_LeapEntry * out);

/**
 * @brief When this table stops being able to answer.
 *
 * @param table The table.
 * @param out Receives the expiry, as a UTC instant.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_leap_table_expiry(
    const GCHRON_LeapTable * table, GCHRON_Instant * out);

/**
 * @brief When this table was last updated.
 *
 * The `#$` line. Distinct from the expiry: a file is updated whenever the
 * IERS republishes it, which is more often than it changes.
 *
 * @param table The table.
 * @param out Receives the update time, as a UTC instant.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_leap_table_updated(
    const GCHRON_LeapTable * table, GCHRON_Instant * out);

/**
 * @brief How far TAI is ahead of UTC at a given instant.
 *
 * @param table The table.
 * @param utc The instant, on the UTC scale.
 * @param out Receives the offset in seconds.
 * @return GCHRON_OK; GCHRON_ERR_RANGE when @p utc is before the table's
 *   first entry - UTC before 1972 ran on rubber seconds and the offset was
 *   not an integer, so there is no answer to give; GCHRON_ERR_EXPIRED when
 *   @p utc is past the expiry; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_leap_offset_at(const GCHRON_LeapTable * table,
    GCHRON_Instant utc, int32_t * out);

/**
 * @brief Whether a given UTC date ends with a leap second.
 *
 * What GCHRON_LEAP_TABLE asks in order to refuse `1999-06-30T23:59:60Z`,
 * when no leap second occurred, while accepting `1998-12-31T23:59:60Z`.
 *
 * @param table The table.
 * @param date A UTC calendar date.
 * @param out Receives true when a second was inserted at the end of that
 *   day, false otherwise.
 * @return GCHRON_OK; GCHRON_ERR_RANGE before the table begins;
 *   GCHRON_ERR_EXPIRED past its expiry; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_leap_is_leap_day(const GCHRON_LeapTable * table,
    const GCHRON_Date * date, bool * out);

/**
 * @brief Convert a UTC instant to TAI.
 *
 * @param table The table.
 * @param utc The instant.
 * @param leap_second Whether @p utc is the *repeated* second - the one a
 *   timestamp spelled `:60`. A GCHRON_Instant cannot represent it on its own,
 *   because Unix time gives both seconds of a leap the same number; this is
 *   the bit that tells them apart, and it is what
 *   GCHRON_ParseInfo::leap_second carries. Passing true on a day that has no
 *   leap second is GCHRON_ERR_INVALID.
 * @param out Receives the TAI instant.
 * @return GCHRON_OK; GCHRON_ERR_GAP when a *negative* leap second removed
 *   this second, so that it names no instant - the same answer this library
 *   gives for a civil time inside a daylight-saving gap, and for the same
 *   reason; GCHRON_ERR_RANGE; GCHRON_ERR_EXPIRED; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_tai_from_instant(
    const GCHRON_LeapTable * table, GCHRON_Instant utc, bool leap_second,
    GCHRON_TaiInstant * out);

/**
 * @brief Convert a TAI instant to UTC.
 *
 * The inverse of gchron_tai_from_instant(), and **lossy in one direction
 * only**: two distinct TAI instants map to the same GCHRON_Instant across a
 * positive leap second, which is why @p leap_second exists to tell you which
 * of the two you had.
 *
 * @param table The table.
 * @param tai The instant.
 * @param out Receives the UTC instant.
 * @param leap_second Receives true when @p tai fell inside an inserted
 *   second, so that the caller can spell it `:60`. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_RANGE; GCHRON_ERR_EXPIRED;
 *   GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_tai_to_instant(const GCHRON_LeapTable * table,
    GCHRON_TaiInstant tai, GCHRON_Instant * out, bool * leap_second);

/**
 * @brief The true elapsed seconds between two UTC instants.
 *
 * `to - from`, counting the leap seconds in between - which is what every
 * other difference in this library deliberately does not do. Across
 * 2016-12-31 the answer is one second larger than
 * gchron_instant_difference() gives, and both are correct answers to
 * different questions.
 *
 * @param table The table.
 * @param from The earlier instant.
 * @param to The later instant.
 * @param out Receives the elapsed time.
 * @return GCHRON_OK; GCHRON_ERR_RANGE; GCHRON_ERR_EXPIRED;
 *   GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_leap_elapsed(const GCHRON_LeapTable * table,
    GCHRON_Instant from, GCHRON_Instant to, GCHRON_Duration * out);

/**
 * @brief Print what a table holds and where it came from.
 *
 * For the same reason gchron_zonedb_dump() exists: a fallback that cannot be
 * seen is the defect `CONVENTIONS.md` section 1 names.
 *
 * @param table The table. NULL prints a line saying so.
 * @param stream Where to write. NULL means `stdout`.
 */
GCHRON_API void gchron_leap_table_dump(const GCHRON_LeapTable * table,
    FILE * stream);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_LEAP_H
