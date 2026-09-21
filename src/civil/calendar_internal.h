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
 * How the run-time calendars are freed. Private; installed with nothing.
 *
 * gchron_calendar_destroy() is one public function over two kinds of
 * calendar, so it needs to know which it has. A tag in the struct would be
 * visible to every caller and would have to be maintained by anybody
 * implementing a GCHRON_Calendar of their own; a private pair of destructors
 * and a check on the function pointers keeps it out of the public type.
 */

#ifndef GHOTI_IO_GCHRON_SRC_CIVIL_CALENDAR_INTERNAL_H
#define GHOTI_IO_GCHRON_SRC_CIVIL_CALENDAR_INTERNAL_H

#include <ghoti.io/chron/calendar.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Free a calendar built by gchron_calendar_hybrid(). */
void gchron_calendar_hybrid_free(GCHRON_Calendar * calendar);

/** Free a calendar built by gchron_calendar_tabular(). */
void gchron_calendar_tabular_free(GCHRON_Calendar * calendar);

/** Whether this calendar came from gchron_calendar_hybrid(). */
bool gchron_calendar_is_hybrid(const GCHRON_Calendar * calendar);

/** Whether this calendar came from gchron_calendar_tabular(). */
bool gchron_calendar_is_tabular(const GCHRON_Calendar * calendar);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_SRC_CIVIL_CALENDAR_INTERNAL_H
