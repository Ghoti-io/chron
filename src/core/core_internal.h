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
 * Declarations shared between the library's translation units and installed
 * with none of them.
 */

#ifndef GHOTI_IO_GCHRON_SRC_CORE_CORE_INTERNAL_H
#define GHOTI_IO_GCHRON_SRC_CORE_CORE_INTERNAL_H

#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Overflow-checked signed 64-bit arithmetic.
 *
 * cutil's safemath.h is the suite's home for this and is what design.md
 * section 7.1 names, but it covers `size_t`, `uint32_t` and `uint64_t` only -
 * every quantity in this library is a *signed* 64-bit count, and signed
 * overflow is undefined behaviour rather than a wrap that a portable check
 * could observe after the fact. These are the same shape as cutil's, with the
 * same contract: `false` means the operation would overflow, and `*out` is
 * untouched.
 *
 * They are `static inline` in a private header so that every arithmetic site
 * compiles to the compiler's overflow instruction plus a branch. Nothing here
 * is exported; a caller who wants checked arithmetic gets it through the
 * GCHRON_ERR_RANGE that every public operation returns.
 */

#if defined(__GNUC__) || defined(__clang__)
#define GCHRON_HAS_BUILTIN_OVERFLOW 1
#endif

/**
 * Add two signed 64-bit values, detecting overflow.
 *
 * @param a The first addend.
 * @param b The second addend.
 * @param out Receives `a + b` on success; untouched on overflow.
 * @return `true` when the sum is representable.
 */
static inline bool gchron_add_i64(int64_t a, int64_t b, int64_t * out) {
#ifdef GCHRON_HAS_BUILTIN_OVERFLOW
  int64_t tmp;
  if (__builtin_add_overflow(a, b, &tmp)) {
    return false;
  }
  *out = tmp;
  return true;
#else
  if (b > 0 && a > INT64_MAX - b) {
    return false;
  }
  if (b < 0 && a < INT64_MIN - b) {
    return false;
  }
  *out = a + b;
  return true;
#endif
}

/**
 * Subtract two signed 64-bit values, detecting overflow.
 *
 * @param a The minuend.
 * @param b The subtrahend.
 * @param out Receives `a - b` on success; untouched on overflow.
 * @return `true` when the difference is representable.
 */
static inline bool gchron_sub_i64(int64_t a, int64_t b, int64_t * out) {
#ifdef GCHRON_HAS_BUILTIN_OVERFLOW
  int64_t tmp;
  if (__builtin_sub_overflow(a, b, &tmp)) {
    return false;
  }
  *out = tmp;
  return true;
#else
  if (b < 0 && a > INT64_MAX + b) {
    return false;
  }
  if (b > 0 && a < INT64_MIN + b) {
    return false;
  }
  *out = a - b;
  return true;
#endif
}

/**
 * Multiply two signed 64-bit values, detecting overflow.
 *
 * @param a The first factor.
 * @param b The second factor.
 * @param out Receives `a * b` on success; untouched on overflow.
 * @return `true` when the product is representable.
 */
static inline bool gchron_mul_i64(int64_t a, int64_t b, int64_t * out) {
#ifdef GCHRON_HAS_BUILTIN_OVERFLOW
  int64_t tmp;
  if (__builtin_mul_overflow(a, b, &tmp)) {
    return false;
  }
  *out = tmp;
  return true;
#else
  if (a == 0 || b == 0) {
    *out = 0;
    return true;
  }
  if (a == -1) {
    if (b == INT64_MIN) {
      return false;
    }
    *out = -b;
    return true;
  }
  if (b == -1) {
    if (a == INT64_MIN) {
      return false;
    }
    *out = -a;
    return true;
  }
  if (a > 0 ? (b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a)
            : (b > 0 ? a < INT64_MIN / b : a < INT64_MAX / b)) {
    return false;
  }
  *out = a * b;
  return true;
#endif
}

/**
 * Negate a signed 64-bit value, detecting the one case that overflows.
 *
 * @param a The value.
 * @param out Receives `-a` on success; untouched on overflow.
 * @return `true` unless @p a is `INT64_MIN`.
 */
static inline bool gchron_neg_i64(int64_t a, int64_t * out) {
  if (a == INT64_MIN) {
    return false;
  }
  *out = -a;
  return true;
}

/**
 * Divide, rounding towards negative infinity.
 *
 * C's `/` truncates towards zero, which splits `-1 / 86400` into day 0 rather
 * than day -1 and puts every pre-epoch instant on the wrong side of midnight.
 * Every day and month division in this library goes through here.
 *
 * @param a The dividend.
 * @param b The divisor; must not be 0 or -1 with @p a at `INT64_MIN`.
 * @return The floor of `a / b`.
 */
static inline int64_t gchron_floor_div(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) {
    q -= 1;
  }
  return q;
}

/**
 * The remainder that goes with gchron_floor_div(): always has the sign of the
 * divisor, so a positive divisor gives a non-negative result.
 *
 * @param a The dividend.
 * @param b The divisor.
 * @return `a - gchron_floor_div(a, b) * b`.
 */
static inline int64_t gchron_floor_mod(int64_t a, int64_t b) {
  int64_t r = a % b;
  if (r != 0 && ((r < 0) != (b < 0))) {
    r += b;
  }
  return r;
}

/**
 * Record a failure in a caller's error structure, if they wanted one.
 *
 * @param error Where to record it. NULL is ignored, which is what makes every
 *   `GCHRON_Error *` parameter optional.
 * @param code The result being returned.
 * @param diag Which of that code's causes this is.
 * @param offset Byte offset into the input.
 * @param length Bytes to underline from @p offset.
 * @return @p code, so that a caller can `return gchron_fail(...)`.
 */
GCHRON_Result gchron_fail(GCHRON_Error * error, GCHRON_Result code,
    GCHRON_Diag diag, size_t offset, size_t length);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_SRC_CORE_CORE_INTERNAL_H
