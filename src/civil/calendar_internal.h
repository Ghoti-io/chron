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
 *
 * Copyright 2026 by Corey Pennycuff
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
