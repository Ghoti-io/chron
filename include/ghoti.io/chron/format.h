/**
 * @file
 *
 * Formatting: turning a value into text a person reads.
 *
 * Tier 3 (design.md section 3): the first thing here that needs data beyond
 * the value itself - month names, day names, era names - and the last tier a
 * consumer has to opt into.
 *
 * A pattern is **compiled once** into a GCHRON_Format, immutable and
 * shareable, and applied many times - the same shape as `GRX_Regex` and for
 * the same reasons: the pattern is checked once, its errors are reported with
 * an offset, and a hostile pattern is bounded at compile time. In `ctang` a
 * pattern comes from a template and a template may come from a user, which is
 * why that matters here as much as it does in `regex`.
 *
 * Reference: Unicode TR35 part 4, *Dates* - the LDML pattern letters;
 * `strftime(3)`; RFC 9110 section 5.6.7 (HTTP-date); RFC 5322 sections 3.3
 * and 4.3.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_FORMAT_H
#define GHOTI_IO_GCHRON_FORMAT_H

#include <ghoti.io/chron/allocator.h>
#include <ghoti.io/chron/calendar.h>
#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/clock.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/instant.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/offset.h>
#include <ghoti.io/chron/parse.h>
#include <ghoti.io/chron/zoned.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------------*
 * Names
 *--------------------------------------------------------------------------*/

/** Forward declaration, so the vtable can name its own type. */
typedef struct GCHRON_Names GCHRON_Names;

/**
 * @brief How long a name a caller is asking for.
 *
 * TR35's four widths. A provider that has no name at a given width should
 * fall back to the abbreviated one rather than return NULL - CLDR's own root
 * locale has no wide or short weekday names and does exactly that.
 */
typedef enum {
  GCHRON_NAME_WIDE = 0,       ///< `January`, `Monday`. LDML `MMMM`, `EEEE`.
  GCHRON_NAME_ABBREVIATED,    ///< `Jan`, `Mon`. LDML `MMM`, `EEE`.
  GCHRON_NAME_NARROW,         ///< `J`, `M`. Not unique, and only for a grid.
  GCHRON_NAME_SHORT           ///< `Mo`, `Tu`. LDML `EEEEEE`; weekdays only.
} GCHRON_NameWidth;

/**
 * @brief Where month names, day names and era names come from.
 *
 * The library ships one, gchron_names_english(), which is the root/C-locale
 * table and is what every named format uses. **It never reads `LC_TIME` or
 * any other locale setting** - `text` learned in `src/text_number.c` what a
 * locale does to `printf`, and a time library's exposure is a hundred times
 * wider (design.md section 8.5).
 *
 * An application that has CLDR - because it linked ICU for other reasons, as
 * `ctang` has - supplies a provider backed by it through this seam. The
 * library defines the interface and ships the minimum; the dependency
 * decision stays with the application.
 */
struct GCHRON_Names {
  /** An identifier for the locale this provider speaks, e.g. `"root"`. */
  const char * id;

  /**
   * The name of a month.
   *
   * @param month 1..12, or further for a calendar with more.
   * @return A borrowed string, or NULL when this provider has none.
   */
  const char * (*month)(const GCHRON_Names * self, int month,
      GCHRON_NameWidth width);

  /**
   * The name of a day of the week, ISO-numbered: 1 is Monday, 7 is Sunday.
   *
   * @return A borrowed string, or NULL.
   */
  const char * (*weekday)(const GCHRON_Names * self, int weekday,
      GCHRON_NameWidth width);

  /**
   * The name of an era: 0 is BCE, 1 is CE.
   *
   * @return A borrowed string, or NULL.
   */
  const char * (*era)(const GCHRON_Names * self, int era,
      GCHRON_NameWidth width);

  /**
   * The name of a day period: 0 is AM, 1 is PM.
   *
   * @return A borrowed string, or NULL.
   */
  const char * (*day_period)(const GCHRON_Names * self, int period,
      GCHRON_NameWidth width);

  /** Which day the week starts on, ISO-numbered. 1 for the root locale. */
  int first_day_of_week;

  /** How many days of the new year week 1 must contain. 4 for ISO 8601. */
  int minimum_days_in_first_week;

  /** Whatever the implementation needs. Ignored by this library. */
  void * ctx;
};

/**
 * @brief The names this library ships: the root locale, in English.
 *
 * @return A borrowed, immutable provider that lives for the life of the
 *   program. Never freed.
 */
GCHRON_API const GCHRON_Names * gchron_names_english(void);

/*--------------------------------------------------------------------------*
 * Compiling a pattern
 *--------------------------------------------------------------------------*/

/** Which pattern language a string is written in. */
typedef enum {
  /**
   * Unicode TR35's letters: `yyyy-MM-dd'T'HH:mm:ssXXX`.
   *
   * Zero, because it is the native one. Chosen over `strftime` because it is
   * the international standard, because it is the most expressive - `VV` for
   * a zone identifier, `xxx` versus `XXX` for offset spelling, `G` for eras -
   * and because ICU speaks it natively and **ICU is the oracle**: a pattern's
   * meaning is defined as what `icu::SimpleDateFormat` does with it in the
   * root locale (design.md section 8.3).
   */
  GCHRON_FORMAT_LDML = 0,

  /**
   * C's `strftime` letters: `%Y-%m-%dT%H:%M:%S%z`.
   *
   * Lowered to the same compiled program, so that everything below the
   * compiler is one implementation. Covers C, Python, Ruby and Rust
   * `chrono`.
   */
  GCHRON_FORMAT_STRFTIME
} GCHRON_FormatSyntax;

/**
 * @brief A compiled pattern.
 *
 * Immutable once created and safe to share between threads. Created with
 * gchron_format_compile(), destroyed with gchron_format_destroy().
 */
typedef struct GCHRON_Format GCHRON_Format;

/**
 * @brief Compile a pattern.
 *
 * @param pattern The pattern. Not assumed to be NUL-terminated, and **may
 *   contain a NUL**, which becomes a literal NUL in the output - so a caller
 *   measuring that output with `strlen` would stop early. The reported length
 *   is the authority, as it is for every writer in this library.
 * @param len Bytes of pattern.
 * @param syntax Which pattern language it is written in.
 * @param limits Caps on the pattern. NULL means gchron_limits_default().
 * @param allocator Allocator. NULL means gchron_allocator_default().
 * @param out Receives the compiled pattern; untouched on failure.
 * @param err Receives the failure and the offset into @p pattern that caused
 *   it. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_FORMAT for a pattern that is not this
 *   syntax; GCHRON_ERR_UNSUPPORTED for a letter this library does not
 *   implement; GCHRON_ERR_LIMIT; GCHRON_ERR_OOM.
 */
GCHRON_API GCHRON_Result gchron_format_compile(const char * pattern,
    size_t len, GCHRON_FormatSyntax syntax, const GCHRON_Limits * limits,
    const GCHRON_Allocator * allocator, GCHRON_Format ** out,
    GCHRON_Error * err);

/**
 * @brief Destroy a compiled pattern.
 *
 * A named format from gchron_format_named() is static, and passing one here
 * is ignored - so a caller holding "whichever format was chosen" need not
 * remember which kind it is.
 *
 * @param format The format. NULL is ignored.
 */
GCHRON_API void gchron_format_destroy(GCHRON_Format * format);

/**
 * @brief The longest output this format can produce, the NUL included.
 *
 * design.md section 8.6: there is no allocating variant, so a caller needs a
 * bound. This is an upper bound and not a promise of exactness.
 *
 * @param format A compiled format.
 * @param out Receives the bound on success; untouched on failure.
 * @return GCHRON_OK, or GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_format_max_length(
    const GCHRON_Format * format, size_t * out);

/*--------------------------------------------------------------------------*
 * Named formats
 *--------------------------------------------------------------------------*/

/**
 * @brief The formats that have names, so that nobody types them by hand.
 *
 * Mistake M8 is `YYYY` where `yyyy` was meant - the week-based year, which is
 * right for fifty-one weeks a year and wrong over New Year. A named format is
 * a timestamp somebody has already got right.
 */
typedef enum {
  GCHRON_NAMED_RFC3339 = 0,     ///< `2026-09-20T15:30:00Z`
  GCHRON_NAMED_RFC3339_NANOS,   ///< `2026-09-20T15:30:00.123456789Z`
  GCHRON_NAMED_RFC9557,         ///< `2026-09-20T17:30:00+02:00[Europe/Paris]`
  GCHRON_NAMED_ISO8601_BASIC,   ///< `20260920T153000Z`
  GCHRON_NAMED_ISO_WEEK,        ///< `2026-W38-7`
  GCHRON_NAMED_ISO_ORDINAL,     ///< `2026-263`
  GCHRON_NAMED_HTTP,            ///< `Sun, 20 Sep 2026 15:30:00 GMT`
  GCHRON_NAMED_RFC5322,         ///< `Sun, 20 Sep 2026 17:30:00 +0200`
  GCHRON_NAMED_COUNT
} GCHRON_NamedFormat;

/**
 * @brief A pre-compiled named format.
 *
 * Every one has a matching parser, so `parse(write(x))` is an identity for
 * all of them - `tests/unit/test_roundtrip.cpp` says so.
 *
 * @param named Which one.
 * @return A borrowed, immutable format, or NULL for a value outside the enum.
 *   Never freed; passing it to gchron_format_destroy() is ignored.
 */
GCHRON_API const GCHRON_Format * gchron_format_named(
    GCHRON_NamedFormat named);

/*--------------------------------------------------------------------------*
 * Applying a format
 *--------------------------------------------------------------------------*/

/**
 * @brief Everything a format might need beyond the value itself.
 *
 * A zero-initialised struct means the English names, the Gregorian calendar
 * and no zone, which is what a civil value formatted with a civil pattern
 * needs.
 */
typedef struct GCHRON_FormatContext {
  /** Month, day and era names. NULL means gchron_names_english(). */
  const GCHRON_Names * names;

  /** The calendar the value is written in. NULL means Gregorian. */
  const GCHRON_Calendar * calendar;

  /**
   * The zone, for the pattern letters that name one.
   *
   * NULL when there is none, and then `VV`, `z` and `v` report
   * GCHRON_ERR_UNSUPPORTED rather than inventing a name.
   */
  const GCHRON_Zone * zone;

  /** The offset to print, in seconds ahead of UTC. */
  int32_t offset_sec;

  /** Whether the offset is RFC 3339 section 4.3's *unknown*. */
  bool offset_unknown;

  /** What the zone called this moment, e.g. `"EST"`. May be NULL. */
  const char * abbreviation;

  /** Whether the zone considered this daylight-saving time. */
  bool is_dst;
} GCHRON_FormatContext;

/**
 * @brief Format a civil date-time.
 *
 * The output contract is parse.h's: the buffer receives a NUL-terminated
 * string, @p out_len is its length without the NUL, and a buffer too small is
 * GCHRON_ERR_LIMIT with @p out_len set to the length the output would have
 * had - so a zero-length buffer asks for the length alone.
 *
 * @param format A compiled format.
 * @param dt A valid civil date-time.
 * @param context Names, calendar and zone. NULL means the defaults.
 * @param buf Where to write. May be NULL only when @p buf_len is 0.
 * @param buf_len Bytes available at @p buf.
 * @param out_len Receives the length written, without the NUL. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_LIMIT; GCHRON_ERR_UNSUPPORTED when a letter
 *   needs something the context does not carry; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_format_datetime(const GCHRON_Format * format,
    const GCHRON_DateTime * dt, const GCHRON_FormatContext * context,
    char * buf, size_t buf_len, size_t * out_len);

/**
 * @brief Format an offset date-time.
 *
 * Fills the context's offset from the value, so a caller need not.
 *
 * @param format A compiled format.
 * @param odt A valid offset date-time.
 * @param context Names and calendar. NULL means the defaults.
 * @param buf Where to write; see gchron_format_datetime().
 * @param buf_len Bytes available at @p buf.
 * @param out_len Receives the length written, without the NUL. May be NULL.
 * @return As gchron_format_datetime().
 */
GCHRON_API GCHRON_Result gchron_format_offset(const GCHRON_Format * format,
    const GCHRON_OffsetDateTime * odt, const GCHRON_FormatContext * context,
    char * buf, size_t buf_len, size_t * out_len);

/**
 * @brief Format a zoned date-time.
 *
 * Fills the context's zone, offset, abbreviation and daylight-saving flag
 * from the value, so that every pattern letter that names a zone works.
 *
 * @param format A compiled format.
 * @param zoned A valid zoned date-time.
 * @param context Names and calendar. NULL means the defaults.
 * @param buf Where to write; see gchron_format_datetime().
 * @param buf_len Bytes available at @p buf.
 * @param out_len Receives the length written, without the NUL. May be NULL.
 * @return As gchron_format_datetime().
 */
GCHRON_API GCHRON_Result gchron_format_zoned(const GCHRON_Format * format,
    const GCHRON_ZonedDateTime * zoned, const GCHRON_FormatContext * context,
    char * buf, size_t buf_len, size_t * out_len);

/*--------------------------------------------------------------------------*
 * HTTP-date and RFC 5322
 *--------------------------------------------------------------------------*/

/**
 * @brief Parse an HTTP-date.
 *
 * RFC 9110 section 5.6.7. IMF-fixdate is what a server should send
 * (`Sun, 06 Nov 1994 08:49:37 GMT`), and a recipient **must** also accept the
 * obsolete RFC 850 form (`Sunday, 06-Nov-94 08:49:37 GMT`) and the `asctime`
 * form (`Sun Nov  6 08:49:37 1994`).
 *
 * **RFC 850's year is two digits**, and the RFC's rule for expanding it -
 * "recipients of a timestamp value in rfc850-date format, which uses a
 * two-digit year, MUST interpret a timestamp that appears to be more than 50
 * years in the future as representing the most recent year in the past that
 * had the same last two digits" - needs to know what year it is now. That is
 * why this takes a clock, and why nothing in this library calls the system
 * clock on its own (mistake M18).
 *
 * @param text The input. Not assumed to be NUL-terminated.
 * @param len Bytes of input.
 * @param clock What year it is, for the RFC 850 rule. NULL is
 *   GCHRON_ERR_INVALID when the text needs one, and fine when it does not.
 * @param opts Options. NULL means gchron_parse_options_default().
 * @param out Receives the value on success; untouched on failure. Always
 *   GMT, which is the only zone an HTTP-date has.
 * @param info Receives what the text said. May be NULL.
 * @param err Receives the failure and its position. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_FORMAT; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_parse_http_date(const char * text, size_t len,
    const GCHRON_Clock * clock, const GCHRON_ParseOptions * opts,
    GCHRON_OffsetDateTime * out, GCHRON_ParseInfo * info, GCHRON_Error * err);

/** Bytes an IMF-fixdate needs, the terminating NUL included. */
#define GCHRON_HTTP_DATE_MAX ((size_t)30)

/**
 * @brief Write an HTTP-date.
 *
 * Always IMF-fixdate, always GMT, as RFC 9110 requires of a sender. The value
 * is converted to GMT first, so a caller may pass one with any offset.
 *
 * @param odt A valid offset date-time whose year is 0..9999.
 * @param buf Where to write; see gchron_format_datetime().
 * @param buf_len Bytes available at @p buf.
 * @param out_len Receives the length written, without the NUL. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_LIMIT; GCHRON_ERR_RANGE; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_write_http_date(
    const GCHRON_OffsetDateTime * odt, char * buf, size_t buf_len,
    size_t * out_len);

/**
 * @brief Parse an RFC 5322 date-time.
 *
 * The `Date:` header of an email. Section 3.3's grammar, and section 4.3's
 * obsolete forms: a two-digit year, folding whitespace, comments in
 * parentheses, and the obsolete zone names.
 *
 * **`-0000` means the offset is unknown**, exactly as RFC 3339's `-00:00`
 * does, and sets GCHRON_OffsetDateTime::offset_unknown (mistake M14).
 *
 * The obsolete alphabetic zones map per section 4.3's own table: `UT` and
 * `GMT` are +0000, `EST` is -0500 and so on, and **every other single letter
 * is +0000 with the offset marked unknown**, which is what the RFC says to do
 * with military zones because they were widely got wrong.
 *
 * @param text The input. Not assumed to be NUL-terminated.
 * @param len Bytes of input.
 * @param clock What year it is, for the two-digit-year rule. May be NULL when
 *   the text carries four digits.
 * @param opts Options. NULL means gchron_parse_options_default().
 * @param out Receives the value on success; untouched on failure.
 * @param info Receives what the text said. May be NULL.
 * @param err Receives the failure and its position. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_FORMAT; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_parse_rfc5322(const char * text, size_t len,
    const GCHRON_Clock * clock, const GCHRON_ParseOptions * opts,
    GCHRON_OffsetDateTime * out, GCHRON_ParseInfo * info, GCHRON_Error * err);

/** Bytes an RFC 5322 date-time needs, the terminating NUL included. */
#define GCHRON_RFC5322_MAX ((size_t)32)

/**
 * @brief Write an RFC 5322 date-time.
 *
 * `Sun, 20 Sep 2026 17:30:00 +0200`. An unknown offset writes `-0000`.
 *
 * @param odt A valid offset date-time whose year is 0..9999.
 * @param buf Where to write; see gchron_format_datetime().
 * @param buf_len Bytes available at @p buf.
 * @param out_len Receives the length written, without the NUL. May be NULL.
 * @return GCHRON_OK; GCHRON_ERR_LIMIT; GCHRON_ERR_RANGE; GCHRON_ERR_INVALID.
 */
GCHRON_API GCHRON_Result gchron_write_rfc5322(
    const GCHRON_OffsetDateTime * odt, char * buf, size_t buf_len,
    size_t * out_len);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_FORMAT_H
