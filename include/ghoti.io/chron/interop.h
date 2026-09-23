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
 * The encodings other systems use for a timestamp, converted by name.
 *
 * Tier 1 (design.md section 3). One function pair per foreign encoding, and
 * **the defect in each encoding written in the header beside it** - because
 * mistake M17 is that Excel's 1900 leap year, NTP's 2036 era, DOS's local
 * time and FILETIME's 1601 epoch get rediscovered by each program that meets
 * them, and each one gets them slightly wrong on its own.
 *
 * `compress` writes gzip `MTIME` and DOS date-time fields by hand today;
 * `image` reads PNG `tIME` and EXIF `DateTime`. These are the functions those
 * two stop hand-rolling.
 *
 * Every conversion *to* a narrower encoding returns GCHRON_ERR_RANGE when the
 * value does not fit, and none rounds a sub-unit fraction without being
 * asked.
 */

#ifndef GHOTI_IO_GCHRON_INTEROP_H
#define GHOTI_IO_GCHRON_INTEROP_H

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------------*
 * The C library
 *--------------------------------------------------------------------------*/

/**
 * @brief An instant from a `time_t`.
 *
 * @param value Seconds since 1970, as the platform's `time_t` holds them.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_from_time_t(time_t value,
    GCHRON_Instant * out);

/**
 * @brief A `time_t` from an instant.
 *
 * **`time_t` is 32 bits on some ABIs**, and the conversion refuses a value
 * that would not fit rather than handing back a wrapped one - which is
 * mistake M6, and is the 2038 problem in the one place a 64-bit library can
 * still meet it.
 *
 * The sub-second part is discarded, because `time_t` has none.
 *
 * @param i A valid instant.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_RANGE when it will not fit;
 *   GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_time_t(const GCHRON_Instant * i,
    time_t * out);

/**
 * @brief An instant from a `struct timespec`.
 *
 * The same shape as GCHRON_Instant: seconds plus a nanosecond remainder in
 * `[0, 1e9)`, with the sign in the seconds.
 *
 * @param value The timespec.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID when `tv_nsec` is out of range.
 */
GCHRON_API GCHRON_Result gchron_interop_from_timespec(
    const struct timespec * value, GCHRON_Instant * out);

/**
 * @brief A `struct timespec` from an instant.
 *
 * @param i A valid instant.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_RANGE when `time_t` will not hold it;
 *   GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_timespec(const GCHRON_Instant * i,
    struct timespec * out);

/**
 * @brief A civil date-time from a `struct tm`.
 *
 * **`tm_mon` is 0-based and `tm_year` is 1900-based**, which is mistake M7 in
 * its original form. This function does the shifting so that no caller has to
 * remember which way; `tm_wday`, `tm_yday` and `tm_isdst` are ignored on
 * input, because they are derived and a `struct tm` filled in by hand usually
 * has them wrong.
 *
 * @param value The `struct tm`.
 * @param out Receives the date-time on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID for fields that are not a real date;
 *   GCHRON_ERR_RANGE for a year outside the supported range.
 */
GCHRON_API GCHRON_Result gchron_interop_from_tm(const struct tm * value,
    GCHRON_DateTime * out);

/**
 * @brief A `struct tm` from a civil date-time.
 *
 * `tm_wday` and `tm_yday` are filled in, because they are derivable and a
 * consumer will read them. `tm_isdst` is set to -1, meaning "not known" -
 * this is a *civil* reading with no zone, and claiming to know would be
 * mistake M4 exactly.
 *
 * **`tm_gmtoff` is not portable.** Where the platform has it, @p offset_sec
 * fills it in; where it does not, the offset is dropped and there is nowhere
 * to say so. A caller who needs the offset to survive should not be using
 * `struct tm`.
 *
 * @param dt A valid civil date-time.
 * @param offset_sec The offset to record in `tm_gmtoff` where it exists.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_tm(const GCHRON_DateTime * dt,
    int32_t offset_sec, struct tm * out);

/*--------------------------------------------------------------------------*
 * Windows
 *--------------------------------------------------------------------------*/

/**
 * @brief An instant from a Windows `FILETIME`, as a single 64-bit value.
 *
 * **The epoch is 1601-01-01, not 1970**, and the unit is 100 nanoseconds.
 * Taken as a `uint64_t` rather than the struct so that this header does not
 * need `windows.h`; a caller on Windows combines `dwHighDateTime` and
 * `dwLowDateTime` itself, which is one shift they were going to write anyway.
 *
 * @param value Hundreds of nanoseconds since 1601-01-01, UTC.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_interop_from_filetime(uint64_t value,
    GCHRON_Instant * out);

/**
 * @brief A Windows `FILETIME` from an instant.
 *
 * @param i A valid instant.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_RANGE before 1601 or past what 64 bits of
 *   hundred-nanoseconds hold; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_filetime(const GCHRON_Instant * i,
    uint64_t * out);

/**
 * @brief An instant from .NET ticks.
 *
 * The epoch is 0001-01-01 proleptic Gregorian and the unit is 100
 * nanoseconds.
 *
 * @param value Ticks.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_interop_from_dotnet_ticks(int64_t value,
    GCHRON_Instant * out);

/**
 * @brief .NET ticks from an instant.
 *
 * @param i A valid instant.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_interop_to_dotnet_ticks(
    const GCHRON_Instant * i, int64_t * out);

/*--------------------------------------------------------------------------*
 * Network and file formats
 *--------------------------------------------------------------------------*/

/**
 * @brief An instant from an NTP timestamp.
 *
 * **NTP's seconds field is 32 bits and era 0 ends on 2036-02-07.** After that
 * the field wraps, and which era a timestamp belongs to is information the
 * timestamp does not carry - so this function takes the era rather than
 * guessing at it. Era 0 is 1900-2036, era 1 is 2036-2172.
 *
 * @param value Seconds in the high 32 bits, a 2^-32-second fraction in the
 *   low 32.
 * @param era Which 136-year era the value belongs to.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_interop_from_ntp(uint64_t value, int32_t era,
    GCHRON_Instant * out);

/**
 * @brief An NTP timestamp from an instant.
 *
 * @param i A valid instant.
 * @param out_value Receives the timestamp. May be NULL.
 * @param out_era Receives which era it belongs to, so that a caller writing
 *   it can record which. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_RANGE before 1900; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_ntp(const GCHRON_Instant * i,
    uint64_t * out_value, int32_t * out_era);

/**
 * @brief A date from an Excel serial number, 1900 date system.
 *
 * **Excel believes 1900 was a leap year.** It was not; the bug is Lotus
 * 1-2-3's, kept for compatibility, and it means serial 60 is "1900-02-29" -
 * a day that did not occur. This function **refuses serial 60** and offsets
 * serials 61 and above by one day, so that every other serial converts to the
 * date Excel shows.
 *
 * @param serial The serial number. Its fractional part is the time of day.
 * @param out Receives the date-time on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID for serial 60, or a negative serial;
 *   GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_interop_from_excel_1900(double serial,
    GCHRON_DateTime * out);

/**
 * @brief An Excel serial number from a date, 1900 date system.
 *
 * @param dt A valid civil date-time on or after 1900-03-01. Earlier dates
 *   fall on the wrong side of Excel's phantom leap day and are
 *   GCHRON_ERR_UNSUPPORTED rather than silently off by one.
 * @param out Receives the serial on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_UNSUPPORTED or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_excel_1900(
    const GCHRON_DateTime * dt, double * out);

/**
 * @brief A date from an Excel serial number, 1904 date system.
 *
 * The system old Macintosh Excel used, whose epoch is 1904-01-01 and which
 * has no phantom leap day. A workbook says which system it uses, and reading
 * one with the wrong function is wrong by four years and a day.
 *
 * @param serial The serial number.
 * @param out Receives the date-time on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_interop_from_excel_1904(double serial,
    GCHRON_DateTime * out);

/**
 * @brief An Excel serial number from a date, 1904 date system.
 *
 * @param dt A valid civil date-time.
 * @param out Receives the serial on success; untouched on failure.
 * @return GCHRON_OK or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_excel_1904(
    const GCHRON_DateTime * dt, double * out);

/**
 * @brief A civil date-time from a DOS date and time pair.
 *
 * What a zip archive stores. **DOS timestamps are local time with no zone**,
 * so this produces a GCHRON_DateTime and not an instant - a caller who wants
 * an instant supplies the zone, because the archive does not have one. The
 * epoch is 1980 and the resolution is two seconds.
 *
 * @param date The date word: year-1980 in bits 9-15, month in 5-8, day in
 *   0-4.
 * @param time The time word: hour in bits 11-15, minute in 5-10, and the
 *   second *divided by two* in 0-4.
 * @param out Receives the date-time on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID for fields that are not a date.
 */
GCHRON_API GCHRON_Result gchron_interop_from_dos(uint16_t date, uint16_t time,
    GCHRON_DateTime * out);

/**
 * @brief A DOS date and time pair from a civil date-time.
 *
 * The seconds are truncated to an even number, because the format has one bit
 * fewer than a second needs. Truncated rather than rounded: rounding 23:59:59
 * up would carry into the next day.
 *
 * @param dt A valid civil date-time between 1980 and 2107.
 * @param out_date Receives the date word. May be NULL.
 * @param out_time Receives the time word. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_RANGE outside 1980-2107;
 *   GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_dos(const GCHRON_DateTime * dt,
    uint16_t * out_date, uint16_t * out_time);

/**
 * @brief An instant from a gzip `MTIME` field.
 *
 * **Zero means "not available", not the epoch.** RFC 1952 says so, and a
 * decompressor that wrote 1970-01-01 into a file's timestamp because the
 * archive did not carry one is the defect this exists to prevent.
 *
 * @param value The field, as gzip stores it.
 * @param out_instant Receives the instant when there is one; untouched
 *   otherwise.
 * @param out_present Receives whether the field carried a time at all. May
 *   be NULL, and then a zero field is GCHRON_ERR_UNSUPPORTED - so a caller
 *   who does not check cannot silently get the epoch.
 * @return GCHRON_OK; GCHRON_ERR_UNSUPPORTED for an absent time when @p
 *   out_present is NULL; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_from_gzip_mtime(uint32_t value,
    GCHRON_Instant * out_instant, bool * out_present);

/**
 * @brief A gzip `MTIME` field from an instant.
 *
 * @param i A valid instant, or NULL to write "not available" (zero).
 * @param out Receives the field on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_RANGE outside 1970-2106; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_gzip_mtime(const GCHRON_Instant * i,
    uint32_t * out);

/**
 * @brief A civil date-time from a PNG `tIME` chunk's seven bytes.
 *
 * `tIME` is UTC, and the year is two bytes big-endian.
 *
 * @param bytes The seven bytes, in chunk order.
 * @param out Receives the date-time on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID for fields that are not a date.
 */
GCHRON_API GCHRON_Result gchron_interop_from_png_time(const uint8_t bytes[7],
    GCHRON_DateTime * out);

/**
 * @brief A PNG `tIME` chunk's seven bytes from a civil date-time.
 *
 * @param dt A valid civil date-time whose year is 0..65535.
 * @param out Receives the seven bytes on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_RANGE or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_png_time(
    const GCHRON_DateTime * dt, uint8_t out[7]);

/** Bytes an EXIF `DateTime` field occupies, the terminating NUL included. */
#define GCHRON_EXIF_DATETIME_BYTES 20

/**
 * @brief A civil date-time from an EXIF `DateTime` string.
 *
 * `YYYY:MM:DD HH:MM:SS` - **with colons in the date**, which is the thing
 * everybody's first EXIF parser gets wrong. Nineteen characters and a NUL.
 *
 * EXIF carries no zone. The `OffsetTime` tags of EXIF 2.31 carry one
 * separately, and are the caller's to read and apply.
 *
 * A field of all spaces or all NULs means "unknown", which many cameras write,
 * and is GCHRON_ERR_UNSUPPORTED rather than a date.
 *
 * @param text The field. Not assumed to be NUL-terminated.
 * @param len Bytes at @p text; 19 or 20.
 * @param out Receives the date-time on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_FORMAT; GCHRON_ERR_UNSUPPORTED for an
 *   "unknown" field; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_from_exif(const char * text,
    size_t len, GCHRON_DateTime * out);

/**
 * @brief An EXIF `DateTime` string from a civil date-time.
 *
 * @param dt A valid civil date-time whose year is 0..9999.
 * @param out Receives GCHRON_EXIF_DATETIME_BYTES bytes, NUL-terminated.
 * @return GCHRON_OK, GCHRON_ERR_RANGE or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_exif(const GCHRON_DateTime * dt,
    char out[GCHRON_EXIF_DATETIME_BYTES]);

/**
 * @brief An instant from a `struct timeval`.
 *
 * Beside gchron_interop_from_timespec(), and microseconds rather than
 * nanoseconds. The fields are taken separately rather than the struct,
 * because `struct timeval` lives in `<sys/time.h>` on POSIX and in
 * `<winsock2.h>` on Windows, and neither belongs in this header for one
 * conversion. Pass `tv.tv_sec` and `tv.tv_usec`.
 *
 * @param tv_sec Seconds since 1970.
 * @param tv_usec Microseconds, 0..999999. Anything else is
 *   GCHRON_ERR_INVALID: a `timeval` with an unnormalised field is a bug in
 *   whatever produced it, and guessing which way it meant to carry would
 *   hide it.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_interop_from_timeval(time_t tv_sec,
    int32_t tv_usec, GCHRON_Instant * out);

/**
 * @brief The fields of a `struct timeval` from an instant.
 *
 * **Lossy in the microsecond direction, and it floors.** An instant holds
 * nanoseconds and a `timeval` holds microseconds, so 1.9999 microseconds
 * past a second becomes 1 - the same direction gchron_instant_to_unix_micros()
 * takes, so that the two cannot disagree about the same instant. A caller who
 * wants a different rounding has gchron_instant_round() to say so with.
 *
 * @param i A valid instant.
 * @param out_sec Receives the seconds. May be NULL.
 * @param out_usec Receives 0..999999. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_RANGE when the seconds do not fit this
 *   platform's `time_t`; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_timeval(const GCHRON_Instant * i,
    time_t * out_sec, int32_t * out_usec);

/*--------------------------------------------------------------------------*
 * Windows SYSTEMTIME
 *--------------------------------------------------------------------------*/

/**
 * @brief A civil date-time from the fields of a Windows `SYSTEMTIME`.
 *
 * Taken as fields for the reason gchron_interop_from_filetime() takes a
 * `uint64_t`: the struct is Windows', and this library builds and is tested
 * everywhere.
 *
 * **`wDayOfWeek` is not a parameter.** Windows documents it as output only,
 * a caller filling a `SYSTEMTIME` in by hand may well have it wrong, and the
 * right behaviour is to compute the day of the week from the date rather
 * than believe a field that contradicts it. gchron_date_day_of_week() is how
 * to ask, and it numbers Monday 1 where Windows numbers Sunday 0.
 *
 * @param year `wYear`.
 * @param month `wMonth`, 1..12.
 * @param day `wDay`, 1..31.
 * @param hour `wHour`, 0..23.
 * @param minute `wMinute`, 0..59.
 * @param second `wSecond`, 0..59. Windows never writes 60.
 * @param milliseconds `wMilliseconds`, 0..999.
 * @param out Receives the date-time on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID for fields that are not a date.
 */
GCHRON_API GCHRON_Result gchron_interop_from_systemtime(uint16_t year,
    uint16_t month, uint16_t day, uint16_t hour, uint16_t minute,
    uint16_t second, uint16_t milliseconds, GCHRON_DateTime * out);

/**
 * @brief The fields of a Windows `SYSTEMTIME` from a civil date-time.
 *
 * `wDayOfWeek` is computed here rather than taken, and is Sunday-0 as Windows
 * numbers it - the one place in this library that numbering appears, for the
 * reason design.md gives at mistake M7.
 *
 * The sub-second part is milliseconds, so it is lossy and **floors**, like
 * gchron_interop_to_timeval().
 *
 * @param dt A valid civil date-time whose year is 1601..30827, which is what
 *   the field holds.
 * @param out_year Receives `wYear`. May be NULL, as may any of the rest.
 * @param out_month Receives `wMonth`.
 * @param out_day_of_week Receives `wDayOfWeek`, 0..6 with Sunday 0.
 * @param out_day Receives `wDay`.
 * @param out_hour Receives `wHour`.
 * @param out_minute Receives `wMinute`.
 * @param out_second Receives `wSecond`.
 * @param out_milliseconds Receives `wMilliseconds`.
 * @return GCHRON_OK, GCHRON_ERR_RANGE or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_systemtime(
    const GCHRON_DateTime * dt, uint16_t * out_year, uint16_t * out_month,
    uint16_t * out_day_of_week, uint16_t * out_day, uint16_t * out_hour,
    uint16_t * out_minute, uint16_t * out_second,
    uint16_t * out_milliseconds);

/*--------------------------------------------------------------------------*
 * ASN.1 - X.509, CMS, LDAP
 *--------------------------------------------------------------------------*/

/** Bytes gchron_interop_to_asn1_utctime() writes, NUL included. */
#define GCHRON_ASN1_UTCTIME_BYTES 14

/** Bytes gchron_interop_to_asn1_gentime() may write, NUL included. */
#define GCHRON_ASN1_GENTIME_BYTES 26

/**
 * @brief The pivot RFC 5280 section 4.1.2.5.1 gives for a two-digit year.
 *
 * Years below it are 20xx and years at or above it are 19xx, so 50 means
 * 1950..2049.
 */
#define GCHRON_ASN1_UTCTIME_PIVOT_RFC5280 50

/**
 * @brief An instant from an ASN.1 `UTCTime`.
 *
 * `YYMMDDHHMMSSZ` - the encoding X.509 certificates carry for any validity
 * date before 2050, and the one that made a certificate expiring in 2050 a
 * different kind of problem from one expiring in 2049.
 *
 * **The pivot is a parameter.** RFC 5280 fixes it at 50, and
 * ::GCHRON_ASN1_UTCTIME_PIVOT_RFC5280 is that value, but the same encoding
 * appears elsewhere under other rules and a library that guesses a century
 * for its caller is making mistake M18 on their behalf. Pass the constant
 * unless you know you need something else.
 *
 * Read accepts the looser BER spellings that real certificates carry: the
 * seconds may be absent, and the zone may be `Z` or `+HHMM`/`-HHMM` rather
 * than only `Z`. Write is DER-strict.
 *
 * @param text The field. Not assumed to be NUL-terminated.
 * @param len Bytes at @p text.
 * @param pivot The two-digit year at which the century changes, 0..99.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_FORMAT; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_from_asn1_utctime(const char * text,
    size_t len, int pivot, GCHRON_Instant * out);

/**
 * @brief An ASN.1 `UTCTime` from an instant, DER-strict.
 *
 * Always `YYMMDDHHMMSSZ`: UTC, seconds present, no fraction - which is what
 * DER requires and what X.509 therefore carries.
 *
 * @param i A valid instant whose UTC year is within the pivot's window.
 * @param pivot The pivot the reader will use, so that what is written can be
 *   read back as the same year.
 * @param out Receives GCHRON_ASN1_UTCTIME_BYTES bytes, NUL-terminated.
 * @return GCHRON_OK; GCHRON_ERR_RANGE when the year is outside the hundred
 *   years @p pivot selects, rather than a silently wrong century;
 *   GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_asn1_utctime(
    const GCHRON_Instant * i, int pivot,
    char out[GCHRON_ASN1_UTCTIME_BYTES]);

/**
 * @brief An instant from an ASN.1 `GeneralizedTime`.
 *
 * `YYYYMMDDHHMMSS[.fff]Z`, with a four-digit year and therefore no pivot to
 * get wrong. What X.509 uses for any date from 2050 on.
 *
 * Read accepts BER: the fraction may be introduced by `.` or `,` (both are
 * legal in BER, and DER allows only `.`), the seconds may be absent, and the
 * zone may be an offset rather than `Z`.
 *
 * @param text The field. Not assumed to be NUL-terminated.
 * @param len Bytes at @p text.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_FORMAT; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_from_asn1_gentime(const char * text,
    size_t len, GCHRON_Instant * out);

/**
 * @brief An ASN.1 `GeneralizedTime` from an instant, DER-strict.
 *
 * DER restricts what BER allows, and the restrictions are the ones two
 * implementations most often differ on: the zone is `Z` and nothing else,
 * the seconds are always present, a fraction never ends in a zero, and a
 * fraction that would be zero is omitted along with its point. So one second
 * past midnight is `YYYYMMDD000001Z` and never `...000001.000Z`.
 *
 * @param i A valid instant whose UTC year is 0..9999.
 * @param out Receives at most GCHRON_ASN1_GENTIME_BYTES bytes,
 *   NUL-terminated.
 * @return GCHRON_OK, GCHRON_ERR_RANGE or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_asn1_gentime(
    const GCHRON_Instant * i, char out[GCHRON_ASN1_GENTIME_BYTES]);

/*--------------------------------------------------------------------------*
 * Other epochs
 *--------------------------------------------------------------------------*/

/**
 * @brief An instant from Apple's Cocoa reference date.
 *
 * `NSDate.timeIntervalSinceReferenceDate`: seconds since 2001-01-01, as a
 * `double`.
 *
 * **Lossy**, in the same way every `double` of seconds is: a `double` has 53
 * bits of mantissa, so a value near today resolves to about a microsecond and
 * a round trip through it drifts by that much. That is a property of the
 * encoding rather than of this conversion; gchron_instant_as_double() says
 * the same thing about the same limit.
 *
 * @param value Seconds since 2001-01-01T00:00:00Z.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_interop_from_cocoa(double value,
    GCHRON_Instant * out);

/**
 * @brief Apple's Cocoa reference interval from an instant.
 *
 * @param i A valid instant.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_cocoa(const GCHRON_Instant * i,
    double * out);

/**
 * @brief An instant from an HFS+ timestamp.
 *
 * Seconds since 1904-01-01. **Classic HFS stored local time and HFS+ stores
 * UTC**, and the on-disk field does not say which - so a caller reading a
 * classic volume has a civil reading rather than an instant, and this
 * function is for the HFS+ case.
 *
 * @param value Seconds since 1904-01-01T00:00:00Z.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_from_hfs_plus(uint32_t value,
    GCHRON_Instant * out);

/**
 * @brief An HFS+ timestamp from an instant.
 *
 * @param i A valid instant.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_RANGE or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_hfs_plus(const GCHRON_Instant * i,
    uint32_t * out);

/**
 * @brief An instant from a Modified Julian Date.
 *
 * MJD is JD - 2,400,000.5, so it begins at midnight rather than noon.
 *
 * **Lossy**: a Modified Julian Date near today spends sixteen of a `double`'s
 * fifty-three mantissa bits on the day number, leaving about 1.3 microseconds
 * of resolution in the fraction. A caller who needs the nanoseconds wants
 * gchron_epoch_day_to_jdn() and the time of day separately.
 *
 * @param value The Modified Julian Date.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_interop_from_mjd(double value,
    GCHRON_Instant * out);

/**
 * @brief A Modified Julian Date from an instant.
 *
 * @param i A valid instant.
 * @param out Receives the value on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_interop_to_mjd(const GCHRON_Instant * i,
    double * out);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_INTEROP_H
