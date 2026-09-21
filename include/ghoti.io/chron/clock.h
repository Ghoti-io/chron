/**
 * @file
 *
 * Clocks: "now", as a parameter rather than a call.
 *
 * Tier 1 (design.md section 3). **Nothing inside this library calls
 * gchron_clock_system().** The two places a "now" is needed - RFC 850's
 * two-digit-year rule, and a caller's own - take a GCHRON_Clock, so that a
 * test can pin the date and the behaviour on 31 December 2049 can be checked
 * in 2026 (design.md, mistake M18).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_CLOCK_H
#define GHOTI_IO_GCHRON_CLOCK_H

#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/duration.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Forward declaration, so the vtable can name its own type. */
typedef struct GCHRON_Clock GCHRON_Clock;

/**
 * @brief A source of "now".
 *
 * A vtable rather than a function, so that a test can supply one that does
 * not move and an application can supply one that reads a simulation's own
 * time.
 */
struct GCHRON_Clock {
  /**
   * What time it is.
   *
   * @return GCHRON_OK, or GCHRON_ERR_IO when the underlying clock failed.
   */
  GCHRON_Result (*now)(const GCHRON_Clock * self, GCHRON_Instant * out);

  /**
   * How finely this clock resolves, as an exact duration. May be NULL, and
   * then gchron_clock_resolution() reports GCHRON_ERR_UNSUPPORTED.
   */
  GCHRON_Result (*resolution)(const GCHRON_Clock * self,
      GCHRON_Duration * out);

  /** Whatever the implementation needs. Ignored by this library. */
  void * ctx;
};

/**
 * @brief The operating system's wall clock.
 *
 * `CLOCK_REALTIME` on POSIX, `GetSystemTimePreciseAsFileTime` on Windows. It
 * can jump backwards when something sets the time, which is what
 * GCHRON_Tick exists instead of.
 *
 * @return A borrowed, immutable clock that lives for the life of the
 *   program. Never freed.
 */
GCHRON_API const GCHRON_Clock * gchron_clock_system(void);

/**
 * @brief A clock that does not move.
 *
 * What a test uses to pin the date. Embeds its own instant, so it allocates
 * nothing and the caller owns it:
 *
 * @code
 * GCHRON_FixedClock clock;
 * gchron_clock_fixed(moment, &clock);
 * gchron_parse_http_date(text, len, &clock.clock, NULL, &out, NULL, NULL);
 * @endcode
 */
typedef struct GCHRON_FixedClock {
  GCHRON_Clock clock;      ///< Pass `&fixed.clock` wherever a clock is taken.
  GCHRON_Instant instant;  ///< What it always says.
} GCHRON_FixedClock;

/**
 * @brief Initialise a clock that always reports one instant.
 *
 * @param at The instant it reports.
 * @param out The clock to fill in. The caller owns it and it must outlive
 *   every use of `&out->clock`.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_clock_fixed(GCHRON_Instant at,
    GCHRON_FixedClock * out);

/**
 * @brief Ask a clock what time it is.
 *
 * @param clock The clock. NULL is GCHRON_ERR_INVALID rather than a silent
 *   fall back to the system clock - a caller who meant the system clock says
 *   so, and mistake M18 is exactly the library that decided for them.
 * @param out Receives the instant on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_IO.
 */
GCHRON_API GCHRON_Result gchron_clock_now(const GCHRON_Clock * clock,
    GCHRON_Instant * out);

/**
 * @brief How finely a clock resolves.
 *
 * @param clock The clock.
 * @param out Receives an exact duration on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_UNSUPPORTED for a clock that does not say;
 *   GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_clock_resolution(const GCHRON_Clock * clock,
    GCHRON_Duration * out);

/**
 * @brief Which monotonic counter a reading came from.
 *
 * "Monotonic" answers only one question - the reading never goes backwards
 * when somebody sets the system clock. It leaves a second one open, and the
 * two answers are different clocks: **does it keep counting while the machine
 * is suspended?**
 *
 * Both are right, for different questions.
 *
 * - *How long did this take?* - a frame, a benchmark, an operation. If the
 *   lid closes in the middle, the eight hours are not part of the answer.
 *   That is ::GCHRON_TICK_SUSPENDING.
 * - *Has enough time passed?* - a cache entry, a session, a token, an idle
 *   timeout. If the lid closes for eight hours, an hour-long expiry has
 *   certainly expired. That is ::GCHRON_TICK_CONTINUOUS.
 *
 * The second is the one that fails silently. A timeout measured on a clock
 * that stops while the machine sleeps simply never fires, nothing crashes,
 * and the check quietly stops doing its job - which is why this library makes
 * the caller name the clock rather than picking one.
 */
typedef enum {
  /**
   * Not a reading.
   *
   * Zero, so that a zeroed GCHRON_Tick is refused by gchron_tick_since()
   * rather than being taken for a real reading of the first clock
   * (design.md section 3.7).
   */
  GCHRON_TICK_NONE = 0,

  /**
   * Stops while the machine is suspended.
   *
   * `CLOCK_MONOTONIC` on POSIX; `QueryUnbiasedInterruptTimePrecise` on
   * Windows, whose "unbiased" means precisely this - the sleep bias removed.
   */
  GCHRON_TICK_SUSPENDING,

  /**
   * Counts through it.
   *
   * `CLOCK_BOOTTIME` on Linux; `QueryInterruptTimePrecise` on Windows.
   *
   * There is no portable POSIX spelling: `CLOCK_BOOTTIME` is Linux's, and a
   * POSIX system without it reports GCHRON_ERR_UNSUPPORTED rather than
   * quietly answering with ::GCHRON_TICK_SUSPENDING instead. Falling back
   * would hand back a clock that stops while the machine sleeps, under the
   * name of one that does not, to the caller who asked for the difference -
   * which is the one mistake this enum exists to prevent.
   */
  GCHRON_TICK_CONTINUOUS
} GCHRON_TickSource;

/**
 * @brief A reading from a monotonic counter.
 *
 * **Not an instant**, and a separate type on purpose (design.md, mistake
 * M23). It has no epoch, it is not comparable across processes or reboots,
 * and only the difference between two of them means anything. Go's
 * `time.Time` carries an invisible monotonic component that changes what `==`
 * means, and this is what that costs.
 *
 * `cjelly` will use it for frame timing.
 */
typedef struct GCHRON_Tick {
  int64_t nsec;               ///< Nanoseconds from an unspecified origin.
  /**
   * Which counter produced it.
   *
   * Carried in the value rather than trusted to the caller, because the two
   * counters have different origins *and* different rates in any interval
   * containing a suspend. Subtracting one from the other is not a smaller
   * error than subtracting two instants from different epochs, and it is
   * exactly as easy to do by accident.
   */
  GCHRON_TickSource source;
} GCHRON_Tick;

/**
 * @brief Read a monotonic counter.
 *
 * Neither counter is a point on any calendar, and neither is affected by
 * anything that sets the wall clock.
 *
 * @param source Which counter to read. ::GCHRON_TICK_NONE is
 *   GCHRON_ERR_INVALID: there is no default, because the two answer different
 *   questions and guessing wrong is silent.
 * @param out Receives the reading on success; untouched on failure.
 * @return GCHRON_OK; GCHRON_ERR_INVALID; GCHRON_ERR_IO;
 *   GCHRON_ERR_UNSUPPORTED when this platform has no such counter.
 */
GCHRON_API GCHRON_Result gchron_tick_now(GCHRON_TickSource source,
    GCHRON_Tick * out);

/**
 * @brief How long passed between two readings.
 *
 * The only operation a GCHRON_Tick has. Comparing two of them from different
 * processes, or across a reboot, is meaningless - and there is deliberately
 * no function here that would let a caller do it by accident.
 *
 * Two readings from *different counters* are meaningless in the same way, and
 * are refused: GCHRON_ERR_INVALID. So is a reading whose source is
 * ::GCHRON_TICK_NONE, which is what a zeroed struct holds.
 *
 * @param from The earlier reading.
 * @param to The later reading.
 * @param out Receives an exact duration on success; untouched on failure.
 * @return GCHRON_OK, GCHRON_ERR_INVALID or GCHRON_ERR_RANGE.
 */
GCHRON_API GCHRON_Result gchron_tick_since(GCHRON_Tick from, GCHRON_Tick to,
    GCHRON_Duration * out);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_CLOCK_H
