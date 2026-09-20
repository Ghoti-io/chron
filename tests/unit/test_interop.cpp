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
#include <cstring>
#include <ctime>
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
