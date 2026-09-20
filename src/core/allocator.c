/**
 * @file
 *
 * The default allocator, which is cutil's.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/allocator.h>
#include <ghoti.io/chron/macros.h>

const GCHRON_Allocator * gchron_allocator_default(void) {
  return gcu_allocator_default();
}
