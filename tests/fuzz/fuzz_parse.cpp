/**
 * @file
 *
 * Every text grammar, with the first byte selecting which.
 *
 * design.md section 1.2: a date string from a document is the input `text`
 * will hand this library thousands of times per file, and every parser here
 * is bounded by input length. The options byte drives the policies and the
 * limits so that one harness covers the parser at every setting rather than
 * only at its default - which is where the interesting disagreements are,
 * since the leap-second check and the fraction check are the two places a
 * policy changes control flow.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstddef>
#include <cstdint>

#include <ghoti.io/chron/chron.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) {
    return 0;
  }

  const uint8_t selector = data[0];
  const uint8_t options = data[1];
  const char * text = reinterpret_cast<const char *>(data + 2);
  const size_t len = size - 2;

  GCHRON_Limits limits;
  GCHRON_ParseOptions opts;
  gchron_limits_default(&limits);
  gchron_parse_options_default(&opts);

  // A limit of zero means "no limit", so both sides of that branch are
  // reachable rather than only the default.
  limits.max_parse_length = (options & 0x20) ? 0 : ((options >> 6) * 64 + 8);
  opts.limits = &limits;
  opts.fraction = (options & 0x01) ? GCHRON_FRACTION_TRUNCATE
                                   : GCHRON_FRACTION_REJECT;
  opts.leap = static_cast<GCHRON_Leap>((options >> 1) & 0x03);
  opts.allow_space_separator = (options & 0x08) != 0;
  opts.allow_trailing = (options & 0x10) != 0;

  GCHRON_ParseInfo info;
  GCHRON_Error err;

  switch (selector % 5) {
    case 0: {
      GCHRON_OffsetDateTime out;
      gchron_parse_rfc3339_date_time(text, len, &opts, &out, &info, &err);
      break;
    }
    case 1: {
      GCHRON_Date out;
      gchron_parse_rfc3339_full_date(text, len, &opts, &out, &info, &err);
      break;
    }
    case 2: {
      GCHRON_OffsetTime out;
      gchron_parse_rfc3339_full_time(text, len, &opts, &out, &info, &err);
      break;
    }
    case 3: {
      GCHRON_Duration out;
      gchron_parse_rfc3339_duration(text, len, &opts, &out, &info, &err);
      break;
    }
    case 4: {
      GCHRON_TomlValue out;
      gchron_parse_toml(text, len, &opts, &out, &info, &err);
      break;
    }
    default:
      break;
  }
  return 0;
}
