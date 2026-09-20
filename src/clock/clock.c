/**
 * @file
 *
 * The one file in this library that reads a clock.
 *
 * Nothing else calls it. design.md, mistake M18: "now" called from inside a
 * library is what makes two-digit-year rules, certificate-validity checks and
 * anything else that depends on the date untestable. Here it is a parameter.
 *
 * Copyright 2026 by Corey Pennycuff
 */

/* clock_gettime() and CLOCK_MONOTONIC are POSIX, and this library compiles
 * with -std=c17, which declares neither. */
#define _POSIX_C_SOURCE 200809L

#include <ghoti.io/chron/clock.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <stddef.h>

#include "../core/core_internal.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

#if defined(_WIN32)
/*
 * TODO(windows): none of this branch has been run. GetSystemTimePreciseAsFileTime
 * is Windows 8 and later; QueryPerformanceCounter's frequency is fixed at
 * boot. See WINDOWS-TODO.md, where "done" means gchron_clock_now() on the
 * system clock agrees with the shell's own time and gchron_tick_now() is
 * monotonic across a clock change.
 */

/** Hundreds of nanoseconds between 1601-01-01 and 1970-01-01. */
#define FILETIME_EPOCH_OFFSET INT64_C(116444736000000000)

static GCHRON_Result system_now(const GCHRON_Clock * self,
    GCHRON_Instant * out) {
  FILETIME file_time;
  ULARGE_INTEGER value;
  int64_t hundred_nanos;

  (void)self;
  GetSystemTimePreciseAsFileTime(&file_time);
  value.LowPart = file_time.dwLowDateTime;
  value.HighPart = file_time.dwHighDateTime;
  hundred_nanos = (int64_t)value.QuadPart - FILETIME_EPOCH_OFFSET;
  out->sec = gchron_floor_div(hundred_nanos, INT64_C(10000000));
  out->nsec =
      (int32_t)(gchron_floor_mod(hundred_nanos, INT64_C(10000000)) * 100);
  return GCHRON_OK;
}

static GCHRON_Result system_resolution(const GCHRON_Clock * self,
    GCHRON_Duration * out) {
  (void)self;
  /* GetSystemTimePreciseAsFileTime resolves to the FILETIME tick. */
  return gchron_duration_from_exact_seconds(0, 100, out);
}

GCHRON_Result gchron_tick_now(GCHRON_Tick * out) {
  LARGE_INTEGER counter;
  static LARGE_INTEGER frequency;
  static int have_frequency = 0;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!have_frequency) {
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart == 0) {
      return GCHRON_ERR_IO;
    }
    have_frequency = 1;
  }
  if (!QueryPerformanceCounter(&counter)) {
    return GCHRON_ERR_IO;
  }
  /* Scaled in two steps so that the product does not overflow: the counter
   * reaches the billions and a nanosecond scale would multiply it by 1e9. */
  out->nsec = (counter.QuadPart / frequency.QuadPart) * INT64_C(1000000000)
      + ((counter.QuadPart % frequency.QuadPart) * INT64_C(1000000000))
          / frequency.QuadPart;
  return GCHRON_OK;
}

#else

static GCHRON_Result system_now(const GCHRON_Clock * self,
    GCHRON_Instant * out) {
  struct timespec ts;

  (void)self;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return GCHRON_ERR_IO;
  }
  /*
   * The kernel's own normalisation already puts tv_nsec in [0, 1e9), with the
   * sign in tv_sec, which is the same rule GCHRON_Instant uses - so this is a
   * copy rather than a conversion. design.md section 5.1: what comes back is
   * Unix time, leap seconds not counted, and a library whose instant
   * disagreed with this by thirty-seven seconds would be right in a way that
   * makes every conversion at the operating-system boundary wrong.
   */
  out->sec = (int64_t)ts.tv_sec;
  out->nsec = (int32_t)ts.tv_nsec;
  return GCHRON_OK;
}

static GCHRON_Result system_resolution(const GCHRON_Clock * self,
    GCHRON_Duration * out) {
  struct timespec ts;

  (void)self;
  if (clock_getres(CLOCK_REALTIME, &ts) != 0) {
    return GCHRON_ERR_IO;
  }
  return gchron_duration_from_exact_seconds((int64_t)ts.tv_sec,
      (int32_t)ts.tv_nsec, out);
}

GCHRON_Result gchron_tick_now(GCHRON_Tick * out) {
  struct timespec ts;
  int64_t nanos;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return GCHRON_ERR_IO;
  }
  if (!gchron_mul_i64((int64_t)ts.tv_sec, GCHRON_NANOS_PER_SECOND, &nanos)
      || !gchron_add_i64(nanos, (int64_t)ts.tv_nsec, &nanos)) {
    return GCHRON_ERR_RANGE;
  }
  out->nsec = nanos;
  return GCHRON_OK;
}

#endif

static const GCHRON_Clock SYSTEM_CLOCK = {
  system_now,
  system_resolution,
  NULL
};

const GCHRON_Clock * gchron_clock_system(void) {
  return &SYSTEM_CLOCK;
}

/** What a fixed clock reports: the instant beside it in its own struct. */
static GCHRON_Result fixed_now(const GCHRON_Clock * self,
    GCHRON_Instant * out) {
  const GCHRON_FixedClock * fixed = (const GCHRON_FixedClock *)self->ctx;
  *out = fixed->instant;
  return GCHRON_OK;
}

/** A clock that does not move resolves infinitely finely, and says so. */
static GCHRON_Result fixed_resolution(const GCHRON_Clock * self,
    GCHRON_Duration * out) {
  (void)self;
  return gchron_duration_from_exact_seconds(0, 1, out);
}

GCHRON_Result gchron_clock_fixed(GCHRON_Instant at, GCHRON_FixedClock * out) {
  if (out == NULL || !gchron_instant_is_valid(&at)) {
    return GCHRON_ERR_INVALID;
  }
  out->instant = at;
  out->clock.now = fixed_now;
  out->clock.resolution = fixed_resolution;
  /* Points at its own containing struct, so the clock carries its instant
   * without an allocation and without a global. */
  out->clock.ctx = out;
  return GCHRON_OK;
}

GCHRON_Result gchron_clock_now(const GCHRON_Clock * clock,
    GCHRON_Instant * out) {
  if (clock == NULL || out == NULL || clock->now == NULL) {
    /* Not a silent fall back to the system clock. A caller who meant the
     * system clock says so, and mistake M18 is exactly the library that
     * decided for them. */
    return GCHRON_ERR_INVALID;
  }
  return clock->now(clock, out);
}

GCHRON_Result gchron_clock_resolution(const GCHRON_Clock * clock,
    GCHRON_Duration * out) {
  if (clock == NULL || out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (clock->resolution == NULL) {
    return GCHRON_ERR_UNSUPPORTED;
  }
  return clock->resolution(clock, out);
}

GCHRON_Result gchron_tick_since(GCHRON_Tick from, GCHRON_Tick to,
    GCHRON_Duration * out) {
  int64_t nanos;

  if (out == NULL) {
    return GCHRON_ERR_INVALID;
  }
  if (!gchron_sub_i64(to.nsec, from.nsec, &nanos)) {
    return GCHRON_ERR_RANGE;
  }
  return gchron_duration_from_exact_seconds(
      gchron_floor_div(nanos, GCHRON_NANOS_PER_SECOND),
      (int32_t)gchron_floor_mod(nanos, GCHRON_NANOS_PER_SECOND), out);
}
