/**
 * @file
 *
 * The proleptic Gregorian calendar, shared between the civil translation
 * units and installed with none of them.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_SRC_CIVIL_CIVIL_INTERNAL_H
#define GHOTI_IO_GCHRON_SRC_CIVIL_CIVIL_INTERNAL_H

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The epoch day of GCHRON_YEAR_MIN-01-01.
 *
 * A literal rather than a computation because it bounds the input to
 * gchron_gregory_from_epoch_day(), which cannot itself compute it without
 * first accepting an out-of-range day. tests/unit/test_civil.cpp asserts that
 * it is what gchron_gregory_to_epoch_day() gives for that date, so the two
 * cannot drift apart.
 */
#define GCHRON_EPOCH_DAY_MIN INT64_C(-365243219162)

/** The epoch day of GCHRON_YEAR_MAX-12-31. See GCHRON_EPOCH_DAY_MIN. */
#define GCHRON_EPOCH_DAY_MAX INT64_C(365241780471)

/** Days from the Rata Die epoch to the Unix epoch: 0001-01-01 is R.D. 1. */
#define GCHRON_RD_OFFSET INT64_C(719163)

/** Days from the Julian Day Number epoch to the Unix epoch. */
#define GCHRON_JDN_OFFSET INT64_C(2440588)

/**
 * Whether a year is a leap year, with no range check.
 *
 * @param year The astronomical year; the caller has already bounded it.
 * @return `true` for a leap year.
 */
bool gchron_gregory_is_leap(int64_t year);

/**
 * How long a month is, with no range check.
 *
 * @param year The astronomical year, which February needs.
 * @param month 1..12; the caller has already bounded it.
 * @return 28..31.
 */
int gchron_gregory_days_in_month(int64_t year, int month);

/**
 * Days since 1970-01-01, with no validity check.
 *
 * Howard Hinnant's `days_from_civil`, in 64-bit arithmetic. Valid for every
 * `(year, month, day)` this library accepts; the intermediate `era * 146097`
 * reaches about 3.7e11 at the edge of the supported range, against an
 * `int64_t` ceiling of 9.2e18, which is the guard band design.md section 3.2
 * bought by capping the year at nine digits.
 *
 * @param year The astronomical year.
 * @param month 1..12.
 * @param day 1..the length of that month.
 * @return Days since 1970-01-01.
 */
int64_t gchron_gregory_to_epoch_day(int64_t year, int month, int day);

/**
 * The date an epoch day names.
 *
 * Hinnant's `civil_from_days`, the exact inverse of
 * gchron_gregory_to_epoch_day().
 *
 * @param epoch_day Days since 1970-01-01, within
 *   GCHRON_EPOCH_DAY_MIN..GCHRON_EPOCH_DAY_MAX; the caller has already
 *   bounded it.
 * @param out_year Receives the astronomical year.
 * @param out_month Receives 1..12.
 * @param out_day Receives 1..31.
 */
void gchron_gregory_from_epoch_day(int64_t epoch_day, int64_t * out_year,
    int * out_month, int * out_day);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_SRC_CIVIL_CIVIL_INTERNAL_H
