/**
 * @file
 *
 * The zone this machine is set to, and the listing of a zoneinfo directory.
 *
 * **This file is the only place in the library that reads the environment.**
 * `TZ`, `/etc/localtime`, `/etc/timezone` and, on Windows, the registry are
 * consulted here and nowhere else; everything else takes a zone handle
 * (design.md, mistake M2).
 *
 * Reference: POSIX.1-2024, *Environment Variables*, `TZ`.
 *
 * Copyright 2026 by Corey Pennycuff
 */

/*
 * readlink(), opendir() and stat() are POSIX, and this library compiles with
 * -std=c17, which declares none of them. The feature-test macro goes at the
 * very top, before any header: glibc reads it when the first one is included
 * and ignores it afterwards. The suite spells it this way in cutil's
 * thread.c and in text's yaml/stream.c.
 */
#define _POSIX_C_SOURCE 200809L

#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/zone.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/cutil/safemath.h>

#include "../core/core_internal.h"
#include "zone_internal.h"

#if !defined(_WIN32)
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/*
 * These live in zonedb.c, which owns the struct. Declared here rather than in
 * zone_internal.h because nothing outside this pair of files has any business
 * reaching into a database's cache.
 */
GCHRON_Result gchron_zonedb_intern(GCHRON_ZoneDb * db, const char * key,
    GCHRON_Zone * zone, const GCHRON_Zone ** out);
const char * gchron_zonedb_directory_of(const GCHRON_ZoneDb * db);
const GCHRON_Allocator * gchron_zonedb_allocator_of(const GCHRON_ZoneDb * db);
const GCHRON_Limits * gchron_zonedb_limits_of(const GCHRON_ZoneDb * db);
GCHRON_Result gchron_zonedb_ids_of(GCHRON_ZoneDb * db,
    const char * const ** out_ids, size_t * out_count);

/** The key a cached local zone is filed under; not a legal identifier. */
#define LOCAL_KEY "\x01local"

/**
 * Whether a `TZ` value names a rule rather than a zone.
 *
 * POSIX says a leading colon makes the rest implementation-defined - every
 * Unix reads it as a path - and that without one the value is a rule string.
 * In practice both spellings carry both kinds, because every implementation
 * accepts `TZ=Europe/Paris` with no colon. The discriminator that works is
 * the shape: a rule begins with an alphabetic abbreviation or a `<`, and
 * a zone identifier contains a `/` or names a file that exists.
 */
static bool looks_like_rule(const char * value) {
  GCHRON_PosixTz ignored;
  return gchron_posixtz_parse(value, strlen(value), &ignored) == GCHRON_OK;
}

#if !defined(_WIN32)

/**
 * The identifier `/etc/localtime` points at, if it is a symlink into a
 * zoneinfo tree.
 *
 * The target is something like `/usr/share/zoneinfo/America/New_York`, and
 * the identifier is what follows the directory. Read with `readlink` rather
 * than by resolving the path, so that a relative link - Debian writes
 * `../usr/share/zoneinfo/...` - still gives up its tail.
 */
static char * localtime_link_id(const GCHRON_Allocator * allocator) {
  char buffer[1024];
  ssize_t length;
  const char * marker = "zoneinfo/";
  char * found;
  size_t id_len;
  char * id;

  length = readlink("/etc/localtime", buffer, sizeof(buffer) - 1);
  if (length <= 0) {
    return NULL;
  }
  buffer[length] = '\0';

  found = strstr(buffer, marker);
  if (found == NULL) {
    return NULL;
  }
  found += strlen(marker);
  if (!gchron_zone_id_is_safe(found)) {
    return NULL;
  }
  id_len = strlen(found) + 1;
  id = (char *)gcu_allocator_malloc(allocator, id_len);
  if (id != NULL) {
    memcpy(id, found, id_len);
  }
  return id;
}

/**
 * The identifier `/etc/timezone` names, if that file exists.
 *
 * Debian's way of recording the name when `/etc/localtime` is a copy rather
 * than a symlink - in which case the file itself is a perfectly good zone and
 * only its *name* is missing.
 */
static char * etc_timezone_id(const GCHRON_Allocator * allocator) {
  void * data = NULL;
  size_t len = 0;
  char * text;
  char * end;
  char * id = NULL;

  if (gchron_zone_read_file("/etc/timezone", 1024, allocator, &data, &len)
      != GCHRON_OK) {
    return NULL;
  }
  text = (char *)data;
  end = text;
  while (*end != '\0' && *end != '\n' && *end != '\r') {
    ++end;
  }
  *end = '\0';
  if (gchron_zone_id_is_safe(text)) {
    id = (char *)gcu_allocator_malloc(allocator, strlen(text) + 1);
    if (id != NULL) {
      strcpy(id, text);
    }
  }
  gcu_allocator_free(allocator, data);
  return id;
}

#endif /* !_WIN32 */

GCHRON_Result gchron_zonedb_local(GCHRON_ZoneDb * db,
    const GCHRON_Zone ** out) {
  const GCHRON_Allocator * allocator;
  const char * tz;

  if (db == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  allocator = gchron_zonedb_allocator_of(db);

  /* 1. TZ, if it is set. */
  tz = getenv("TZ");
  if (tz != NULL && tz[0] != '\0') {
    const char * value = (tz[0] == ':') ? tz + 1 : tz;
    GCHRON_Zone * zone = NULL;

    if (value[0] != '\0') {
      /*
       * An identifier is tried first, so that `TZ=EST5EDT` - which is both a
       * real zone file and a valid rule string - resolves to the file, whose
       * historical transitions the rule alone cannot give. Only when no such
       * zone exists is the value read as a rule.
       */
      if (gchron_zone_id_is_safe(value)
          && gchron_zonedb_zone(db, value, out) == GCHRON_OK) {
        return GCHRON_OK;
      }
      if (looks_like_rule(value)
          && gchron_zonedb_posix(db, value, out) == GCHRON_OK) {
        return GCHRON_OK;
      }
      (void)zone;
    }
    /*
     * POSIX: an unparsable TZ means UTC. Following that rather than failing,
     * because every other implementation does and a program whose environment
     * is wrong should still be able to print a timestamp - but the zone it
     * gets is UTC by name, so nothing silently pretends to be local.
     */
    return gchron_zonedb_utc(db, out);
  }

#if defined(_WIN32)
  /*
   * TODO(windows): GetDynamicTimeZoneInformation() gives a Windows zone name,
   * which the CLDR windowsZones.xml table maps to an IANA identifier. Both
   * the call and the table are phase 4; see WINDOWS-TODO.md, where "done"
   * means gchron_zonedb_local() on a machine set to Pacific Standard Time
   * returns America/Los_Angeles.
   */
  return GCHRON_ERR_UNSUPPORTED;
#else
  /* 2a. /etc/localtime as a symlink: the tail of its target is the name. */
  {
    char * id = localtime_link_id(allocator);
    if (id != NULL) {
      GCHRON_Result result = gchron_zonedb_zone(db, id, out);
      gcu_allocator_free(allocator, id);
      if (result == GCHRON_OK) {
        return GCHRON_OK;
      }
    }
  }

  /* 2b. /etc/timezone names it, and /etc/localtime is a copy. */
  {
    char * id = etc_timezone_id(allocator);
    if (id != NULL) {
      GCHRON_Result result = gchron_zonedb_zone(db, id, out);
      gcu_allocator_free(allocator, id);
      if (result == GCHRON_OK) {
        return GCHRON_OK;
      }
    }
  }

  /* 2c. /etc/localtime as a plain file: a zone with rules and no name.
   *     gchron_zone_id() returns NULL for it, which is the honest answer. */
  {
    void * data = NULL;
    size_t len = 0;
    const GCHRON_Limits * limits = gchron_zonedb_limits_of(db);
    if (gchron_zone_read_file("/etc/localtime", limits->max_tzif_bytes,
            allocator, &data, &len) == GCHRON_OK) {
      GCHRON_Zone * zone = NULL;
      GCHRON_Result result = gchron_tzif_parse(data, len, limits, allocator,
          &zone);
      gcu_allocator_free(allocator, data);
      if (result == GCHRON_OK) {
        result = gchron_zonedb_intern(db, LOCAL_KEY, zone, out);
        if (result != GCHRON_OK) {
          gchron_zone_free(zone);
        }
        return result;
      }
    }
  }

  return GCHRON_ERR_UNSUPPORTED;
#endif
}

/*--------------------------------------------------------------------------*
 * Listing a directory
 *--------------------------------------------------------------------------*/

#if !defined(_WIN32)

/**
 * Whether a directory entry is one of the parallel trees rather than a zone.
 *
 * `posix/` and `right/` are whole copies of the database - the second counts
 * leap seconds, which design.md section 5.1 says this library's instant
 * deliberately does not - and listing them would double every identifier.
 *
 * The *files* zoneinfo carries beside its zones are not named here. A
 * name-based list of them is a list that goes stale: it had every one this
 * machine holds and still missed `zonenow.tab`, which tzdata added in 2023,
 * and the listing then offered it as a zone that failed to load. What a zone
 * file is, is a file that begins with `TZif` - which is a property of the
 * file rather than of the naming conventions of whoever packaged it.
 */
static bool is_parallel_tree(const char * name) {
  return strcmp(name, "posix") == 0 || strcmp(name, "right") == 0;
}

/** Whether a file begins with the TZif magic. */
static bool is_tzif_file(const char * path) {
  FILE * handle = fopen(path, "rb");
  char magic[4];
  size_t read_bytes;

  if (handle == NULL) {
    return false;
  }
  read_bytes = fread(magic, 1, sizeof(magic), handle);
  fclose(handle);
  return read_bytes == sizeof(magic) && memcmp(magic, "TZif", 4) == 0;
}

/** How long a path this walk will build. */
#define WALK_PATH_MAX 1024

/**
 * Join two path components, refusing to truncate.
 *
 * A truncated path is not a cosmetic problem: it names a *different file*,
 * which would then be opened and read as a zone. `snprintf` reports the
 * length it wanted, so the one case worth handling is the one where that
 * exceeds the buffer.
 *
 * @return `true` when the whole path fit.
 */
static bool join_into(char * out, size_t size, const char * a,
    const char * b) {
  int written;

  if (a[0] == '\0') {
    written = snprintf(out, size, "%s", b);
  }
  else if (b[0] == '\0') {
    written = snprintf(out, size, "%s", a);
  }
  else {
    written = snprintf(out, size, "%s/%s", a, b);
  }
  return written > 0 && (size_t)written < size;
}

/** Walk a directory, visiting every zone identifier under it. */
static GCHRON_Result walk(GCHRON_ZoneDb * db, const char * root,
    const char * prefix, int depth,
    GCHRON_Result (*visit)(GCHRON_ZoneDb *, const char *)) {
  char path[WALK_PATH_MAX];
  DIR * dir;
  struct dirent * entry;
  GCHRON_Result result = GCHRON_OK;

  /* Bounded, because a zoneinfo directory is a filesystem and a filesystem
   * can contain a symlink loop. Four levels is one more than the deepest real
   * identifier (`America/Argentina/Buenos_Aires`). */
  if (depth > 4) {
    return GCHRON_OK;
  }
  if (!join_into(path, sizeof(path), root, prefix)) {
    return GCHRON_OK;
  }

  dir = opendir(path);
  if (dir == NULL) {
    return GCHRON_ERR_IO;
  }
  while ((entry = readdir(dir)) != NULL && result == GCHRON_OK) {
    char child[WALK_PATH_MAX];
    char full[WALK_PATH_MAX];
    struct stat info;

    if (entry->d_name[0] == '.' || is_parallel_tree(entry->d_name)) {
      continue;
    }
    if (!join_into(child, sizeof(child), prefix, entry->d_name)) {
      continue;
    }
    if (!join_into(full, sizeof(full), root, child)) {
      continue;
    }
    if (stat(full, &info) != 0) {
      continue;
    }
    if (S_ISDIR(info.st_mode)) {
      result = walk(db, root, child, depth + 1, visit);
    }
    else if (S_ISREG(info.st_mode) && gchron_zone_id_is_safe(child)
        && is_tzif_file(full)) {
      result = visit(db, child);
    }
  }
  closedir(dir);
  return result;
}

#endif /* !_WIN32 */

GCHRON_Result gchron_zonedb_list(GCHRON_ZoneDb * db,
    const char * const ** out_ids, size_t * out_count) {
  if (db == NULL) {
    return GCHRON_ERR_INVALID;
  }
  return gchron_zonedb_ids_of(db, out_ids, out_count);
}

/**
 * Enumerate a database's directory into its identifier list.
 *
 * Called by zonedb.c the first time gchron_zonedb_list() is asked, so that a
 * database that is only ever used to look zones up never walks a filesystem.
 */
GCHRON_Result gchron_zonedb_walk_directory(GCHRON_ZoneDb * db,
    const char * root, GCHRON_Result (*visit)(GCHRON_ZoneDb *, const char *)) {
#if defined(_WIN32)
  /* TODO(windows): there is no directory to walk; the embedded table
   * enumerates itself. See WINDOWS-TODO.md. */
  (void)db;
  (void)root;
  (void)visit;
  return GCHRON_ERR_UNSUPPORTED;
#else
  return walk(db, root, "", 0, visit);
#endif
}
