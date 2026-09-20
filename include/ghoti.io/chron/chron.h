/**
 * @file
 *
 * Umbrella header for the Ghoti.io Chron library.
 *
 * Including this pulls in every tier. A consumer that wants only civil
 * arithmetic, or only the tier-1 text grammars, includes those headers
 * instead and links nothing it does not use (design.md section 3).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_CHRON_H
#define GHOTI_IO_GCHRON_CHRON_H

#include <ghoti.io/chron/allocator.h>
#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/offset.h>
#include <ghoti.io/chron/parse.h>

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
