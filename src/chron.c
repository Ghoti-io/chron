/**
 * @file
 *
 * What this build of the library is.
 *
 * The numbers come from libver_gen.h, which the Makefile writes, so that the
 * version the library reports is the one that names its .pc file, its install
 * directory and its soname. Writing them out here instead is how four
 * libraries in this suite came to report 0.0.0 through one artifact and
 * something else through another (CONVENTIONS.md section 4).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/chron.h>
#include <ghoti.io/chron/macros.h>

const char * gchron_version_string(void) {
  return GCHRON_VERSION_STRING;
}

int gchron_version_major(void) {
  return GCHRON_VERSION_MAJOR;
}

int gchron_version_minor(void) {
  return GCHRON_VERSION_MINOR;
}

int gchron_version_patch(void) {
  return GCHRON_VERSION_PATCH;
}
