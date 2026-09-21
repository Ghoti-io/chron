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
 * Half-open intervals of the timeline.
 *
 * Half-open, `[start, end)`, so that abutting intervals tile the line with no
 * overlap and no gap - which closed intervals cannot do, and which is why a
 * scheduler built on them double-books at every boundary.
 */

#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>

#include "../core/core_internal.h"

GCHRON_Result gchron_interval_create(const GCHRON_Instant * start,
    const GCHRON_Instant * end, GCHRON_Interval * out) {
  if (out == NULL || !gchron_instant_is_valid(start)
      || !gchron_instant_is_valid(end)) {
    return GCHRON_ERR_INVALID;
  }
  if (gchron_instant_compare(start, end) > 0) {
    return GCHRON_ERR_INVALID;
  }
  out->start = *start;
  out->end = *end;
  return GCHRON_OK;
}

bool gchron_interval_contains(const GCHRON_Interval * interval,
    const GCHRON_Instant * i) {
  if (interval == NULL || i == NULL) {
    return false;
  }
  return gchron_instant_compare(&interval->start, i) <= 0
      && gchron_instant_compare(i, &interval->end) < 0;
}

bool gchron_interval_overlaps(const GCHRON_Interval * a,
    const GCHRON_Interval * b) {
  if (a == NULL || b == NULL) {
    return false;
  }
  /* Strict on both sides: abutting intervals do not overlap. */
  return gchron_instant_compare(&a->start, &b->end) < 0
      && gchron_instant_compare(&b->start, &a->end) < 0;
}

GCHRON_Result gchron_interval_duration(const GCHRON_Interval * interval,
    GCHRON_Duration * out) {
  if (interval == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  return gchron_instant_until(&interval->start, &interval->end, out);
}
