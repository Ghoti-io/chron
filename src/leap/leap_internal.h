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
 * What leap.c and tai.c share.
 */

#ifndef GHOTI_IO_GCHRON_SRC_LEAP_LEAP_INTERNAL_H
#define GHOTI_IO_GCHRON_SRC_LEAP_LEAP_INTERNAL_H

#include <ghoti.io/chron/allocator.h>
#include <ghoti.io/chron/leap.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Seconds between the NTP epoch (1900-01-01) and the Unix epoch (1970-01-01).
 *
 * Seventy years of which seventeen were leap years: 70*365 + 17 = 25567 days.
 * `leap-seconds.list` timestamps are NTP, and every one of them has to come
 * through here.
 */
#define GCHRON_NTP_TO_UNIX INT64_C(2208988800)

/** A parsed table. Immutable once gchron_leap_table_parse() returns. */
struct GCHRON_LeapTable {
  const GCHRON_Allocator * allocator; /**< NULL for the builtin table. */
  const GCHRON_LeapEntry * entries;   /**< In time order. */
  size_t count;
  int64_t expiry_sec;                 /**< Unix seconds. */
  int64_t updated_sec;                /**< Unix seconds. */
  bool is_builtin;                    /**< Then never freed, and not owned. */
};

/**
 * Find the row in force at a Unix instant.
 *
 * @param table The table.
 * @param unix_sec The instant.
 * @return The index of the last row whose `at` is at or before @p unix_sec,
 *   or `SIZE_MAX` when @p unix_sec precedes the first row.
 */
size_t gchron_leap_index_at(const GCHRON_LeapTable * table, int64_t unix_sec);

/**
 * The range check every public entry point in this module begins with.
 *
 * @param table The table. NULL is GCHRON_ERR_INVALID.
 * @param unix_sec The instant being asked about.
 * @return GCHRON_OK, GCHRON_ERR_INVALID, GCHRON_ERR_RANGE (before the table
 *   begins) or GCHRON_ERR_EXPIRED (past its expiry).
 */
GCHRON_Result gchron_leap_check_span(const GCHRON_LeapTable * table,
    int64_t unix_sec);

#endif // GHOTI_IO_GCHRON_SRC_LEAP_LEAP_INTERNAL_H
