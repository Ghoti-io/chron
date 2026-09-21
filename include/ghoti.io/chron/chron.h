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
 * Umbrella header for the Ghoti.io Chron library.
 *
 * Including this pulls in every tier. A consumer that wants only civil
 * arithmetic, or only the tier-1 text grammars, includes those headers
 * instead and links nothing it does not use (design.md section 3).
 */

#ifndef GHOTI_IO_GCHRON_CHRON_H
#define GHOTI_IO_GCHRON_CHRON_H

#include <ghoti.io/chron/allocator.h>
#include <ghoti.io/chron/calendar.h>
#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/format.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/offset.h>
#include <ghoti.io/chron/clock.h>
#include <ghoti.io/chron/interop.h>
#include <ghoti.io/chron/leap.h>
#include <ghoti.io/chron/parse.h>
#include <ghoti.io/chron/zone.h>
#include <ghoti.io/chron/zoned.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief This build's version, as a string.
 *
 * @return A static string such as `"0.0.0"`, never NULL.
 */
GCHRON_API const char * gchron_version_string(void);

/**
 * @brief This build's major version.
 *
 * @return The major version number.
 */
GCHRON_API int gchron_version_major(void);

/**
 * @brief This build's minor version.
 *
 * @return The minor version number.
 */
GCHRON_API int gchron_version_minor(void);

/**
 * @brief This build's patch version.
 *
 * @return The patch version number.
 */
GCHRON_API int gchron_version_patch(void);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_CHRON_H
