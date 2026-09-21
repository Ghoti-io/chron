/**
 * @file
 *
 * The option presets, and the scanner every grammar shares.
 *
 * Reference: RFC 3339 sections 5.6 and 5.7; RFC 5234 section 2.3, which makes
 * ABNF string literals case-insensitive and so makes `t` and `z` conformant.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/chron/civil.h>
#include <ghoti.io/chron/core.h>
#include <ghoti.io/chron/macros.h>
#include <ghoti.io/chron/parse.h>
#include <string.h>

#include "../core/core_internal.h"
#include "parse_internal.h"

void gchron_parse_options_default(GCHRON_ParseOptions * out) {
  if (out == NULL) {
    return;
  }
  /*
   * Zeroed first, and then nothing further is needed - because "zero is
   * strict" (design.md section 3.7) is a property of every field here: each
   * policy enum's zero value is its REJECT, and every flag's is off.
   *
   * The first spelling assigned the fields one at a time, which meant a field
   * added in a later phase was left holding whatever was on the caller's
   * stack. Phase 4 added `leap_table` and this function started handing back
   * a garbage pointer under the name of a default. Assigning the fields is
   * the kind of correct that stops being correct when someone adds a field;
   * memset is the kind that does not.
   */
  memset(out, 0, sizeof(*out));
}

void gchron_parse_options_toml(GCHRON_ParseOptions * out) {
  if (out == NULL) {
    return;
  }
  gchron_parse_options_default(out);
  /* TOML 1.0.0: "the additional precision must be truncated, not rounded". */
  out->fraction = GCHRON_FRACTION_TRUNCATE;
  /* TOML 1.0.0 permits a space where RFC 3339 wants `T`. */
  out->allow_space_separator = true;
}

void gchron_parse_options_yaml(GCHRON_ParseOptions * out) {
  if (out == NULL) {
    return;
  }
  gchron_parse_options_default(out);
  /*
   * YAML states neither of these, which is why they are set here under the
   * grammar's name rather than buried in the parser. The reasoning is in
   * parse.h; the short form is that YAML's expression permits a fraction of
   * any length and a `:60` second, and a reader that refused either would
   * resolve a conformant `!!timestamp` as a `!!str` - changing what the
   * document says rather than reporting that something is wrong with it.
   */
  out->fraction = GCHRON_FRACTION_TRUNCATE;
  out->leap = GCHRON_LEAP_CLAMP;
}

void gchron_parse_options_json_schema(GCHRON_ParseOptions * out) {
  if (out == NULL) {
    return;
  }
  gchron_parse_options_default(out);
  /*
   * The JSON-Schema-Test-Suite's optional format vectors require
   * `00:59:59.999999999999999Z` to be a valid `time` and
   * `1998-12-31T23:59:60Z` to be a valid `date-time`, while refusing
   * `1998-12-31T23:58:60Z`. That is exactly GCHRON_FRACTION_TRUNCATE plus
   * GCHRON_LEAP_MINUTE (design.md section 5.4).
   */
  out->fraction = GCHRON_FRACTION_TRUNCATE;
  out->leap = GCHRON_LEAP_MINUTE;
}

void gchron_parse_info_clear(GCHRON_ParseInfo * info) {
  if (info == NULL) {
    return;
  }
  /* Zeroed wholesale, for the reason gchron_parse_options_default() gives. */
  memset(info, 0, sizeof(*info));
}

const GCHRON_ParseOptions * gchron_parse_options_effective(
    const GCHRON_ParseOptions * opts, GCHRON_ParseOptions * fallback) {
  if (opts != NULL) {
    return opts;
  }
  gchron_parse_options_default(fallback);
  return fallback;
}

GCHRON_Result gchron_scan_preflight(GCHRON_Scanner * sc) {
  GCHRON_Limits defaults;
  const GCHRON_Limits * limits = sc->opts->limits;
  char c;

  if (limits == NULL) {
    gchron_limits_default(&defaults);
    limits = &defaults;
  }
  if (limits->max_parse_length != 0 && sc->len > limits->max_parse_length) {
    return gchron_fail(sc->err, GCHRON_ERR_LIMIT, GCHRON_DIAG_INPUT_TOO_LONG,
        limits->max_parse_length, 0);
  }
  if (sc->len == 0) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT, GCHRON_DIAG_UNEXPECTED_END,
        0, 0);
  }
  c = sc->text[0];
  if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'
      || c == '\v') {
    /*
     * Reported as its own diagnostic rather than as "expected a digit",
     * because " 2024-01-15" is a caller who trimmed nothing rather than a
     * caller whose format is wrong, and the two have different fixes.
     */
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_LEADING_WHITESPACE, 0, 1);
  }
  return GCHRON_OK;
}

GCHRON_Result gchron_scan_digits(GCHRON_Scanner * sc, int count, int * out) {
  int value = 0;
  int index;

  for (index = 0; index < count; ++index) {
    unsigned char c;
    if (sc->pos >= sc->len) {
      return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
          GCHRON_DIAG_UNEXPECTED_END, sc->pos, 0);
    }
    c = (unsigned char)sc->text[sc->pos];
    if (c < '0' || c > '9') {
      /*
       * A byte with the high bit set is the leading byte of some non-ASCII
       * character, and the character it leads is very often a digit in
       * another script - the suite's Bengali `২`. Saying so is more use than
       * "expected a digit", which reads as though the field were missing.
       */
      return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
          (c >= 0x80) ? GCHRON_DIAG_NON_ASCII_DIGIT
                      : GCHRON_DIAG_EXPECTED_DIGIT,
          sc->pos, 1);
    }
    value = value * 10 + (c - '0');
    sc->pos += 1;
  }
  *out = value;
  return GCHRON_OK;
}

GCHRON_Result gchron_scan_literal(GCHRON_Scanner * sc, char c,
    GCHRON_Diag diag) {
  if (sc->pos >= sc->len) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT, GCHRON_DIAG_UNEXPECTED_END,
        sc->pos, 0);
  }
  if (sc->text[sc->pos] != c) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT, diag, sc->pos, 1);
  }
  sc->pos += 1;
  return GCHRON_OK;
}

GCHRON_Result gchron_scan_date_time_separator(GCHRON_Scanner * sc) {
  char c;

  if (sc->pos >= sc->len) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT, GCHRON_DIAG_UNEXPECTED_END,
        sc->pos, 0);
  }
  c = sc->text[sc->pos];
  if (c == 'T' || c == 't'
      || (c == ' ' && sc->opts->allow_space_separator)) {
    sc->pos += 1;
    return GCHRON_OK;
  }
  return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
      GCHRON_DIAG_EXPECTED_SEPARATOR, sc->pos, 1);
}

GCHRON_Result gchron_scan_finish(GCHRON_Scanner * sc, GCHRON_ParseInfo * info) {
  if (sc->pos != sc->len && !sc->opts->allow_trailing) {
    return gchron_fail(sc->err, GCHRON_ERR_FORMAT,
        GCHRON_DIAG_TRAILING_CHARACTERS, sc->pos, sc->len - sc->pos);
  }
  if (info != NULL) {
    info->consumed = sc->pos;
  }
  return GCHRON_OK;
}
