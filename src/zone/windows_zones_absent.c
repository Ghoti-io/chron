/**
 * @file
 *
 * What stands in for the Windows zone mapping when it has not been generated.
 *
 * This file is compiled **only** when `src/zone/windows_zones.c` does not
 * exist. That file comes from CLDR's `windowsZones.xml`, which
 * `tools/tzdata/fetch-cldr.sh` downloads and
 * `tools/tzdata/windows_zones.py` turns into a table; nothing in the build
 * reaches the network, so a fresh clone has neither.
 *
 * The functions report **no table**, not an empty one. `gchron_windows_zones_count()`
 * returning zero is what the Windows branch of `gchron_zonedb_local()` checks
 * in order to answer GCHRON_ERR_UNSUPPORTED with a reason a caller can act on
 * - rather than "unknown zone", which is what an empty mapping would say and
 * which is also what a *complete* mapping says about a zone name Windows
 * added after it was generated. Those are different problems with different
 * fixes, and a caller that cannot tell them apart cannot fix either.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/macros.h>
#include <stddef.h>

#include "zone_internal.h"

const char * gchron_windows_zones_version(void) {
  return NULL;
}

size_t gchron_windows_zones_count(void) {
  return 0;
}

const GCHRON_WindowsZone * gchron_windows_zones_at(size_t index) {
  (void)index;
  return NULL;
}

const char * gchron_windows_zones_lookup(const char * windows_name) {
  (void)windows_name;
  return NULL;
}
