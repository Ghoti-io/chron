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
 * The zone database: where zone files come from, and the cache in front of
 * them.
 *
 * Zones are loaded on first use and cached, and the database is internally
 * synchronised, so that one of these serves a whole process. That is a
 * stronger promise than CONVENTIONS.md section 5's default, and it is made
 * because the alternative is every application inventing a lock around it.
 *
 * What is *not* here is a process-wide default database. design.md, mistake
 * M2: `tzset()` and a global `TZ` are the defect, and a library that shipped
 * a hidden singleton would have reinvented it with better manners.
 */

#include <ghoti.io/chron/allocator.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/offset.h>
#include <ghoti.io/chron/zone.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/cutil/file.h>
#include <ghoti.io/cutil/mutex.h>
#include <ghoti.io/cutil/path.h>
#include <ghoti.io/cutil/safemath.h>

#include "../core/core_internal.h"
#include "zone_internal.h"

/** Defined in local.c, which owns every filesystem and environment call. */
GCHRON_Result gchron_zonedb_walk_directory(GCHRON_ZoneDb * db,
    const char * root, GCHRON_Result (*visit)(GCHRON_ZoneDb *, const char *));

/** One cached zone, keyed by the identifier the caller asked for. */
typedef struct CacheEntry {
  char * key;          /**< The identifier as asked for; owned. */
  GCHRON_Zone * zone;  /**< The zone; owned. */
} CacheEntry;

struct GCHRON_ZoneDb {
  const GCHRON_Allocator * allocator;
  GCHRON_Limits limits;
  GCHRON_ZoneSource source;
  char * directory;        /**< Where TZif files live; NULL when synthesised. */
  char * version;          /**< The tzdata release, or NULL if unknown. */

  GCU_MUTEX_T lock;
  bool lock_ready;

  CacheEntry * entries;
  size_t entry_count;
  size_t entry_capacity;

  char ** ids;             /**< Lazily built by gchron_zonedb_list(). */
  size_t id_count;
  bool ids_built;

  /*
   * The tzdb's own link table, from `<directory>/tzdata.zi`, built on first
   * use. `link_text` owns the bytes; the two arrays point into it.
   *
   * A directory does not necessarily contain a file for every zone name the
   * tzdb defines. Debian splits the backward-compatibility names into a
   * `tzdata-legacy` package that is not installed by default, so on a stock
   * Debian there is no `Asia/Calcutta`, no `Europe/Kiev` and no `US/Eastern`
   * on disk - while `tzdata.zi`, which ships with base tzdata and which this
   * database already reads for its version, lists every one of them.
   */
  char * link_text;
  const char ** link_names;   /**< Sorted, for a binary search. */
  const char ** link_targets;
  size_t link_count;
  bool links_built;
};

/** Duplicate a string through an allocator. */
static char * dup_string(const GCHRON_Allocator * allocator, const char * s) {
  size_t length;
  char * copy;

  if (s == NULL) {
    return NULL;
  }
  length = strlen(s) + 1;
  copy = (char *)gcu_allocator_malloc(allocator, length);
  if (copy != NULL) {
    memcpy(copy, s, length);
  }
  return copy;
}

bool gchron_zone_id_is_safe(const char * id) {
  size_t i;
  size_t length;

  if (id == NULL) {
    return false;
  }
  length = strlen(id);
  if (length == 0 || length > 256) {
    return false;
  }
  /*
   * A zone identifier reaches this library from a document - RFC 9557 puts
   * one inside a timestamp - and is about to be appended to a directory and
   * opened. Everything that could make it escape that directory is refused
   * here, before any path is built: a leading slash, a `..` component, a
   * doubled slash, a backslash, and anything outside the small set real
   * identifiers use.
   */
  if (id[0] == '/' || id[0] == '.' || id[0] == '-') {
    return false;
  }
  for (i = 0; i < length; ++i) {
    char c = id[i];
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-'
        || c == '+' || c == '.';
    if (!ok) {
      return false;
    }
    if (c == '/' && (i + 1 >= length || id[i + 1] == '/')) {
      return false;
    }
    if (c == '.' && i + 1 < length && id[i + 1] == '.') {
      return false;
    }
  }
  return true;
}

GCHRON_Result gchron_zone_read_file(const char * path, size_t max_bytes,
    const GCHRON_Allocator * allocator, void ** out_data, size_t * out_len) {
  /*
   * cutil owns whole-file reading for the suite, so this is now the boundary
   * between its result enum and ours rather than a second copy of the loop.
   *
   * It is not only a deduplication. What stood here sized the file with
   * fseek() and ftell() and then read that many bytes, which reports an empty
   * file for anything that has no size to tell - a pipe, a character device,
   * anything under /proc - and `/etc/localtime` is a regular file by
   * convention rather than by rule. cutil reads in chunks instead, and on
   * Windows it opens through the wide entry point, which fopen() cannot do
   * for a path whose bytes are UTF-8.
   *
   * GCU_FILE_UNLIMITED is 0, which is what this function already documented
   * `max_bytes` of 0 to mean, so the caller-facing contract is unchanged.
   */
  /*
   * Checked here so that a GCU_FILE_ERR_INVALID coming back can only be about
   * the path's *length*, never about this library having handed cutil a null.
   * The mapping below depends on that being true.
   */
  if (path == NULL || out_data == NULL || out_len == NULL) {
    return GCHRON_ERR_INVALID;
  }

  switch (gcu_file_read(path, max_bytes, allocator, out_data, out_len)) {
    case GCU_FILE_OK:
      return GCHRON_OK;
    case GCU_FILE_ERR_LIMIT:
      return GCHRON_ERR_LIMIT;
    case GCU_FILE_ERR_OOM:
      return GCHRON_ERR_OOM;
    /*
     * A default rather than the remaining names spelled out. Enumerating them
     * turns the next value cutil adds into a build failure here, and - worse
     * - invites answering it with GCHRON_ERR_INTERNAL, which would report a
     * bug in this library to a caller whose file was simply deleted. Every
     * way a read can fail that this function has no better word for is an I/O
     * failure, which is what the caller needs to know.
     *
     * GCU_FILE_ERR_INVALID is deliberately among them, and is the one that
     * looks like it should not be. It means `ENAMETOOLONG`, and
     * GCHRON_ERR_INVALID means "a caller-supplied argument is wrong" - which
     * this is not. `NAME_MAX` and `PATH_MAX` are per-filesystem, so the same
     * path is too long on one mount and fine on another, and the argument the
     * caller supplied is a zone identifier that may be perfectly good; what
     * was too long is the directory this database was built with. Reported as
     * GCHRON_ERR_INVALID it told a caller to fix `Europe/Paris`.
     *
     * It also matters further up: gchron_zonedb_zone() consults the tzdb's
     * link table only when this says GCHRON_ERR_IO, so an over-long path used
     * to skip the backward-compatibility names as well as mislabelling
     * itself.
     */
    default:
      break;
  }
  return GCHRON_ERR_IO;
}

/**
 * Build `<directory>/<id>`, through cutil's path rules.
 *
 * The name is checked here rather than only at the entry point, because this
 * is the single place in the library that turns an identifier into a path,
 * and gcu_path_join() deliberately lets an absolute right-hand side replace
 * the left - the rule that makes a configuration override behave, and exactly
 * the wrong one for a name that is supposed to select a file *within* a
 * directory. gchron_zone_id_is_safe() refuses a leading separator and a `..`,
 * which is what keeps the two rules from disagreeing.
 *
 * Putting it here also closes a gap. Callers were checking the identifier the
 * *caller* supplied, and then load_zone() fell back to a name it had read out
 * of the directory's own `tzdata.zi` without checking that one. A link line
 * reading `L ../../../etc/shadow Foo` would have been joined unexamined.
 *
 * @return The path, for the caller to free, or NULL if the name is unsafe,
 *   the join does not fit, or the allocation failed.
 */
static char * join_path(const GCHRON_Allocator * allocator,
    const char * directory, const char * id) {
  size_t length = 0;
  size_t size;
  char * path;

  if (directory == NULL || id == NULL || !gchron_zone_id_is_safe(id)) {
    return NULL;
  }
  /* Measure, then write: the join has no allocating form, because its result
   * is always bounded by its inputs. */
  if (gcu_path_join(GCU_PATH_NATIVE, directory, id, NULL, 0, &length)
      != GCU_PATH_OK
      || !gcu_safe_add_size(length, 1, &size)) {
    return NULL;
  }
  path = (char *)gcu_allocator_malloc(allocator, size);
  if (path == NULL) {
    return NULL;
  }
  if (gcu_path_join(GCU_PATH_NATIVE, directory, id, path, size, NULL)
      != GCU_PATH_OK) {
    gcu_allocator_free(allocator, path);
    return NULL;
  }
  return path;
}

/**
 * Read the tzdata release out of a zoneinfo directory.
 *
 * Two spellings exist: a `+VERSION` file, which is what `zic` writes, and the
 * `# version` line at the top of `tzdata.zi`, which is what Debian ships.
 * Neither is guaranteed; a database that cannot say its version returns NULL
 * from gchron_zonedb_version() rather than a guess.
 */
static char * read_version(const GCHRON_Allocator * allocator,
    const char * directory) {
  static const char * const candidates[] = { "+VERSION", "tzdata.zi" };
  size_t which;

  for (which = 0; which < 2; ++which) {
    char * path = join_path(allocator, directory, candidates[which]);
    void * data = NULL;
    size_t len = 0;
    char * result = NULL;

    if (path == NULL) {
      return NULL;
    }
    if (gchron_zone_read_file(path, 1024 * 1024, allocator, &data, &len)
        == GCHRON_OK) {
      char * text = (char *)data;
      char * start = text;
      char * end;

      if (which == 1) {
        /* `# version 2026c` on the first line, or no version at all. */
        if (len < 10 || memcmp(text, "# version ", 10) != 0) {
          gcu_allocator_free(allocator, data);
          gcu_allocator_free(allocator, path);
          continue;
        }
        start = text + 10;
      }
      end = start;
      while (*end != '\0' && *end != '\n' && *end != '\r' && *end != ' ') {
        ++end;
      }
      *end = '\0';
      if (end != start) {
        result = dup_string(allocator, start);
      }
      gcu_allocator_free(allocator, data);
    }
    gcu_allocator_free(allocator, path);
    if (result != NULL) {
      return result;
    }
  }
  return NULL;
}

/** Create an empty database. */
static GCHRON_Result db_create(const GCHRON_Allocator * allocator,
    const GCHRON_Limits * limits, GCHRON_ZoneSource source,
    const char * directory, GCHRON_ZoneDb ** out) {
  GCHRON_ZoneDb * db;

  /*
   * "NULL means the default" is resolved once, here at the boundary, rather
   * than at each use. cutil's allocator helpers accept NULL and default for
   * themselves, but gchron_tzif_parse() refuses it as a missing argument -
   * and a database that kept the NULL handed it on to every zone it loaded,
   * which turned every lookup into GCHRON_ERR_INVALID.
   */
  if (allocator == NULL) {
    allocator = gchron_allocator_default();
  }
  db = (GCHRON_ZoneDb *)gcu_allocator_calloc(allocator, 1,
      sizeof(GCHRON_ZoneDb));
  if (db == NULL) {
    return GCHRON_ERR_OOM;
  }
  db->allocator = allocator;
  if (limits != NULL) {
    db->limits = *limits;
  }
  else {
    gchron_limits_default(&db->limits);
  }
  db->source = source;
  if (directory != NULL) {
    db->directory = dup_string(allocator, directory);
    if (db->directory == NULL) {
      gcu_allocator_free(allocator, db);
      return GCHRON_ERR_OOM;
    }
    db->version = read_version(allocator, directory);
  }
  if (GCU_MUTEX_CREATE(db->lock) == 0) {
    db->lock_ready = true;
  }
  else {
    gcu_allocator_free(allocator, db->directory);
    gcu_allocator_free(allocator, db->version);
    gcu_allocator_free(allocator, db);
    return GCHRON_ERR_INTERNAL;
  }
  *out = db;
  return GCHRON_OK;
}

void gchron_zonedb_destroy(GCHRON_ZoneDb * db) {
  size_t i;
  const GCHRON_Allocator * allocator;

  if (db == NULL) {
    return;
  }
  allocator = db->allocator;
  for (i = 0; i < db->entry_count; ++i) {
    gcu_allocator_free(allocator, db->entries[i].key);
    gchron_zone_free(db->entries[i].zone);
  }
  gcu_allocator_free(allocator, db->entries);
  for (i = 0; i < db->id_count; ++i) {
    gcu_allocator_free(allocator, db->ids[i]);
  }
  gcu_allocator_free(allocator, db->ids);
  gcu_allocator_free(allocator, db->directory);
  gcu_allocator_free(allocator, db->version);
  gcu_allocator_free(allocator, db->link_text);
  gcu_allocator_free(allocator, (void *)db->link_names);
  gcu_allocator_free(allocator, (void *)db->link_targets);
  if (db->lock_ready) {
    GCU_MUTEX_DESTROY(db->lock);
  }
  gcu_allocator_free(allocator, db);
}

/** Find a cached zone. The caller holds the lock. */
static GCHRON_Zone * cache_find(GCHRON_ZoneDb * db, const char * key) {
  size_t i;
  for (i = 0; i < db->entry_count; ++i) {
    if (strcmp(db->entries[i].key, key) == 0) {
      return db->entries[i].zone;
    }
  }
  return NULL;
}

/** Add a zone to the cache, which takes ownership. The caller holds the lock. */
static GCHRON_Result cache_add(GCHRON_ZoneDb * db, const char * key,
    GCHRON_Zone * zone) {
  if (db->limits.max_zones != 0 && db->entry_count >= db->limits.max_zones) {
    return GCHRON_ERR_LIMIT;
  }
  if (db->entry_count == db->entry_capacity) {
    size_t capacity = db->entry_capacity == 0 ? 16 : db->entry_capacity * 2;
    size_t bytes;
    CacheEntry * grown;

    if (!gcu_safe_mul_size(capacity, sizeof(CacheEntry), &bytes)) {
      return GCHRON_ERR_OOM;
    }
    grown = (CacheEntry *)gcu_allocator_realloc(db->allocator, db->entries,
        bytes);
    if (grown == NULL) {
      return GCHRON_ERR_OOM;
    }
    db->entries = grown;
    db->entry_capacity = capacity;
  }
  db->entries[db->entry_count].key = dup_string(db->allocator, key);
  if (db->entries[db->entry_count].key == NULL) {
    return GCHRON_ERR_OOM;
  }
  db->entries[db->entry_count].zone = zone;
  db->entry_count += 1;
  return GCHRON_OK;
}

/** Lock, if this database has a working lock. */
static void db_lock(GCHRON_ZoneDb * db) {
  if (db->lock_ready) {
    GCU_MUTEX_LOCK(db->lock);
  }
}

/** Unlock. */
static void db_unlock(GCHRON_ZoneDb * db) {
  if (db->lock_ready) {
    GCU_MUTEX_UNLOCK(db->lock);
  }
}

/*--------------------------------------------------------------------------*
 * Creating a database
 *--------------------------------------------------------------------------*/

/** Where the platform keeps its zone files. */
static const char * platform_zoneinfo(void) {
#if defined(_WIN32)
  /* TODO(windows): Windows has no zoneinfo directory. gchron_zonedb_system()
   * says so rather than inventing a path; gchron_zonedb_embedded() is the
   * route, and phase 4 builds its table. See the workspace's
   * notes/suite/WINDOWS-TODO.md. */
  return NULL;
#else
  return "/usr/share/zoneinfo";
#endif
}

GCHRON_Result gchron_zonedb_system(const GCHRON_Allocator * allocator,
    const GCHRON_Limits * limits, GCHRON_ZoneDb ** out) {
  const char * directory;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  /*
   * `TZDIR` first, because that is what the tzdb's own tools honour and what
   * a test fixture sets. It is read here and in gchron_zonedb_local(), and
   * nowhere else in the library.
   */
  directory = getenv("TZDIR");
  if (directory == NULL || directory[0] == '\0') {
    directory = platform_zoneinfo();
  }
  if (directory == NULL) {
    return GCHRON_ERR_UNSUPPORTED;
  }
  return db_create(allocator, limits, GCHRON_ZONE_SOURCE_SYSTEM, directory,
      out);
}

GCHRON_Result gchron_zonedb_directory(const char * path,
    const GCHRON_Allocator * allocator, const GCHRON_Limits * limits,
    GCHRON_ZoneDb ** out) {
  if (path == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  return db_create(allocator, limits, GCHRON_ZONE_SOURCE_DIRECTORY, path, out);
}

GCHRON_Result gchron_zonedb_embedded(const GCHRON_Allocator * allocator,
    const GCHRON_Limits * limits, GCHRON_ZoneDb ** out) {
  GCHRON_ZoneDb * db = NULL;
  GCHRON_Result result;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (gchron_tzdata_embedded_count() == 0) {
    /*
     * The generator refuses to write an empty table, so this should be
     * unreachable. It is checked anyway, because an empty embedded database
     * would answer every lookup with GCHRON_ERR_UNSUPPORTED and be
     * indistinguishable from a working one asked for a zone it lacks - and
     * the whole point of this call is that a caller with no zoneinfo
     * directory can rely on it.
     */
    return GCHRON_ERR_UNSUPPORTED;
  }

  result = db_create(allocator, limits, GCHRON_ZONE_SOURCE_EMBEDDED, NULL,
      &db);
  if (result != GCHRON_OK) {
    return result;
  }
  db->version = dup_string(db->allocator, gchron_tzdata_embedded_version());
  if (db->version == NULL) {
    gchron_zonedb_destroy(db);
    return GCHRON_ERR_OOM;
  }
  *out = db;
  return GCHRON_OK;
}

GCHRON_Result gchron_zonedb_default(const GCHRON_Allocator * allocator,
    const GCHRON_Limits * limits, GCHRON_ZoneDb ** out) {
  GCHRON_Result result;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  /*
   * design.md section 6.2: choose by currency. tzdata releases are named
   * `YYYYx` - a year and a lowercase letter - which orders lexically for as
   * long as the year has four digits, so `strcmp` is the comparison and not
   * an approximation of one.
   */
  GCHRON_ZoneDb * system_db = NULL;
  const char * system_version;

  result = gchron_zonedb_system(allocator, limits, &system_db);
  if (result != GCHRON_OK) {
    /* No system database at all - which on Windows is every machine. */
    return gchron_zonedb_embedded(allocator, limits, out);
  }

  system_version = gchron_zonedb_version(system_db);
  if (system_version == NULL) {
    /*
     * The system database is there but will not say which release it is.
     * Preferred anyway, on the reasoning that the operating system's copy is
     * the one somebody is updating - and a database that cannot name its
     * version is far more likely to be a distribution that strips the version
     * file than one that is out of date.
     */
    *out = system_db;
    return GCHRON_OK;
  }

  {
    GCHRON_ZoneDb * embedded_db = NULL;
    const char * embedded_version;

    if (gchron_zonedb_embedded(allocator, limits, &embedded_db) != GCHRON_OK) {
      /* Nothing to compare against; the system database stands. */
      *out = system_db;
      return GCHRON_OK;
    }
    embedded_version = gchron_zonedb_version(embedded_db);
    if (embedded_version == NULL
        || strcmp(embedded_version, system_version) <= 0) {
      /*
       * Ties go to the system database, for the same reason: equal currency
       * and one of them is the copy being maintained.
       */
      gchron_zonedb_destroy(embedded_db);
      *out = system_db;
      return GCHRON_OK;
    }

    /*
     * The embedded table is newer. This happens when a long-lived machine has
     * not had its tzdata updated since this library was built.
     * gchron_zonedb_source() and gchron_zonedb_version() both say which was
     * chosen, and gchron_zonedb_dump() prints it: a fallback that cannot be
     * seen is the defect CONVENTIONS.md section 1 names.
     */
    gchron_zonedb_destroy(system_db);
    *out = embedded_db;
    return GCHRON_OK;
  }
}

GCHRON_Result gchron_zonedb_memory(const void * blob, size_t len,
    const char * id, const GCHRON_Allocator * allocator,
    const GCHRON_Limits * limits, GCHRON_ZoneDb ** out) {
  GCHRON_ZoneDb * db;
  GCHRON_Zone * zone;
  GCHRON_Result result;

  if (blob == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = db_create(allocator, limits, GCHRON_ZONE_SOURCE_MEMORY, NULL, &db);
  if (result != GCHRON_OK) {
    return result;
  }
  result = gchron_tzif_parse(blob, len, &db->limits, db->allocator, &zone);
  if (result != GCHRON_OK) {
    gchron_zonedb_destroy(db);
    return result;
  }
  if (id != NULL) {
    zone->id = dup_string(db->allocator, id);
    if (zone->id == NULL) {
      gchron_zone_free(zone);
      gchron_zonedb_destroy(db);
      return GCHRON_ERR_OOM;
    }
    zone->canonical_id = zone->id;
  }
  result = cache_add(db, id != NULL ? id : "", zone);
  if (result != GCHRON_OK) {
    gchron_zone_free(zone);
    gchron_zonedb_destroy(db);
    return result;
  }
  *out = db;
  return GCHRON_OK;
}

/*--------------------------------------------------------------------------*
 * Looking a zone up
 *--------------------------------------------------------------------------*/

/** Load a zone from the database's directory. The caller holds the lock. */

/** Order two link names, for qsort and bsearch over parallel arrays. */
static int compare_link(const void * a, const void * b) {
  return strcmp(*(const char * const *)a, *(const char * const *)b);
}

/**
 * Parse `<directory>/tzdata.zi` into the link table. The caller holds the
 * lock. Failure is not an error: a directory with no `tzdata.zi` simply has
 * no links, and every lookup then behaves as it did before.
 */
static void build_links(GCHRON_ZoneDb * db) {
  char * path;
  void * data = NULL;
  size_t len = 0;
  size_t count = 0;
  size_t index = 0;
  char * cursor;
  char * end;

  db->links_built = true;
  if (db->directory == NULL) {
    return;
  }
  path = join_path(db->allocator, db->directory, "tzdata.zi");
  if (path == NULL) {
    return;
  }
  if (gchron_zone_read_file(path, 4 * 1024 * 1024, db->allocator, &data, &len)
      != GCHRON_OK) {
    gcu_allocator_free(db->allocator, path);
    return;
  }
  gcu_allocator_free(db->allocator, path);

  /* Count first, so the arrays are allocated once. */
  cursor = (char *)data;
  end = cursor + len;
  while (cursor < end) {
    if ((cursor == (char *)data || cursor[-1] == '\n')
        && (cursor[0] == 'L' || cursor[0] == 'l')
        && cursor + 1 < end && (cursor[1] == ' ' || cursor[1] == '\t')) {
      count += 1;
    }
    cursor += 1;
  }
  if (count == 0) {
    gcu_allocator_free(db->allocator, data);
    return;
  }

  db->link_names = (const char **)gcu_allocator_malloc(db->allocator,
      count * sizeof(char *));
  db->link_targets = (const char **)gcu_allocator_malloc(db->allocator,
      count * sizeof(char *));
  if (db->link_names == NULL || db->link_targets == NULL) {
    gcu_allocator_free(db->allocator, (void *)db->link_names);
    gcu_allocator_free(db->allocator, (void *)db->link_targets);
    db->link_names = NULL;
    db->link_targets = NULL;
    gcu_allocator_free(db->allocator, data);
    return;
  }

  /*
   * Rewrite the text in place into NUL-terminated fields. The buffer is kept
   * as `link_text`, so every name and target is a pointer into it and there
   * is one allocation rather than two per link.
   */
  cursor = (char *)data;
  while (cursor < end && index < count) {
    char * line = cursor;
    char * stop = line;
    char * target;
    char * name;

    while (stop < end && *stop != '\n') {
      stop += 1;
    }
    cursor = (stop < end) ? stop + 1 : end;
    /*
     * The predicate the counting pass above uses, spelled the same way on
     * purpose. It used to test only the first character here, and the two
     * passes disagreeing is worse than either of them being wrong: this loop
     * stops at `index < count`, so a line accepted here that was not counted
     * there spends a slot reserved for a real link, and the real link falls
     * off the end and stops resolving. `Link ...` - the tzdb's own source
     * spelling, which `zishrink.awk` does not emit but zic accepts - was such
     * a line, and one of them ahead of a genuine `L` line silently cost that
     * link. Whether the long form ought to be supported is a separate
     * question from whether the two passes answer it alike.
     */
    if ((*line != 'L' && *line != 'l') || line + 1 >= stop
        || (line[1] != ' ' && line[1] != '\t')) {
      continue;
    }
    *stop = '\0';

    /* `L <target> <link name>`, whitespace-separated. */
    target = line + 1;
    while (*target == ' ' || *target == '\t') { target += 1; }
    name = target;
    while (*name != '\0' && *name != ' ' && *name != '\t') { name += 1; }
    if (*name == '\0') {
      continue;
    }
    *name = '\0';
    name += 1;
    while (*name == ' ' || *name == '\t') { name += 1; }
    {
      char * tail = name;
      while (*tail != '\0' && *tail != ' ' && *tail != '\t'
          && *tail != '\r') {
        tail += 1;
      }
      *tail = '\0';
    }
    if (*name == '\0' || *target == '\0') {
      continue;
    }
    db->link_names[index] = name;
    db->link_targets[index] = target;
    index += 1;
  }
  db->link_count = index;
  db->link_text = (char *)data;

  /*
   * Sorted by name so a lookup is a binary search. The two arrays are kept
   * parallel by sorting an index-free copy: qsort on `link_names` alone would
   * leave `link_targets` behind, so they are sorted together by hand.
   */
  {
    size_t i;
    size_t j;
    for (i = 1; i < db->link_count; ++i) {
      const char * name = db->link_names[i];
      const char * target = db->link_targets[i];
      j = i;
      while (j > 0 && strcmp(db->link_names[j - 1], name) > 0) {
        db->link_names[j] = db->link_names[j - 1];
        db->link_targets[j] = db->link_targets[j - 1];
        j -= 1;
      }
      db->link_names[j] = name;
      db->link_targets[j] = target;
    }
  }
}

/**
 * What the tzdb says @p id is a link to, or NULL. The caller holds the lock.
 */
static const char * link_target(GCHRON_ZoneDb * db, const char * id) {
  const char ** found;

  if (!db->links_built) {
    build_links(db);
  }
  if (db->link_count == 0) {
    return NULL;
  }
  found = (const char **)bsearch(&id, db->link_names, db->link_count,
      sizeof(char *), compare_link);
  if (found == NULL) {
    return NULL;
  }
  return db->link_targets[found - db->link_names];
}

static GCHRON_Result load_zone(GCHRON_ZoneDb * db, const char * id,
    GCHRON_Zone ** out) {
  char * path;
  void * data = NULL;
  size_t len = 0;
  GCHRON_Result result;

  if (db->source == GCHRON_ZONE_SOURCE_EMBEDDED) {
    /*
     * The bytes are already here, in a static blob. They are handed to the
     * parser as they are - the parser copies what it keeps - so an embedded
     * lookup reads no file and allocates nothing for the image itself.
     */
    const GCHRON_EmbeddedZone * entry = gchron_tzdata_embedded_find(id);
    if (entry == NULL) {
      return GCHRON_ERR_UNSUPPORTED;
    }
    return gchron_tzif_parse(gchron_tzdata_embedded_bytes(entry),
        entry->length, &db->limits, db->allocator, out);
  }

  if (db->directory == NULL) {
    return GCHRON_ERR_UNSUPPORTED;
  }
  path = join_path(db->allocator, db->directory, id);
  if (path == NULL) {
    return GCHRON_ERR_OOM;
  }
  result = gchron_zone_read_file(path, db->limits.max_tzif_bytes,
      db->allocator, &data, &len);
  gcu_allocator_free(db->allocator, path);
  if (result == GCHRON_ERR_IO) {
    /*
     * No file of that name. Before giving up, ask the tzdb's own link table:
     * a directory need not contain a file for every name the tzdb defines,
     * and Debian's default install has none of the backward-compatibility
     * names - no `Asia/Calcutta`, no `Europe/Kiev`, no `US/Eastern` - while
     * `tzdata.zi` beside them lists every one.
     *
     * Without this, a caller on a stock Debian could not open names that
     * CLDR, Java and a great deal of existing configuration still use, and
     * the embedded database could while the system one could not.
     */
    const char * target = link_target(db, id);
    if (target != NULL && strcmp(target, id) != 0) {
      char * link_path = join_path(db->allocator, db->directory, target);
      if (link_path != NULL) {
        result = gchron_zone_read_file(link_path, db->limits.max_tzif_bytes,
            db->allocator, &data, &len);
        gcu_allocator_free(db->allocator, link_path);
      }
    }
    if (result != GCHRON_OK) {
      /*
       * The zone is not in this database, which is a different thing from
       * the file being unreadable - but the C library does not tell the two
       * apart through fopen alone, and UNSUPPORTED is the answer a caller
       * acts on.
       */
      return GCHRON_ERR_UNSUPPORTED;
    }
  }
  if (result != GCHRON_OK) {
    return result;
  }

  result = gchron_tzif_parse(data, len, &db->limits, db->allocator, out);
  gcu_allocator_free(db->allocator, data);
  return result;
}

GCHRON_Result gchron_zonedb_zone(GCHRON_ZoneDb * db, const char * id,
    const GCHRON_Zone ** out) {
  GCHRON_Zone * zone;
  GCHRON_Result result;

  if (db == NULL || id == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_zone_id_is_safe(id)) {
    return GCHRON_ERR_INVALID;
  }

  db_lock(db);
  zone = cache_find(db, id);
  if (zone != NULL) {
    db_unlock(db);
    *out = zone;
    return GCHRON_OK;
  }

  result = load_zone(db, id, &zone);
  if (result != GCHRON_OK) {
    db_unlock(db);
    return result;
  }

  zone->id = dup_string(db->allocator, id);
  if (zone->id == NULL) {
    gchron_zone_free(zone);
    db_unlock(db);
    return GCHRON_ERR_OOM;
  }
  /*
   * A backward-compatibility link such as `US/Eastern` is a real file in the
   * directory - a copy or a hard link, depending on how the distribution
   * built it - so it loads like any other zone, and what it does not carry is
   * the name it is a link *to*: TZif has nowhere to put one.
   *
   * The embedded table does have that name, because the generator can see
   * which entries were symbolic links. So an embedded database answers
   * gchron_zone_canonical_id() with the real name and a directory-backed one
   * still answers with the name it was asked for - which is the truth
   * available to each, rather than an invention by either.
   */
  zone->canonical_id = zone->id;
  {
    const char * canonical_name = NULL;
    if (db->source == GCHRON_ZONE_SOURCE_EMBEDDED) {
      const GCHRON_EmbeddedZone * entry = gchron_tzdata_embedded_find(id);
      canonical_name = (entry != NULL) ? entry->canonical : NULL;
    }
    else if (db->directory != NULL) {
      /* The same `tzdata.zi` that made the lookup work also says what the
       * name resolved to, so a directory-backed database can now answer
       * gchron_zone_canonical_id() as truthfully as the embedded one. */
      canonical_name = link_target(db, id);
    }
    if (canonical_name != NULL && strcmp(canonical_name, id) != 0) {
      char * canonical = dup_string(db->allocator, canonical_name);
      if (canonical == NULL) {
        gchron_zone_free(zone);
        db_unlock(db);
        return GCHRON_ERR_OOM;
      }
      /* gchron_zone_free() frees this when it differs from `id`. */
      zone->canonical_id = canonical;
    }
  }

  result = cache_add(db, id, zone);
  if (result != GCHRON_OK) {
    gchron_zone_free(zone);
    db_unlock(db);
    return result;
  }
  db_unlock(db);
  *out = zone;
  return GCHRON_OK;
}

GCHRON_Result gchron_zonedb_fixed(GCHRON_ZoneDb * db, int32_t offset_sec,
    const GCHRON_Zone ** out) {
  char key[32];
  GCHRON_Zone * zone;
  GCHRON_Result result;

  if (db == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  snprintf(key, sizeof(key), "\x01""fixed:%d", (int)offset_sec);

  db_lock(db);
  zone = cache_find(db, key);
  if (zone != NULL) {
    db_unlock(db);
    *out = zone;
    return GCHRON_OK;
  }
  result = gchron_zone_build_fixed(offset_sec, NULL, NULL, db->allocator,
      &zone);
  if (result != GCHRON_OK) {
    db_unlock(db);
    return result;
  }
  result = cache_add(db, key, zone);
  if (result != GCHRON_OK) {
    gchron_zone_free(zone);
    db_unlock(db);
    return result;
  }
  db_unlock(db);
  *out = zone;
  return GCHRON_OK;
}

GCHRON_Result gchron_zonedb_posix(GCHRON_ZoneDb * db, const char * rule,
    const GCHRON_Zone ** out) {
  char stack_key[128];
  char * key = stack_key;
  GCHRON_Zone * zone;
  GCHRON_Result result;
  size_t length;

  if (db == NULL || rule == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  length = strlen(rule);
  /*
   * The cache key is the rule text under a prefix byte no identifier can
   * begin with, so that a rule and a zone spelled the same cannot collide.
   * A rule longer than the stack buffer gets a heap key rather than being
   * left uncached, because an uncached zone still has to be owned by
   * somebody and "owned by nobody" is the leak.
   */
  if (length + 2 > sizeof(stack_key)) {
    key = (char *)gcu_allocator_malloc(db->allocator, length + 2);
    if (key == NULL) {
      return GCHRON_ERR_OOM;
    }
  }
  key[0] = '\x01';
  memcpy(key + 1, rule, length + 1);

  db_lock(db);
  zone = cache_find(db, key);
  if (zone != NULL) {
    db_unlock(db);
    if (key != stack_key) {
      gcu_allocator_free(db->allocator, key);
    }
    *out = zone;
    return GCHRON_OK;
  }
  db_unlock(db);

  result = gchron_zone_build_posix(rule, length, NULL, db->allocator, &zone);
  if (result == GCHRON_OK) {
    db_lock(db);
    result = cache_add(db, key, zone);
    db_unlock(db);
    if (result != GCHRON_OK) {
      gchron_zone_free(zone);
    }
    else {
      *out = zone;
    }
  }
  if (key != stack_key) {
    gcu_allocator_free(db->allocator, key);
  }
  return result;
}

GCHRON_Result gchron_zonedb_utc(GCHRON_ZoneDb * db, const GCHRON_Zone ** out) {
  return gchron_zonedb_fixed(db, 0, out);
}

/*--------------------------------------------------------------------------*
 * What the data says about itself
 *--------------------------------------------------------------------------*/

GCHRON_ZoneSource gchron_zonedb_source(const GCHRON_ZoneDb * db) {
  return db == NULL ? GCHRON_ZONE_SOURCE_NONE : db->source;
}

const char * gchron_zonedb_version(const GCHRON_ZoneDb * db) {
  return db == NULL ? NULL : db->version;
}

/** The name of a source, for gchron_zonedb_dump(). */
static const char * source_string(GCHRON_ZoneSource source) {
  switch (source) {
    case GCHRON_ZONE_SOURCE_SYSTEM: return "system";
    case GCHRON_ZONE_SOURCE_EMBEDDED: return "embedded";
    case GCHRON_ZONE_SOURCE_DIRECTORY: return "directory";
    case GCHRON_ZONE_SOURCE_MEMORY: return "memory";
    case GCHRON_ZONE_SOURCE_FIXED: return "fixed";
    case GCHRON_ZONE_SOURCE_NONE:
    default: return "none";
  }
}

/*--------------------------------------------------------------------------*
 * The seam local.c reaches the database through
 *
 * Declared in local.c rather than in zone_internal.h: nothing outside that
 * one file has any business reaching into a database's cache, and a private
 * header is an invitation for a third file to start.
 *--------------------------------------------------------------------------*/

const char * gchron_zonedb_directory_of(const GCHRON_ZoneDb * db) {
  return db == NULL ? NULL : db->directory;
}

const GCHRON_Allocator * gchron_zonedb_allocator_of(const GCHRON_ZoneDb * db) {
  return db == NULL ? NULL : db->allocator;
}

const GCHRON_Limits * gchron_zonedb_limits_of(const GCHRON_ZoneDb * db) {
  return db == NULL ? NULL : &db->limits;
}

/** Take ownership of a zone, filing it under a key no identifier can be. */
GCHRON_Result gchron_zonedb_intern(GCHRON_ZoneDb * db, const char * key,
    GCHRON_Zone * zone, const GCHRON_Zone ** out) {
  GCHRON_Zone * existing;
  GCHRON_Result result;

  if (db == NULL || key == NULL || zone == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  db_lock(db);
  existing = cache_find(db, key);
  if (existing != NULL) {
    /* Somebody else interned one first. The caller frees theirs. */
    db_unlock(db);
    *out = existing;
    return GCHRON_ERR_LIMIT;
  }
  result = cache_add(db, key, zone);
  db_unlock(db);
  if (result != GCHRON_OK) {
    return result;
  }
  *out = zone;
  return GCHRON_OK;
}

/** Append one identifier to the database's list. The caller holds the lock. */
static GCHRON_Result collect_one(GCHRON_ZoneDb * db, const char * id) {
  size_t bytes;
  char ** grown;

  if (!gcu_safe_mul_size(db->id_count + 1, sizeof(char *), &bytes)) {
    return GCHRON_ERR_OOM;
  }
  grown = (char **)gcu_allocator_realloc(db->allocator, db->ids, bytes);
  if (grown == NULL) {
    return GCHRON_ERR_OOM;
  }
  db->ids = grown;
  db->ids[db->id_count] = dup_string(db->allocator, id);
  if (db->ids[db->id_count] == NULL) {
    return GCHRON_ERR_OOM;
  }
  db->id_count += 1;
  return GCHRON_OK;
}

/** Order two identifiers, for qsort. */
static int compare_ids(const void * a, const void * b) {
  return strcmp(*(const char * const *)a, *(const char * const *)b);
}

GCHRON_Result gchron_zonedb_ids_of(GCHRON_ZoneDb * db,
    const char * const ** out_ids, size_t * out_count) {
  GCHRON_Result result = GCHRON_OK;

  db_lock(db);
  if (!db->ids_built) {
    if (db->source == GCHRON_ZONE_SOURCE_EMBEDDED) {
      /*
       * Already sorted, and already the whole list: the generator writes the
       * table in order so that a lookup can binary-search it, and listing is
       * the same order for free.
       */
      size_t i;
      size_t count = gchron_tzdata_embedded_count();
      for (i = 0; i < count && result == GCHRON_OK; ++i) {
        const GCHRON_EmbeddedZone * entry = gchron_tzdata_embedded_at(i);
        result = collect_one(db, entry->id);
      }
      if (result == GCHRON_OK) {
        db->ids_built = true;
      }
      db_unlock(db);
      if (result != GCHRON_OK) {
        return result;
      }
      if (out_ids != NULL) {
        *out_ids = (const char * const *)db->ids;
      }
      if (out_count != NULL) {
        *out_count = db->id_count;
      }
      return GCHRON_OK;
    }
    if (db->directory == NULL) {
      db_unlock(db);
      return GCHRON_ERR_UNSUPPORTED;
    }
    result = gchron_zonedb_walk_directory(db, db->directory, collect_one);
    if (result == GCHRON_OK) {
      /*
       * Then the link names the tzdb defines but this directory has no file
       * for. A lookup resolves them, so a listing that left them out would
       * mean `list` and `zone` disagreed about what the database contains -
       * and the listing is what a caller enumerates to build a picker.
       */
      size_t i;
      if (!db->links_built) {
        build_links(db);
      }
      for (i = 0; i < db->link_count && result == GCHRON_OK; ++i) {
        size_t j;
        bool already = false;
        for (j = 0; j < db->id_count; ++j) {
          if (strcmp(db->ids[j], db->link_names[i]) == 0) {
            already = true;
            break;
          }
        }
        if (!already) {
          result = collect_one(db, db->link_names[i]);
        }
      }
    }
    if (result == GCHRON_OK) {
      /* Sorted, so that a listing is stable across filesystems: readdir
       * returns entries in whatever order the directory happens to hold, and
       * a test that compared two listings would otherwise fail on a
       * different machine for no reason. */
      /*
       * Guarded, because a directory holding no zones never allocates the
       * array at all and `db->ids` is still null here. `qsort` declares its
       * base `__nonnull`, so the call is undefined whatever the count says -
       * glibc returns immediately on a count of zero and nothing misbehaves,
       * which is why this survived until a fuzzer with UBSan asked. The
       * sibling `bsearch` over `link_names` has carried the same guard since
       * it was written; this one was simply missed.
       */
      if (db->id_count > 0) {
        qsort(db->ids, db->id_count, sizeof(char *), compare_ids);
      }
      db->ids_built = true;
    }
  }
  db_unlock(db);
  if (result != GCHRON_OK) {
    return result;
  }
  if (out_ids != NULL) {
    *out_ids = (const char * const *)db->ids;
  }
  if (out_count != NULL) {
    *out_count = db->id_count;
  }
  return GCHRON_OK;
}

void gchron_zonedb_dump(const GCHRON_ZoneDb * db, FILE * stream) {
  if (stream == NULL) {
    return;
  }
  if (db == NULL) {
    fprintf(stream, "GCHRON_ZoneDb(NULL)\n");
    return;
  }
  /*
   * The source and the version, both printed, because "which rules produced
   * this timestamp" is an audit question and a fallback that cannot be seen
   * is the defect CONVENTIONS.md section 1 names (design.md section 6.6).
   */
  fprintf(stream, "GCHRON_ZoneDb(source=%s version=%s)\n",
      source_string(db->source), db->version ? db->version : "<unknown>");
  if (db->directory != NULL) {
    fprintf(stream, "  directory: %s\n", db->directory);
  }
  fprintf(stream, "  %zu zones cached, limit %zu\n", db->entry_count,
      db->limits.max_zones);
}
