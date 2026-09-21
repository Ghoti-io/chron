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
 * The formats that have names.
 *
 * design.md section 8.4: they exist so that the RFC 3339 timestamp in a log
 * line was never typed by hand. Mistake M8 is `YYYY` where `yyyy` was meant -
 * the week-based year, which is right for fifty-one weeks a year and wrong
 * over New Year, and which has taken down a service somewhere every few
 * Decembers since ICU shipped it.
 *
 * Each is compiled once, on first use, into a static table. The compilation
 * cannot fail - these patterns are in this file and are covered by a test
 * that compiles every one of them - so a failure here is
 * GCHRON_ERR_INTERNAL's definition of unreachable.
 */

#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/format.h>
#include <ghoti.io/chron/macros.h>
#include <string.h>

#include <ghoti.io/cutil/mutex.h>

#include "../core/core_internal.h"
#include "format_internal.h"

/** The pattern behind each named format, in enum order. */
static const char * const PATTERNS[GCHRON_NAMED_COUNT] = {
  /* GCHRON_NAMED_RFC3339        */ "uuuu-MM-dd'T'HH:mm:ssXXX",
  /* GCHRON_NAMED_RFC3339_NANOS  */ "uuuu-MM-dd'T'HH:mm:ss.SSSSSSSSSXXX",
  /* GCHRON_NAMED_RFC9557        */ "uuuu-MM-dd'T'HH:mm:ssXXX'['VV']'",
  /* GCHRON_NAMED_ISO8601_BASIC  */ "uuuuMMdd'T'HHmmssXX",
  /* GCHRON_NAMED_ISO_WEEK       */ "YYYY-'W'ww-e",
  /* GCHRON_NAMED_ISO_ORDINAL    */ "uuuu-DDD",
  /* GCHRON_NAMED_HTTP           */ "EEE, dd MMM uuuu HH:mm:ss 'GMT'",
  /* GCHRON_NAMED_RFC5322        */ "EEE, dd MMM uuuu HH:mm:ss Z",
};

static GCHRON_Format FORMATS[GCHRON_NAMED_COUNT];
static bool BUILT = false;
static GCU_MUTEX_T LOCK;
static bool LOCK_READY = false;

/**
 * Build the table once.
 *
 * A constructor rather than a lazy check under a lock in the hot path: the
 * table is immutable once built and read from every thread, and building it
 * before `main` removes the question of what happens when two threads ask
 * first.
 */
GCHRON_INIT_FUNCTION(gchron_named_formats_init) {
  size_t i;

  if (GCU_MUTEX_CREATE(LOCK) == 0) {
    LOCK_READY = true;
  }
  for (i = 0; i < GCHRON_NAMED_COUNT; ++i) {
    GCHRON_Limits limits;
    gchron_limits_default(&limits);
    if (gchron_format_compile_into(PATTERNS[i], strlen(PATTERNS[i]),
            GCHRON_FORMAT_LDML, &limits, gchron_allocator_default(),
            &FORMATS[i], NULL) != GCHRON_OK) {
      /* Unreachable: these patterns are in this file, and
       * tests/unit/test_format.cpp compiles every one of them. Leaving the
       * entry zeroed makes gchron_format_named() return NULL rather than a
       * half-built format. */
      memset(&FORMATS[i], 0, sizeof(FORMATS[i]));
      continue;
    }
    /* Static: gchron_format_destroy() ignores it, so a caller holding
     * "whichever format was chosen" need not remember which kind it is. */
    FORMATS[i].is_static = true;
  }
  BUILT = true;
}

GCHRON_CLEANUP_FUNCTION(gchron_named_formats_cleanup) {
  size_t i;

  for (i = 0; i < GCHRON_NAMED_COUNT; ++i) {
    /* The static flag is what stops the public destructor freeing these, so
     * it has to come off before the library's own cleanup frees them - or
     * Valgrind reports the table as leaked on every run. */
    FORMATS[i].is_static = false;
    gcu_allocator_free(FORMATS[i].allocator, FORMATS[i].items);
    gcu_allocator_free(FORMATS[i].allocator, FORMATS[i].literals);
    memset(&FORMATS[i], 0, sizeof(FORMATS[i]));
  }
  if (LOCK_READY) {
    GCU_MUTEX_DESTROY(LOCK);
    LOCK_READY = false;
  }
  BUILT = false;
}

const GCHRON_Format * gchron_format_named(GCHRON_NamedFormat named) {
  if (named < 0 || named >= GCHRON_NAMED_COUNT) {
    return NULL;
  }
  if (!BUILT) {
    /* A platform whose compiler has no constructor attribute, which
     * GCHRON_INIT_FUNCTION is a no-op on. Building under the lock is the
     * fallback rather than the normal path. */
    if (LOCK_READY) {
      GCU_MUTEX_LOCK(LOCK);
    }
    if (!BUILT) {
      gchron_named_formats_init();
    }
    if (LOCK_READY) {
      GCU_MUTEX_UNLOCK(LOCK);
    }
  }
  if (FORMATS[named].item_count == 0) {
    return NULL;
  }
  return &FORMATS[named];
}
