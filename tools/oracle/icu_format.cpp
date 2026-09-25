/**
 * @file
 *
 * The ICU side of the LDML differential.
 *
 * design.md section 8.3: the native pattern language is LDML, and **a
 * pattern's meaning is defined as what `icu::SimpleDateFormat` does with it
 * in the root locale**. That is not a figure of speech - it is the reason
 * this file exists and the reason ICU is never linked by the library itself.
 * ICU is the authority on the question; chron is the implementation being
 * measured.
 *
 * Two modes, chosen by the single argument.
 *
 * Without one, it *writes*: reads `<pattern> <TAB> <zone> <TAB> <unix millis>`
 * and writes `<pattern> <TAB> <zone> <TAB> <millis> <TAB> <formatted>`.
 *
 * With `parse`, it *reads back*: takes that four-field line and replaces the
 * last field with the millis ICU recovers from its own text. That is what
 * makes the parse differential a fair comparison - both sides are handed the
 * same bytes, so a pattern that cannot carry a millisecond or the seconds of
 * a sub-minute offset loses them on both sides and the two still agree
 * (design.md section 8.7).
 *
 * Either way `ERR` marks a line ICU itself refused. One line in, one line
 * out, so a diff names the exact input that disagreed.
 *
 * Built by `make tools` only when `pkg-config icu-i18n` succeeds.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <unicode/calendar.h>
#include <unicode/uvernum.h>
#include <unicode/locid.h>
#include <unicode/smpdtfmt.h>
#include <unicode/timezone.h>
#include <unicode/unistr.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char ** argv) {
  /*
   * `version` answers which ICU this binary was compiled against, which no
   * gate used to record. Two libraries in this suite ask ICU questions and
   * they do not necessarily ask the same ICU: this one takes whatever
   * pkg-config finds on the host. The number that decides whether a
   * disagreement can be a defect at all is the Unicode data version rather
   * than the release, so both are printed, and both come from the headers
   * this file compiles against rather than from a library queried at runtime.
   */
  if (argc > 1 && std::string(argv[1]) == "version") {
    std::printf("icu %s\tunicode %s\n", U_ICU_VERSION, U_UNICODE_VERSION);
    return 0;
  }

  const bool parsing = (argc > 1 && std::string(argv[1]) == "parse");
  std::string line;

  while (std::getline(std::cin, line)) {
    size_t first = line.find('\t');
    if (first == std::string::npos) {
      continue;
    }
    size_t second = line.find('\t', first + 1);
    if (second == std::string::npos) {
      continue;
    }

    std::string pattern = line.substr(0, first);
    std::string zone = line.substr(first + 1, second - first - 1);
    double millis = std::strtod(line.substr(second + 1).c_str(), nullptr);

    UErrorCode status = U_ZERO_ERROR;
    icu::UnicodeString ustring =
        icu::UnicodeString::fromUTF8(icu::StringPiece(pattern));
    icu::SimpleDateFormat format(ustring, icu::Locale::getRoot(), status);
    if (U_FAILURE(status)) {
      std::printf("%s\t%s\t%.0f\tERR\n", pattern.c_str(), zone.c_str(),
          millis);
      continue;
    }

    icu::TimeZone * tz = icu::TimeZone::createTimeZone(
        icu::UnicodeString::fromUTF8(icu::StringPiece(zone)));
    format.setTimeZone(*tz);
    delete tz;

    std::string utf8;
    if (parsing) {
      // The text ICU wrote, on the fourth field, read back by ICU.
      size_t third = line.find('\t', second + 1);
      std::string text =
          (third == std::string::npos) ? std::string() : line.substr(third + 1);
      if (text == "ERR" || text.empty()) {
        std::printf("%s\t%s\t%.0f\tERR\n", pattern.c_str(), zone.c_str(),
            millis);
        continue;
      }
      // Lenient off: the mode being measured is the strict one, and ICU's
      // default would take `2026-9-8` for `yyyy-MM-dd`.
      format.setLenient(false);
      UErrorCode parse_status = U_ZERO_ERROR;
      UDate back = format.parse(
          icu::UnicodeString::fromUTF8(icu::StringPiece(text)), parse_status);
      if (U_FAILURE(parse_status)) {
        std::printf("%s\t%s\t%.0f\tERR\n", pattern.c_str(), zone.c_str(),
            millis);
        continue;
      }
      std::printf("%s\t%s\t%.0f\t%.0f\n", pattern.c_str(), zone.c_str(),
          millis, back);
      continue;
    }

    icu::UnicodeString out;
    format.format((UDate)millis, out);
    out.toUTF8String(utf8);

    // Tabs and newlines would break the line protocol, and no date format
    // produces one - so their presence is a bug rather than a case to handle.
    for (char & c : utf8) {
      if (c == '\t' || c == '\n' || c == '\r') {
        c = ' ';
      }
    }
    std::printf("%s\t%s\t%.0f\t%s\n", pattern.c_str(), zone.c_str(), millis,
        utf8.c_str());
  }
  return 0;
}
