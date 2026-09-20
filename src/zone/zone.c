/**
 * @file
 *
 * What a zone answers: the offset at an instant, the changes either side of
 * it, and what instants a wall-clock reading could have named.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/zone.h>
#include <stdio.h>
#include <string.h>

#include "../core/core_internal.h"
#include "zone_internal.h"

const char * gchron_zone_id(const GCHRON_Zone * zone) {
  return zone == NULL ? NULL : zone->id;
}

const char * gchron_zone_canonical_id(const GCHRON_Zone * zone) {
  return zone == NULL ? NULL : zone->canonical_id;
}

bool gchron_zone_is_fixed(const GCHRON_Zone * zone) {
  return zone != NULL && zone->is_fixed;
}

/** Fill in a GCHRON_ZoneInfo from one of a zone's local-time types. */
static void info_from_type(const GCHRON_Zone * zone, size_t index,
    GCHRON_ZoneInfo * out) {
  out->offset_sec = zone->types[index].utoff;
  out->is_dst = zone->types[index].is_dst;
  out->abbreviation = zone->types[index].abbrev;
}

/**
 * The index of the last transition at or before @p instant.
 *
 * @return The index, or -1 when @p instant precedes every transition.
 */
static long find_transition(const GCHRON_Zone * zone, int64_t instant) {
  size_t low = 0;
  size_t high = zone->transition_count;

  /*
   * A binary search, not a scan. A zone's table runs to a few hundred entries
   * and a formatter asks this question once per timestamp; a linear scan is
   * how a log-rotation loop comes to spend its time in the time library.
   * The table is strictly ascending - the reader refuses one that is not -
   * so the search is sound.
   */
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (zone->transition_at[mid] <= instant) {
      low = mid + 1;
    }
    else {
      high = mid;
    }
  }
  return (long)low - 1;
}

/**
 * Whether the POSIX TZ footer, rather than the table, governs an instant.
 *
 * RFC 8536 section 3.3: the footer describes the rule for every instant after
 * the last listed transition. With no transitions at all it describes them
 * all, which is what a zone built from a bare `TZ` string looks like.
 */
static bool rule_governs(const GCHRON_Zone * zone, int64_t instant) {
  if (!zone->has_rule) {
    return false;
  }
  if (zone->transition_count == 0) {
    return true;
  }
  return instant >= zone->transition_at[zone->transition_count - 1];
}

GCHRON_Result gchron_zone_offset_at(const GCHRON_Zone * zone,
    GCHRON_Instant instant, GCHRON_ZoneInfo * out) {
  long index;

  if (zone == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (rule_governs(zone, instant.sec)) {
    return gchron_posixtz_offset_at(&zone->rule, instant.sec, out);
  }
  index = find_transition(zone, instant.sec);
  if (index < 0) {
    /* Before the zone's records begin. RFC 8536 section 4's rule, resolved
     * once at load: the first type that is not a daylight-saving one. */
    info_from_type(zone, zone->first_type, out);
    return GCHRON_OK;
  }
  info_from_type(zone, zone->transition_type[index], out);
  return GCHRON_OK;
}

/** What was in force immediately before a table entry. */
static void info_before_index(const GCHRON_Zone * zone, size_t index,
    GCHRON_ZoneInfo * out) {
  if (index == 0) {
    info_from_type(zone, zone->first_type, out);
  }
  else {
    info_from_type(zone, zone->transition_type[index - 1], out);
  }
}

GCHRON_Result gchron_zone_next_transition(const GCHRON_Zone * zone,
    GCHRON_Instant after, GCHRON_Transition * out) {
  long index;
  size_t next;

  if (zone == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (zone->is_fixed) {
    return GCHRON_ERR_UNSUPPORTED;
  }

  index = find_transition(zone, after.sec);
  next = (size_t)(index + 1);
  if (next < zone->transition_count) {
    out->at.sec = zone->transition_at[next];
    out->at.nsec = 0;
    info_before_index(zone, next, &out->before);
    info_from_type(zone, zone->transition_type[next], &out->after);
    return GCHRON_OK;
  }

  if (zone->has_rule) {
    int64_t at = 0;
    GCHRON_Result result = gchron_posixtz_next_transition(&zone->rule,
        after.sec, &at, &out->before, &out->after);
    if (result != GCHRON_OK) {
      return result;
    }
    out->at.sec = at;
    out->at.nsec = 0;
    return GCHRON_OK;
  }

  /*
   * The table has run out and there is no footer. A "fat" file without one
   * stops changing after its last entry, which is a real answer - this zone
   * has no next transition - and not an error in the data.
   */
  return GCHRON_ERR_UNSUPPORTED;
}

GCHRON_Result gchron_zone_prev_transition(const GCHRON_Zone * zone,
    GCHRON_Instant before, GCHRON_Transition * out) {
  long index;

  if (zone == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (zone->is_fixed) {
    return GCHRON_ERR_UNSUPPORTED;
  }

  if (rule_governs(zone, before.sec) && zone->has_rule) {
    int64_t at = 0;
    GCHRON_Result result = gchron_posixtz_prev_transition(&zone->rule,
        before.sec, &at, &out->before, &out->after);
    if (result == GCHRON_OK
        && (zone->transition_count == 0
            || at >= zone->transition_at[zone->transition_count - 1])) {
      out->at.sec = at;
      out->at.nsec = 0;
      return GCHRON_OK;
    }
    /* The rule's own previous change falls before the table ends, so the
     * table is the authority for it. Fall through. */
  }

  index = find_transition(zone, before.sec);
  if (index < 0) {
    return GCHRON_ERR_UNSUPPORTED;
  }
  out->at.sec = zone->transition_at[index];
  out->at.nsec = 0;
  info_before_index(zone, (size_t)index, &out->before);
  info_from_type(zone, zone->transition_type[index], &out->after);
  return GCHRON_OK;
}

GCHRON_Result gchron_zone_offsets_for_civil(const GCHRON_Zone * zone,
    GCHRON_DateTime civil, GCHRON_CivilOffsets * out) {
  GCHRON_Instant as_utc;
  GCHRON_Result result;
  int64_t local;
  GCHRON_ZoneInfo candidates[2];
  int64_t instants[2];
  int count = 0;
  int index;
  GCHRON_Transition surrounding;
  bool have_transition = false;

  if (zone == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  result = gchron_instant_from_utc(&civil, &as_utc);
  if (result != GCHRON_OK) {
    return result;
  }
  local = as_utc.sec;

  memset(out, 0, sizeof(*out));

  /*
   * The standard two-probe method. A wall-clock reading `local` names the
   * instant `local - offset` for whatever offset was in force then, and the
   * offset in force depends on the instant - so the question is circular and
   * is broken by trying both of the offsets that could plausibly apply.
   *
   * Probing with the largest and smallest offsets the zone uses near that
   * moment would need a search; probing either side of the reading by a day
   * is what every implementation does, because no transition moves a clock by
   * anywhere near that much.
   */
  {
    int32_t probes[2];
    GCHRON_ZoneInfo before_info;
    GCHRON_ZoneInfo after_info;
    GCHRON_Instant probe;
    int i;

    probe.nsec = 0;
    probe.sec = local - GCHRON_SECONDS_PER_DAY;
    if (gchron_zone_offset_at(zone, probe, &before_info) != GCHRON_OK) {
      return GCHRON_ERR_RANGE;
    }
    probe.sec = local + GCHRON_SECONDS_PER_DAY;
    if (gchron_zone_offset_at(zone, probe, &after_info) != GCHRON_OK) {
      return GCHRON_ERR_RANGE;
    }
    probes[0] = before_info.offset_sec;
    probes[1] = after_info.offset_sec;

    for (i = 0; i < 2; ++i) {
      GCHRON_Instant candidate;
      GCHRON_ZoneInfo actual;
      int64_t sec;

      if (i == 1 && probes[1] == probes[0]) {
        break;
      }
      if (!gchron_sub_i64(local, probes[i], &sec)) {
        return GCHRON_ERR_RANGE;
      }
      candidate.sec = sec;
      candidate.nsec = 0;
      if (gchron_zone_offset_at(zone, candidate, &actual) != GCHRON_OK) {
        return GCHRON_ERR_RANGE;
      }
      /*
       * The probe is only a candidate if the zone agrees: `local - offset`
       * has to be an instant at which the zone really was on `offset`. A
       * reading in a gap satisfies neither probe, which is how a gap is
       * detected rather than guessed at.
       */
      if (actual.offset_sec == probes[i]) {
        candidates[count] = actual;
        instants[count] = sec;
        count += 1;
      }
    }
  }

  /* Earliest first, so that GCHRON_RESOLVE_EARLIER can take index 0. */
  if (count == 2 && instants[0] > instants[1]) {
    GCHRON_ZoneInfo tmp_info = candidates[0];
    int64_t tmp_at = instants[0];
    candidates[0] = candidates[1];
    instants[0] = instants[1];
    candidates[1] = tmp_info;
    instants[1] = tmp_at;
  }

  out->count = count;
  for (index = 0; index < count; ++index) {
    out->options[index] = candidates[index];
    out->instants[index].sec = instants[index];
    out->instants[index].nsec = civil.time.nsec;
  }

  if (count != 1) {
    /*
     * Which transition caused it, and for a gap how long the gap is - which
     * is what GCHRON_RESOLVE_COMPATIBLE pushes a reading forward by, and what
     * a calendar interface wants to say out loud.
     */
    GCHRON_Instant around;
    around.sec = local - GCHRON_SECONDS_PER_DAY;
    around.nsec = 0;
    if (gchron_zone_next_transition(zone, around, &surrounding) == GCHRON_OK) {
      have_transition = true;
    }
    if (have_transition) {
      out->transition = surrounding.at;
      if (count == 0) {
        int64_t shift = (int64_t)surrounding.after.offset_sec
            - (int64_t)surrounding.before.offset_sec;
        out->gap_seconds = (int32_t)(shift > 0 ? shift : -shift);
      }
    }
  }
  return GCHRON_OK;
}

void gchron_zone_dump(const GCHRON_Zone * zone, FILE * stream) {
  size_t i;

  if (stream == NULL) {
    return;
  }
  if (zone == NULL) {
    fprintf(stream, "GCHRON_Zone(NULL)\n");
    return;
  }
  fprintf(stream, "GCHRON_Zone(%s", zone->id ? zone->id : "<anonymous>");
  if (zone->canonical_id != NULL && zone->id != NULL
      && strcmp(zone->canonical_id, zone->id) != 0) {
    fprintf(stream, " -> %s", zone->canonical_id);
  }
  fprintf(stream, ")\n");
  fprintf(stream, "  %s, %zu transitions, %zu types\n",
      zone->is_fixed ? "fixed" : "varying", zone->transition_count,
      zone->type_count);
  for (i = 0; i < zone->type_count; ++i) {
    fprintf(stream, "    type %zu: %+d %s %s\n", i, zone->types[i].utoff,
        zone->types[i].is_dst ? "dst" : "std", zone->types[i].abbrev);
  }
  if (zone->has_rule) {
    fprintf(stream, "  rule: %s%+d", zone->rule.std_abbrev,
        zone->rule.std_offset);
    if (zone->rule.has_dst) {
      fprintf(stream, " / %s%+d", zone->rule.dst_abbrev,
          zone->rule.dst_offset);
    }
    fprintf(stream, "\n");
  }
  else {
    fprintf(stream, "  rule: none (the table is the whole of it)\n");
  }
}

GCHRON_Result gchron_zone_id_from_windows(const char * windows_name,
    const char ** out) {
  const char * id;

  if (windows_name == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (gchron_windows_zones_count() == 0) {
    /*
     * No table was built in. Distinct from "this table does not carry that
     * name", because this one has a fix the caller can carry out:
     * tools/tzdata/fetch-cldr.sh, then tools/tzdata/windows_zones.py.
     */
    return GCHRON_ERR_UNSUPPORTED;
  }
  id = gchron_windows_zones_lookup(windows_name);
  if (id == NULL) {
    return GCHRON_ERR_RANGE;
  }
  *out = id;
  return GCHRON_OK;
}

const char * gchron_zone_windows_mapping_version(void) {
  return gchron_windows_zones_version();
}

size_t gchron_zone_windows_mapping_count(void) {
  return gchron_windows_zones_count();
}
