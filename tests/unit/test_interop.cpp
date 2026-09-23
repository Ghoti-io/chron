/**
 * @file
 *
 * The foreign encodings, and the defect in each one.
 *
 * Mistake M17: Excel's 1900 leap year, NTP's 2036 era, DOS's local time and
 * FILETIME's 1601 epoch get rediscovered by each program that meets them.
 * Every test here is named for the defect it pins.
 *
 * The epoch constants are checked against Python's `datetime`, so that a
 * wrong one is caught rather than being wrong self-consistently in both
 * directions.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>
#include <ghoti.io/chron/interop.h>

#include "test_helpers.h"

namespace {

GCHRON_Instant at_utc(int32_t year, int month, int day, int hour, int minute,
    int second) {
  GCHRON_DateTime dt =
      gchrontest::datetime(year, month, day, hour, minute, second);
  GCHRON_Instant i{};
  EXPECT_EQ(GCHRON_OK, gchron_instant_from_utc(&dt, &i));
  return i;
}

} // namespace

/*
 * The epochs, each against a date Python's `datetime` was asked for, so that
 * a constant which is wrong is wrong visibly rather than consistently.
 */
TEST(Interop, EveryEpochConstantLandsOnItsOwnDate) {
  struct Case {
    const char * name;
    GCHRON_Result (*from)(uint64_t, GCHRON_Instant *);
    uint64_t zero;
    int32_t year;
    int month;
    int day;
  };

  GCHRON_Instant i{};
  GCHRON_DateTime dt{};

  // FILETIME: 1601-01-01.
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_filetime(0, &i));
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&i, &dt));
  EXPECT_EQ(1601, dt.date.year);
  EXPECT_EQ(1, dt.date.month);
  EXPECT_EQ(1, dt.date.day);

  // .NET ticks: 0001-01-01 proleptic Gregorian.
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_dotnet_ticks(0, &i));
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&i, &dt));
  EXPECT_EQ(1, dt.date.year);
  EXPECT_EQ(1, dt.date.month);
  EXPECT_EQ(1, dt.date.day);

  // NTP era 0: 1900-01-01.
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_ntp(0, 0, &i));
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&i, &dt));
  EXPECT_EQ(1900, dt.date.year);

  // HFS+: 1904-01-01.
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_hfs_plus(0, &i));
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&i, &dt));
  EXPECT_EQ(1904, dt.date.year);

  // Cocoa: 2001-01-01.
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_cocoa(0.0, &i));
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&i, &dt));
  EXPECT_EQ(2001, dt.date.year);

  // Modified Julian Date: 1858-11-17.
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_mjd(0.0, &i));
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&i, &dt));
  EXPECT_EQ(1858, dt.date.year);
  EXPECT_EQ(11, dt.date.month);
  EXPECT_EQ(17, dt.date.day);
  (void)sizeof(Case);
}

/*
 * Excel believes 1900 was a leap year. It was not; the bug is Lotus 1-2-3's,
 * kept for compatibility. Serial 60 is "1900-02-29", a day that did not
 * occur, and every serial above it is one too high.
 */
TEST(Interop, ExcelsPhantomLeapDayIsRefusedRatherThanMapped) {
  GCHRON_DateTime dt{};

  // Serial 1 is 1900-01-01, on both sides of the phantom day.
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_excel_1900(1.0, &dt));
  EXPECT_EQ(1900, dt.date.year);
  EXPECT_EQ(1, dt.date.month);
  EXPECT_EQ(1, dt.date.day);

  ASSERT_EQ(GCHRON_OK, gchron_interop_from_excel_1900(59.0, &dt));
  EXPECT_EQ(2, dt.date.month);
  EXPECT_EQ(28, dt.date.day) << "serial 59 is the real 28 February";

  // Serial 60 is the day that did not happen.
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_interop_from_excel_1900(60.0, &dt));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_interop_from_excel_1900(60.5, &dt));

  ASSERT_EQ(GCHRON_OK, gchron_interop_from_excel_1900(61.0, &dt));
  EXPECT_EQ(3, dt.date.month);
  EXPECT_EQ(1, dt.date.day) << "serial 61 is 1 March, one day later than a "
                               "naive epoch would give";

  // The serial everybody checks: 2026-09-20.
  double serial = 0;
  GCHRON_DateTime today = gchrontest::datetime(2026, 9, 20, 0, 0, 0);
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_excel_1900(&today, &serial));
  GCHRON_DateTime back{};
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_excel_1900(serial, &back));
  EXPECT_EQ(0, gchron_datetime_compare(&today, &back));

  // Writing a date before the phantom day has no single right answer.
  GCHRON_DateTime early = gchrontest::datetime(1900, 2, 1, 0, 0, 0);
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_interop_to_excel_1900(&early, &serial));
}

TEST(Interop, TheNineteenOhFourSystemHasNoPhantomDay) {
  GCHRON_DateTime dt{};
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_excel_1904(0.0, &dt));
  EXPECT_EQ(1904, dt.date.year);
  EXPECT_EQ(1, dt.date.month);
  EXPECT_EQ(1, dt.date.day);

  // The same calendar day has different serials in the two systems, which is
  // why a workbook says which it uses.
  GCHRON_DateTime today = gchrontest::datetime(2026, 9, 20, 0, 0, 0);
  double s1900 = 0;
  double s1904 = 0;
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_excel_1900(&today, &s1900));
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_excel_1904(&today, &s1904));
  EXPECT_NE(s1900, s1904);
  EXPECT_EQ(1462, static_cast<long>(s1900 - s1904))
      << "four years and a day apart";
}

/*
 * NTP's seconds field is 32 bits and era 0 ends on 2036-02-07. Which era a
 * timestamp belongs to is information the timestamp does not carry.
 */
TEST(Interop, NtpTakesTheEraRatherThanGuessingAtIt) {
  GCHRON_Instant i{};
  GCHRON_DateTime dt{};

  // The same 32-bit field means two different dates in two eras.
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_ntp(UINT64_C(0) << 32, 0, &i));
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&i, &dt));
  EXPECT_EQ(1900, dt.date.year);

  ASSERT_EQ(GCHRON_OK, gchron_interop_from_ntp(UINT64_C(0) << 32, 1, &i));
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&i, &dt));
  EXPECT_EQ(2036, dt.date.year);

  // And the writer says which era it produced, so a caller can record it.
  GCHRON_Instant now = at_utc(2026, 9, 20, 15, 30, 0);
  uint64_t value = 0;
  int32_t era = -1;
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_ntp(&now, &value, &era));
  EXPECT_EQ(0, era);

  GCHRON_Instant later = at_utc(2100, 1, 1, 0, 0, 0);
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_ntp(&later, &value, &era));
  EXPECT_EQ(1, era) << "past 2036, which era 0 cannot hold";

  // There is no NTP timestamp before 1900.
  GCHRON_Instant early = at_utc(1899, 12, 31, 23, 59, 59);
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_interop_to_ntp(&early, &value, &era));

  // The fraction survives a round trip to the nanosecond.
  GCHRON_Instant fractional{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1758382200, 500000000,
      &fractional));
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_ntp(&fractional, &value, &era));
  GCHRON_Instant back{};
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_ntp(value, era, &back));
  EXPECT_EQ(fractional.sec, back.sec);
  EXPECT_NEAR(fractional.nsec, back.nsec, 1);
}

/*
 * DOS timestamps are local time with no zone, so the conversion produces a
 * civil date-time and not an instant.
 */
TEST(Interop, DosTimestampsAreCivilAndTwoSecondResolution) {
  GCHRON_DateTime dt{};
  uint16_t date = 0;
  uint16_t time = 0;

  GCHRON_DateTime original =
      gchrontest::datetime(2026, 9, 20, 15, 30, 45);
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_dos(&original, &date, &time));
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_dos(date, time, &dt));
  EXPECT_EQ(2026, dt.date.year);
  EXPECT_EQ(9, dt.date.month);
  EXPECT_EQ(20, dt.date.day);
  EXPECT_EQ(15, dt.time.hour);
  EXPECT_EQ(30, dt.time.minute);
  // Truncated to an even second, because the format has one bit fewer than a
  // second needs - and truncated rather than rounded, because rounding
  // 23:59:59 up would carry into the next day.
  EXPECT_EQ(44, dt.time.second);

  GCHRON_DateTime end_of_day =
      gchrontest::datetime(2026, 9, 20, 23, 59, 59);
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_dos(&end_of_day, &date, &time));
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_dos(date, time, &dt));
  EXPECT_EQ(20, dt.date.day) << "still the same day";
  EXPECT_EQ(58, dt.time.second);

  // The epoch is 1980 and the field is seven bits.
  GCHRON_DateTime too_early = gchrontest::datetime(1979, 12, 31, 0, 0, 0);
  EXPECT_EQ(GCHRON_ERR_RANGE,
      gchron_interop_to_dos(&too_early, &date, &time));
  GCHRON_DateTime too_late = gchrontest::datetime(2108, 1, 1, 0, 0, 0);
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_interop_to_dos(&too_late, &date, &time));
}

/*
 * RFC 1952: a gzip MTIME of zero means "not available", not the epoch. A
 * decompressor that stamped 1970-01-01 on a file because the archive carried
 * no time is the defect this exists to prevent.
 */
TEST(Interop, AZeroGzipMtimeMeansAbsentAndNotTheEpoch) {
  GCHRON_Instant i{};
  bool present = true;

  ASSERT_EQ(GCHRON_OK, gchron_interop_from_gzip_mtime(0, &i, &present));
  EXPECT_FALSE(present);

  // A caller who does not ask gets an error rather than a wrong date.
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_interop_from_gzip_mtime(0, &i, nullptr));

  ASSERT_EQ(GCHRON_OK, gchron_interop_from_gzip_mtime(1758382200, &i,
      &present));
  EXPECT_TRUE(present);
  EXPECT_EQ(1758382200, i.sec);

  // Writing "not available".
  uint32_t value = 12345;
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_gzip_mtime(nullptr, &value));
  EXPECT_EQ(0u, value);

  // The epoch itself cannot be written, because zero is taken - which is a
  // limit of the format rather than of this function.
  GCHRON_Instant epoch{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(0, 0, &epoch));
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_interop_to_gzip_mtime(&epoch, &value));
}

TEST(Interop, PngTimeIsUtcWithATwoByteYear) {
  uint8_t bytes[7];
  GCHRON_DateTime dt = gchrontest::datetime(2026, 9, 20, 15, 30, 45);
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_png_time(&dt, bytes));
  EXPECT_EQ(0x07, bytes[0]);
  EXPECT_EQ(0xEA, bytes[1]) << "2026 big-endian";
  EXPECT_EQ(9, bytes[2]);
  EXPECT_EQ(20, bytes[3]);

  GCHRON_DateTime back{};
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_png_time(bytes, &back));
  EXPECT_EQ(0, gchron_datetime_compare(&dt, &back));

  // Fields that are not a date are refused rather than stored.
  uint8_t bad[7] = { 0x07, 0xEA, 2, 30, 0, 0, 0 };
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_interop_from_png_time(bad, &back));
}

/*
 * `YYYY:MM:DD HH:MM:SS` - with colons in the *date*, which is the thing
 * everybody's first EXIF parser gets wrong.
 */
TEST(Interop, ExifUsesColonsInTheDate) {
  char text[GCHRON_EXIF_DATETIME_BYTES];
  GCHRON_DateTime dt = gchrontest::datetime(2026, 9, 20, 15, 30, 45);
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_exif(&dt, text));
  EXPECT_STREQ("2026:09:20 15:30:45", text);

  GCHRON_DateTime back{};
  ASSERT_EQ(GCHRON_OK,
      gchron_interop_from_exif(text, std::strlen(text), &back));
  EXPECT_EQ(0, gchron_datetime_compare(&dt, &back));

  // Hyphens are not this format.
  const char * hyphens = "2026-09-20 15:30:45";
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_interop_from_exif(hyphens, 19, &back));

  // Many cameras write all spaces or all NULs for "unknown".
  const char * blank = "                   ";
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_interop_from_exif(blank, 19, &back));
  const char * colons = "    :  :     :  :  ";
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED,
      gchron_interop_from_exif(colons, 19, &back));

  // Wrong length, and fields that are not a date.
  EXPECT_EQ(GCHRON_ERR_FORMAT, gchron_interop_from_exif("2026:09:20", 10,
      &back));
  EXPECT_EQ(GCHRON_ERR_FORMAT,
      gchron_interop_from_exif("2026:13:20 15:30:45", 19, &back));
}

/*
 * time_t is 32 bits on some ABIs, and this is the one place a 64-bit library
 * still meets 2038 (mistake M6).
 */
TEST(Interop, TimeTRefusesWhatItCannotHoldRatherThanWrapping) {
  GCHRON_Instant i = at_utc(2026, 9, 20, 15, 30, 0);
  time_t value = 0;
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_time_t(&i, &value));
  EXPECT_EQ(1789918200, static_cast<int64_t>(value));

  if (sizeof(time_t) == 4) {
    GCHRON_Instant far = at_utc(2040, 1, 1, 0, 0, 0);
    EXPECT_EQ(GCHRON_ERR_RANGE, gchron_interop_to_time_t(&far, &value));
  }
  else {
    // A 64-bit time_t holds everything an instant does, so the check cannot
    // trip here - which is worth saying rather than leaving as a silent gap.
    GCHRON_Instant far = at_utc(9999, 1, 1, 0, 0, 0);
    EXPECT_EQ(GCHRON_OK, gchron_interop_to_time_t(&far, &value));
  }
}

/*
 * tm_mon is 0-based and tm_year is 1900-based, which is mistake M7 in its
 * original form.
 */
TEST(Interop, StructTmsOffsetsAreUndone) {
  GCHRON_DateTime dt = gchrontest::datetime(2026, 9, 20, 15, 30, 45);
  struct tm parts{};
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_tm(&dt, 2 * 3600, &parts));

  EXPECT_EQ(126, parts.tm_year) << "2026 - 1900";
  EXPECT_EQ(8, parts.tm_mon) << "September, 0-based";
  EXPECT_EQ(20, parts.tm_mday);
  EXPECT_EQ(0, parts.tm_wday) << "a Sunday, which tm numbers 0";
  EXPECT_EQ(262, parts.tm_yday) << "the 263rd day, 0-based";
  // -1 means "not known". This is a civil reading with no zone, and claiming
  // to know would be mistake M4 exactly.
  EXPECT_EQ(-1, parts.tm_isdst);

  GCHRON_DateTime back{};
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_tm(&parts, &back));
  EXPECT_EQ(0, gchron_datetime_compare(&dt, &back));
}

TEST(Interop, EveryConversionRoundTripsOverASpreadOfInstants) {
  const int64_t seconds[] = {
    0, 1, -1, 1758382200, 951782400, -2208988800, 2145916800,
  };
  for (int64_t s : seconds) {
    GCHRON_Instant original{};
    ASSERT_EQ(GCHRON_OK, gchron_instant_create(s, 0, &original));
    GCHRON_Instant back{};

    uint64_t filetime = 0;
    if (gchron_interop_to_filetime(&original, &filetime) == GCHRON_OK) {
      ASSERT_EQ(GCHRON_OK, gchron_interop_from_filetime(filetime, &back));
      EXPECT_EQ(0, gchron_instant_compare(&original, &back)) << s;
    }

    int64_t ticks = 0;
    ASSERT_EQ(GCHRON_OK, gchron_interop_to_dotnet_ticks(&original, &ticks));
    ASSERT_EQ(GCHRON_OK, gchron_interop_from_dotnet_ticks(ticks, &back));
    EXPECT_EQ(0, gchron_instant_compare(&original, &back)) << s;

    uint32_t hfs = 0;
    if (gchron_interop_to_hfs_plus(&original, &hfs) == GCHRON_OK) {
      ASSERT_EQ(GCHRON_OK, gchron_interop_from_hfs_plus(hfs, &back));
      EXPECT_EQ(0, gchron_instant_compare(&original, &back)) << s;
    }

    /*
     * The two `double` encodings round-trip to about a microsecond and no
     * better, and the assertion says so rather than pretending otherwise. A
     * `double` has 53 bits of mantissa; a Modified Julian Date near today
     * spends sixteen of them on the day number, leaving about 86400/2^36
     * seconds - roughly 1.3 microseconds - of resolution in the fraction.
     * That is a property of the encoding, not of this conversion, and
     * gchron_instant_as_double() documents the same limit for the same
     * reason.
     */
    double mjd = 0;
    ASSERT_EQ(GCHRON_OK, gchron_interop_to_mjd(&original, &mjd));
    ASSERT_EQ(GCHRON_OK, gchron_interop_from_mjd(mjd, &back));
    {
      int64_t drift = (back.sec - original.sec) * 1000000000
          + (back.nsec - original.nsec);
      EXPECT_LT(drift < 0 ? -drift : drift, 2000) << "MJD at " << s;
    }

    double cocoa = 0;
    ASSERT_EQ(GCHRON_OK, gchron_interop_to_cocoa(&original, &cocoa));
    ASSERT_EQ(GCHRON_OK, gchron_interop_from_cocoa(cocoa, &back));
    {
      int64_t drift = (back.sec - original.sec) * 1000000000
          + (back.nsec - original.nsec);
      EXPECT_LT(drift < 0 ? -drift : drift, 2000) << "Cocoa at " << s;
    }
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/*
 * The four entry points that take a `double` each guard the cast that follows
 * them, and nothing tested those guards.  They are not ordinary range checks:
 * `(int64_t)d` for a `d` outside the type is undefined behaviour, so the guard
 * is the only thing standing between a caller's number and UB, and the
 * comparison form `!(v > lo && v < hi)` is written that way so NaN - which
 * fails every comparison - is refused too.
 *
 * What is asserted is refusal, not a particular code, because the four refuse
 * at two different places for two different reasons.  A negative Excel serial
 * is GCHRON_ERR_INVALID from `!(serial >= 1.0)` - an Excel serial below 1 is a
 * wrong argument, not an unrepresentable one - while a negative MJD reaches
 * the finite-range test and is GCHRON_ERR_RANGE.  Both are correct and only
 * one property is common to them: the cast never happens.  Pinning the code
 * instead would have made this a test of which guard fires first.
 *
 * Worth pinning because the failure is invisible without help: GCC does not
 * put float-cast-overflow in `-fsanitize=undefined`, so before the check was
 * named in the Makefile the sanitizer build would have run straight past a
 * removed guard and reported a clean suite.
 */
TEST(Interop, EveryDoubleEntryPointRefusesWhatItCannotCast) {
  GCHRON_DateTime dt{};
  GCHRON_Instant i{};

  const double outside[] = {
    1e30, -1e30,
    std::numeric_limits<double>::infinity(),
    -std::numeric_limits<double>::infinity(),
    std::numeric_limits<double>::quiet_NaN(),
  };

  for (double value : outside) {
    EXPECT_NE(GCHRON_OK, gchron_interop_from_excel_1900(value, &dt))
        << "excel_1900 accepted " << value;
    EXPECT_NE(GCHRON_OK, gchron_interop_from_excel_1904(value, &dt))
        << "excel_1904 accepted " << value;
    EXPECT_NE(GCHRON_OK, gchron_interop_from_cocoa(value, &i))
        << "cocoa accepted " << value;
    EXPECT_NE(GCHRON_OK, gchron_interop_from_mjd(value, &i))
        << "mjd accepted " << value;
  }

  // The two that reach the finite-range test say so, which is the distinction
  // GCHRON_ERR_RANGE exists to draw: the argument is not wrong, it is outside
  // what the type can hold.
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_interop_from_mjd(1e30, &i));
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_interop_from_cocoa(1e30, &i));
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_interop_from_excel_1900(1e30, &dt));

  // The boundary is exclusive, and a value inside it still works.
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_interop_from_mjd(1e12, &i));
  EXPECT_EQ(GCHRON_OK, gchron_interop_from_mjd(0.0, &i));
}

/*--------------------------------------------------------------------------*
 * ASN.1 - X.509, CMS, LDAP
 *--------------------------------------------------------------------------*/

namespace {

struct Asn1Vector {
  std::string encoded;
  int64_t epoch;
};

/*
 * The vectors are the notBefore and notAfter fields of the certificates in
 * this machine's CA bundle, and the expected instant on each line is
 * OpenSSL's reading of the string rather than ours. A corpus generated from
 * our own writer would agree with our own reader about any shared mistake,
 * and a two-digit year is exactly where to make one.
 */
std::vector<Asn1Vector> LoadAsn1Vectors() {
  std::vector<Asn1Vector> out;
  std::ifstream in(GCHRON_TEST_DATA "/asn1/ca_bundle_times.txt");
  std::string line;

  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream parts(line);
    Asn1Vector v;
    if (parts >> v.encoded >> v.epoch) {
      out.push_back(v);
    }
  }
  return out;
}

} // namespace

TEST(Asn1, ReadsEveryTimeInTheCertificateBundle) {
  const std::vector<Asn1Vector> vectors = LoadAsn1Vectors();
  ASSERT_GT(vectors.size(), 200u)
      << "the vector file did not load; this test would otherwise pass by "
      << "checking nothing";

  size_t utctime = 0;
  size_t gentime = 0;
  size_t nineteen = 0;
  for (const Asn1Vector & v : vectors) {
    GCHRON_Instant got{};
    GCHRON_Result result;

    if (v.encoded.size() == 13) {
      result = gchron_interop_from_asn1_utctime(v.encoded.data(),
          v.encoded.size(), GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, &got);
      utctime += 1;
      if (v.encoded[0] >= '5') {
        nineteen += 1;
      }
    }
    else {
      result = gchron_interop_from_asn1_gentime(v.encoded.data(),
          v.encoded.size(), &got);
      gentime += 1;
    }
    ASSERT_EQ(GCHRON_OK, result) << v.encoded;
    EXPECT_EQ(v.epoch, got.sec) << v.encoded
        << " read as " << got.sec << ", OpenSSL says " << v.epoch;
    EXPECT_EQ(0, got.nsec);
  }

  /*
   * Both sides of the pivot have to be present or the corpus is not testing
   * the thing it exists for. The bundle is mostly 20xx, and the handful of
   * 19xx roots are what keep this honest.
   */
  EXPECT_GT(utctime, 200u);
  EXPECT_GE(gentime, 2u);
  EXPECT_GE(nineteen, 2u)
      << "no 19xx vector in the corpus, so the pivot is untested";
}

TEST(Asn1, TheCenturyPivotIsTheCallersToChoose) {
  GCHRON_Instant got{};
  const char * text = "490101000000Z";

  /* RFC 5280: below 50 is 20xx, 50 and above is 19xx. So 49 is 2049. */
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_asn1_utctime(text, 13,
      GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, &got));
  GCHRON_DateTime dt{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&got, &dt));
  EXPECT_EQ(2049, dt.date.year);

  /* A different pivot is a different century, which is the caller's call. */
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_asn1_utctime(text, 13, 40, &got));
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&got, &dt));
  EXPECT_EQ(1949, dt.date.year)
      << "with a pivot of 40, a year of 49 is in the 1900s";

  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_interop_from_asn1_utctime(text, 13,
      100, &got));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_interop_from_asn1_utctime(text, 13,
      -1, &got));

  /*
   * The pivot year itself, which is the whole off-by-one. RFC 5280 says
   * "less than 50" is 20xx, so 50 is 1950 and the window is 1950..2049.
   *
   * Nothing tested this until a mutant changed `<` to `<=` and every test
   * still passed: the certificate corpus happens to contain no year 50, and
   * the cases above use 49 and 40. A boundary that no vector lands on is a
   * boundary no corpus can check.
   */
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_asn1_utctime("500101000000Z", 13,
      GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, &got));
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&got, &dt));
  EXPECT_EQ(1950, dt.date.year)
      << "the pivot year is the first of the 1900s, not the last of the "
      << "2000s";

  ASSERT_EQ(GCHRON_OK, gchron_interop_from_asn1_utctime("491231235959Z", 13,
      GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, &got));
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&got, &dt));
  EXPECT_EQ(2049, dt.date.year) << "and 49 is the last of the 2000s";
}

TEST(Asn1, EveryVectorRoundTripsThroughTheWriter) {
  const std::vector<Asn1Vector> vectors = LoadAsn1Vectors();
  ASSERT_GT(vectors.size(), 200u);

  for (const Asn1Vector & v : vectors) {
    GCHRON_Instant parsed{};
    if (v.encoded.size() == 13) {
      ASSERT_EQ(GCHRON_OK, gchron_interop_from_asn1_utctime(v.encoded.data(),
          v.encoded.size(), GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, &parsed));
      char buf[GCHRON_ASN1_UTCTIME_BYTES];
      const GCHRON_Result wrote = gchron_interop_to_asn1_utctime(&parsed,
          GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, buf);
      if (wrote == GCHRON_ERR_RANGE) {
        /* Outside the hundred years this pivot names; refused, not wrong. */
        continue;
      }
      ASSERT_EQ(GCHRON_OK, wrote) << v.encoded;
      EXPECT_EQ(v.encoded, std::string(buf));
    }
    else {
      ASSERT_EQ(GCHRON_OK, gchron_interop_from_asn1_gentime(v.encoded.data(),
          v.encoded.size(), &parsed));
      char buf[GCHRON_ASN1_GENTIME_BYTES];
      ASSERT_EQ(GCHRON_OK, gchron_interop_to_asn1_gentime(&parsed, buf));
      EXPECT_EQ(v.encoded, std::string(buf));
    }
  }
}

/*
 * DER restricts what BER allows, and these are the restrictions two
 * implementations most often differ on.
 */
TEST(Asn1, TheWriterIsDerStrict) {
  GCHRON_Instant i{};
  char buf[GCHRON_ASN1_GENTIME_BYTES];

  /* A fraction that would be zero is omitted, along with its point. */
  i.sec = 1000000000;
  i.nsec = 0;
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_asn1_gentime(&i, buf));
  EXPECT_EQ("20010909014640Z", std::string(buf));
  EXPECT_EQ(nullptr, std::strchr(buf, '.'))
      << "DER omits a zero fraction rather than writing .000";

  /* A fraction never ends in a zero. */
  i.nsec = 500000000;
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_asn1_gentime(&i, buf));
  EXPECT_EQ("20010909014640.5Z", std::string(buf));

  i.nsec = 123000000;
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_asn1_gentime(&i, buf));
  EXPECT_EQ("20010909014640.123Z", std::string(buf));

  i.nsec = 123456789;
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_asn1_gentime(&i, buf));
  EXPECT_EQ("20010909014640.123456789Z", std::string(buf));

  /* UTCTime has no fraction at all, so one is refused rather than dropped. */
  char small[GCHRON_ASN1_UTCTIME_BYTES];
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_interop_to_asn1_utctime(&i,
      GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, small))
      << "silently dropping the fraction would move the value";
}

TEST(Asn1, TheReaderAcceptsTheLooserBerSpellings) {
  GCHRON_Instant strict{};
  GCHRON_Instant loose{};

  ASSERT_EQ(GCHRON_OK, gchron_interop_from_asn1_gentime("20010909014640.5Z",
      17, &strict));

  /* BER allows a comma for the decimal point; DER allows only a full stop. */
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_asn1_gentime("20010909014640,5Z",
      17, &loose));
  EXPECT_EQ(0, gchron_instant_compare(&strict, &loose));

  /* Seconds may be absent. */
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_asn1_gentime("200109090146Z", 13,
      &loose));
  GCHRON_DateTime dt{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_to_utc(&loose, &dt));
  EXPECT_EQ(0, dt.time.second);

  /* An offset rather than Z, which older certificates carry. */
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_asn1_utctime("010909014640Z", 13,
      GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, &strict));
  ASSERT_EQ(GCHRON_OK, gchron_interop_from_asn1_utctime("010909034640+0200",
      17, GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, &loose));
  EXPECT_EQ(0, gchron_instant_compare(&strict, &loose))
      << "+0200 means two hours ahead of UTC, so it names the same instant";

  ASSERT_EQ(GCHRON_OK, gchron_interop_from_asn1_utctime("010908234640-0200",
      17, GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, &loose));
  EXPECT_EQ(0, gchron_instant_compare(&strict, &loose));
}

TEST(Asn1, ALocalTimeWithNoZoneIsRefusedRatherThanAssumedUtc) {
  GCHRON_Instant got{};

  /* BER calls this local time. It names no instant without knowing where it
   * was written, so it is refused (design.md, mistake M2). */
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, gchron_interop_from_asn1_gentime(
      "20010909014640", 14, &got));
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, gchron_interop_from_asn1_utctime(
      "010909014640", 12, GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, &got));
}

TEST(Asn1, MalformedInputIsFormatNotACrash) {
  GCHRON_Instant got{};
  const char * bad[] = {
    "", "Z", "01", "0109090146", "01090901464xZ", "013209014640Z",
    "010909014640X", "010909014640+99", "010909014640+2500",
    "010909254640Z", "010909016040Z", "20010909014640.Z",
    "20010909014640.5", "010909014640ZZ"
  };

  for (const char * text : bad) {
    const size_t len = std::strlen(text);
    const GCHRON_Result a = gchron_interop_from_asn1_utctime(text, len,
        GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, &got);
    const GCHRON_Result b = gchron_interop_from_asn1_gentime(text, len, &got);
    EXPECT_NE(GCHRON_OK, a) << "UTCTime accepted \"" << text << "\"";
    EXPECT_NE(GCHRON_OK, b) << "GeneralizedTime accepted \"" << text << "\"";
  }

  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_interop_from_asn1_utctime(nullptr, 0,
      GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, &got));
  EXPECT_EQ(GCHRON_ERR_INVALID, gchron_interop_from_asn1_gentime("x", 1,
      nullptr));
}

TEST(Asn1, AYearOutsideThePivotsWindowIsRefusedNotMiswritten) {
  GCHRON_Instant i{};
  char buf[GCHRON_ASN1_UTCTIME_BYTES];
  GCHRON_DateTime dt{};

  /* 2050 is outside 1950..2049, which is why X.509 switches to
   * GeneralizedTime there. */
  ASSERT_EQ(GCHRON_OK, gchron_date_create(2050, 1, 1, &dt.date));
  dt.time.hour = 0;
  dt.time.minute = 0;
  dt.time.second = 0;
  dt.time.nsec = 0;
  ASSERT_EQ(GCHRON_OK, gchron_instant_from_utc(&dt, &i));
  EXPECT_EQ(GCHRON_ERR_RANGE, gchron_interop_to_asn1_utctime(&i,
      GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, buf))
      << "writing 500101... would read back as 1950";

  /* And the last year that does fit. */
  ASSERT_EQ(GCHRON_OK, gchron_date_create(2049, 12, 31, &dt.date));
  dt.time.hour = 23;
  dt.time.minute = 59;
  dt.time.second = 59;
  ASSERT_EQ(GCHRON_OK, gchron_instant_from_utc(&dt, &i));
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_asn1_utctime(&i,
      GCHRON_ASN1_UTCTIME_PIVOT_RFC5280, buf));
  EXPECT_EQ("491231235959Z", std::string(buf));

  /* GeneralizedTime has a four-digit year and so has no such edge. */
  char big[GCHRON_ASN1_GENTIME_BYTES];
  ASSERT_EQ(GCHRON_OK, gchron_date_create(2050, 1, 1, &dt.date));
  dt.time.hour = 0;
  dt.time.minute = 0;
  dt.time.second = 0;
  ASSERT_EQ(GCHRON_OK, gchron_instant_from_utc(&dt, &i));
  ASSERT_EQ(GCHRON_OK, gchron_interop_to_asn1_gentime(&i, big));
  EXPECT_EQ("20500101000000Z", std::string(big));
}
