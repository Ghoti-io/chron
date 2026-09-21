/**
 * @file
 *
 * The compiled shape of a pattern. Private; installed with nothing.
 *
 * A pattern compiles to a flat list of items, each either a run of literal
 * text or one field with a letter count. Both pattern languages lower to the
 * same list, so everything below the compiler - the emitter, the length
 * bound, the tests - is one implementation rather than two.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCHRON_SRC_FORMAT_FORMAT_INTERNAL_H
#define GHOTI_IO_GCHRON_SRC_FORMAT_FORMAT_INTERNAL_H

#include <ghoti.io/chron/allocator.h>
#include <ghoti.io/chron/format.h>
#include <ghoti.io/chron/macros.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What one item of a compiled pattern produces. */
typedef enum {
  GCHRON_ITEM_LITERAL = 0,     /**< Bytes copied through unchanged. */
  GCHRON_ITEM_ERA,             /**< `G` */
  GCHRON_ITEM_YEAR,            /**< `y` */
  GCHRON_ITEM_WEEK_YEAR,       /**< `Y` */
  GCHRON_ITEM_EXTENDED_YEAR,   /**< `u` */
  GCHRON_ITEM_QUARTER,         /**< `Q`, `q` */
  GCHRON_ITEM_MONTH,           /**< `M`, `L` */
  GCHRON_ITEM_WEEK_OF_YEAR,    /**< `w` */
  GCHRON_ITEM_WEEK_OF_MONTH,   /**< `W` */
  GCHRON_ITEM_DAY,             /**< `d` */
  GCHRON_ITEM_DAY_OF_YEAR,     /**< `D` */
  GCHRON_ITEM_WEEKDAY_IN_MONTH,/**< `F` */
  GCHRON_ITEM_MODIFIED_JULIAN, /**< `g` */
  GCHRON_ITEM_WEEKDAY,         /**< `E` */
  GCHRON_ITEM_WEEKDAY_LOCAL,   /**< `e`, `c` */
  GCHRON_ITEM_DAY_PERIOD,      /**< `a` */
  GCHRON_ITEM_HOUR_1_12,       /**< `h` */
  GCHRON_ITEM_HOUR_0_23,       /**< `H` */
  GCHRON_ITEM_HOUR_0_11,       /**< `K` */
  GCHRON_ITEM_HOUR_1_24,       /**< `k` */
  GCHRON_ITEM_MINUTE,          /**< `m` */
  GCHRON_ITEM_SECOND,          /**< `s` */
  GCHRON_ITEM_FRACTION,        /**< `S` */
  GCHRON_ITEM_MILLIS_OF_DAY,   /**< `A` */
  GCHRON_ITEM_ZONE_ABBREV,     /**< `z` */
  GCHRON_ITEM_ZONE_ID,         /**< `V` */
  GCHRON_ITEM_OFFSET_ISO_Z,    /**< `X` - `Z` when the offset is zero. */
  GCHRON_ITEM_OFFSET_ISO,      /**< `x` - always numeric. */
  GCHRON_ITEM_OFFSET_RFC822,   /**< `Z` */
  GCHRON_ITEM_OFFSET_LOCALISED,/**< `O` */
  GCHRON_ITEM_EPOCH_SECONDS,   /**< strftime's `%s`; no LDML letter. */

  /*
   * `strftime` has shapes LDML has no letter for: space padding instead of
   * zero, a century without its year, and two more weekday numberings. They
   * get their own items rather than being approximated with an LDML letter,
   * because `%e` really does write " 5" where `dd` writes "05", and a
   * differential against glibc notices the difference immediately.
   */
  GCHRON_ITEM_CENTURY,               /**< `%C` */
  GCHRON_ITEM_DAY_SPACE_PADDED,      /**< `%e` */
  GCHRON_ITEM_HOUR_0_23_SPACE_PADDED,/**< `%k` */
  GCHRON_ITEM_HOUR_1_12_SPACE_PADDED,/**< `%l` */
  GCHRON_ITEM_WEEKDAY_ISO_NUMBER,    /**< `%u` - Monday 1 .. Sunday 7 */
  GCHRON_ITEM_WEEKDAY_SUNDAY_ZERO,   /**< `%w` - Sunday 0 .. Saturday 6 */

  GCHRON_ITEM_COUNT
} GCHRON_ItemKind;

/** One item of a compiled pattern. */
typedef struct GCHRON_FormatItem {
  GCHRON_ItemKind kind;  /**< What it produces. */
  uint16_t count;        /**< How many pattern letters; 1 for a literal. */
  uint32_t literal_at;   /**< Offset into the literal pool. */
  uint32_t literal_len;  /**< Bytes of literal. */
  /**
   * Byte offset into the pattern this item was compiled from.
   *
   * So that a complaint about an item can point at the text a person wrote.
   * gchron_format_is_invertible() reported the item *index* before this
   * existed, in a GCHRON_Error::offset documented as a byte offset - which
   * `fuzz_scan` noticed when the number exceeded the length of the input.
   */
  uint32_t pattern_at;
} GCHRON_FormatItem;

/** A compiled pattern. */
struct GCHRON_Format {
  const GCHRON_Allocator * allocator; /**< NULL for a static format. */
  GCHRON_FormatItem * items;
  size_t item_count;
  char * literals;                    /**< All literal runs, concatenated. */
  size_t literal_bytes;
  size_t max_length;                  /**< Upper bound, the NUL included. */
  bool is_static;                     /**< A named format; never freed. */
};

/**
 * Emit one compiled pattern into a buffer.
 *
 * @param format The compiled pattern.
 * @param dt The civil reading to format.
 * @param context Names, calendar, zone and offset. Never NULL here.
 * @param instant The instant, for the letters that need one; may be NULL.
 * @param buf Where to write; may be NULL when @p buf_len is 0.
 * @param buf_len Bytes available.
 * @param out_len Receives the length the output needs, without the NUL.
 * @return GCHRON_OK, GCHRON_ERR_LIMIT, GCHRON_ERR_UNSUPPORTED or
 *   GCHRON_ERR_INVALID.
 */
GCHRON_Result gchron_format_emit(const GCHRON_Format * format,
    const GCHRON_DateTime * dt, const GCHRON_FormatContext * context,
    const GCHRON_Instant * instant, char * buf, size_t buf_len,
    size_t * out_len);

/**
 * Compile a pattern into an already-allocated format.
 *
 * Shared by gchron_format_compile() and the static table of named formats,
 * which is built once on first use.
 */
GCHRON_Result gchron_format_compile_into(const char * pattern, size_t len,
    GCHRON_FormatSyntax syntax, const GCHRON_Limits * limits,
    const GCHRON_Allocator * allocator, GCHRON_Format * out,
    GCHRON_Error * err);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GCHRON_SRC_FORMAT_FORMAT_INTERNAL_H
