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
 * What this build of the library is.
 *
 * The numbers come from libver_gen.h, which the Makefile writes, so that the
 * version the library reports is the one that names its .pc file, its install
 * directory and its soname. Writing them out here instead is how four
 * libraries in this suite came to report 0.0.0 through one artifact and
 * something else through another (CONVENTIONS.md section 4).
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
