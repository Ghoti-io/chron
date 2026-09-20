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
 * Reads `<pattern> <TAB> <zone> <TAB> <unix millis>` on standard input and
 * writes `<pattern> <TAB> <zone> <TAB> <millis> <TAB> <formatted>` on
 * standard output, or `ERR` where ICU itself refused. One line in, one line
 * out, so a diff names the exact input that disagreed.
 *
 * Built by `make tools` only when `pkg-config icu-i18n` succeeds.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <unicode/calendar.h>
#include <unicode/locid.h>
#include <unicode/smpdtfmt.h>
#include <unicode/timezone.h>
#include <unicode/unistr.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

int main() {
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

    icu::UnicodeString out;
    format.format((UDate)millis, out);
    std::string utf8;
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
