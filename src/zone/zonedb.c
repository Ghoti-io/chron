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
 *
 * Copyright 2026 by Corey Pennycuff
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
#include <ghoti.io/cutil/mutex.h>
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
  FILE * handle;
  long size;
  void * buffer;
  size_t read_bytes;

  handle = fopen(path, "rb");
  if (handle == NULL) {
    return GCHRON_ERR_IO;
  }
  if (fseek(handle, 0, SEEK_END) != 0) {
    fclose(handle);
    return GCHRON_ERR_IO;
  }
  size = ftell(handle);
  if (size < 0) {
    fclose(handle);
    return GCHRON_ERR_IO;
  }
  if (max_bytes != 0 && (size_t)size > max_bytes) {
    /* Checked before the allocation, not after the read: the size is the
     * whole of what an attacker controls here. */
    fclose(handle);
    return GCHRON_ERR_LIMIT;
  }
  if (fseek(handle, 0, SEEK_SET) != 0) {
    fclose(handle);
    return GCHRON_ERR_IO;
  }

  buffer = gcu_allocator_malloc(allocator, (size_t)size + 1);
  if (buffer == NULL) {
    fclose(handle);
    return GCHRON_ERR_OOM;
  }
  read_bytes = fread(buffer, 1, (size_t)size, handle);
  fclose(handle);
  if (read_bytes != (size_t)size) {
    gcu_allocator_free(allocator, buffer);
    return GCHRON_ERR_IO;
  }
  ((char *)buffer)[size] = '\0';
  *out_data = buffer;
  *out_len = (size_t)size;
  return GCHRON_OK;
}

/** Build `<directory>/<id>`. */
static char * join_path(const GCHRON_Allocator * allocator,
    const char * directory, const char * id) {
  size_t dir_len = strlen(directory);
  size_t id_len = strlen(id);
  size_t total;
  char * path;

  if (!gcu_safe_add_size(dir_len, id_len, &total)
      || !gcu_safe_add_size(total, 2, &total)) {
    return NULL;
  }
  path = (char *)gcu_allocator_malloc(allocator, total);
  if (path == NULL) {
    return NULL;
  }
  memcpy(path, directory, dir_len);
  path[dir_len] = '/';
  memcpy(path + dir_len + 1, id, id_len);
  path[dir_len + 1 + id_len] = '\0';
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
   * route, and phase 4 builds its table. See WINDOWS-TODO.md. */
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
    /* No such file. The zone is not in this database, which is a different
     * thing from the file being unreadable - but the C library does not tell
     * the two apart through fopen alone, and UNSUPPORTED is the answer a
     * caller acts on. */
    return GCHRON_ERR_UNSUPPORTED;
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
  if (db->source == GCHRON_ZONE_SOURCE_EMBEDDED) {
    const GCHRON_EmbeddedZone * entry = gchron_tzdata_embedded_find(id);
    if (entry != NULL && entry->canonical != NULL) {
      char * canonical = dup_string(db->allocator, entry->canonical);
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
      /* Sorted, so that a listing is stable across filesystems: readdir
       * returns entries in whatever order the directory happens to hold, and
       * a test that compared two listings would otherwise fail on a
       * different machine for no reason. */
      qsort(db->ids, db->id_count, sizeof(char *), compare_ids);
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
