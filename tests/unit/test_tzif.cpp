/**
 * @file
 *
 * The TZif reader, on files that are not zone files.
 *
 * design.md section 1.2: a corrupt or hostile TZif file must produce
 * GCHRON_ERR_CORRUPT or GCHRON_ERR_LIMIT, never a crash and never a zone that
 * silently answers wrongly. The cases below are the ways a reader gets that
 * wrong, one test each, and the first of them is a file the fuzzer found
 * rather than one anybody thought of.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ghoti.io/chron/chron.h>

#include "test_helpers.h"

namespace {

/** Build a minimal but valid version-1 TZif image. */
std::vector<uint8_t> minimal_v1() {
  std::vector<uint8_t> image;
  auto be32 = [&image](uint32_t value) {
    image.push_back(static_cast<uint8_t>(value >> 24));
    image.push_back(static_cast<uint8_t>(value >> 16));
    image.push_back(static_cast<uint8_t>(value >> 8));
    image.push_back(static_cast<uint8_t>(value));
  };
  image.insert(image.end(), { 'T', 'Z', 'i', 'f' });
  image.push_back(0);                      // version 1
  image.insert(image.end(), 15, 0);        // reserved
  be32(0);                                 // isutcnt
  be32(0);                                 // isstdcnt
  be32(0);                                 // leapcnt
  be32(0);                                 // timecnt
  be32(1);                                 // typecnt
  be32(4);                                 // charcnt
  // One ttinfo: UTC, not daylight saving, designation at index 0.
  be32(0);
  image.push_back(0);
  image.push_back(0);
  image.insert(image.end(), { 'U', 'T', 'C', '\0' });
  return image;
}

GCHRON_Result load(const std::vector<uint8_t> & image) {
  GCHRON_ZoneDb * db = nullptr;
  GCHRON_Result result = gchron_zonedb_memory(image.data(), image.size(),
      "test/zone", nullptr, nullptr, &db);
  if (result == GCHRON_OK) {
    gchron_zonedb_destroy(db);
  }
  return result;
}

/** Overwrite a big-endian 32-bit field. */
void poke32(std::vector<uint8_t> & image, size_t at, uint32_t value) {
  image[at] = static_cast<uint8_t>(value >> 24);
  image[at + 1] = static_cast<uint8_t>(value >> 16);
  image[at + 2] = static_cast<uint8_t>(value >> 8);
  image[at + 3] = static_cast<uint8_t>(value);
}

} // namespace

TEST(Tzif, AMinimalFileLoadsAndAnswers) {
  GCHRON_ZoneDb * db = nullptr;
  std::vector<uint8_t> image = minimal_v1();
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_memory(image.data(), image.size(),
      "test/zone", nullptr, nullptr, &db));

  const GCHRON_Zone * zone = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_zone(db, "test/zone", &zone));
  GCHRON_Instant now{};
  ASSERT_EQ(GCHRON_OK, gchron_instant_create(1781539200, 0, &now));
  GCHRON_ZoneInfo info{};
  ASSERT_EQ(GCHRON_OK, gchron_zone_offset_at(zone, now, &info));
  EXPECT_EQ(0, info.offset_sec);
  EXPECT_STREQ("UTC", info.abbreviation);
  gchron_zonedb_destroy(db);
}

/*
 * Found by tests/fuzz/fuzz_tzif.cpp, and the reason the header comment in
 * src/zone/tzif.c now says the counts are bounded by the *file* and not only
 * by the limits.
 *
 * Seventy-four bytes, whose second header claims 987,654,144 transitions. The
 * reader sized an allocation from that count before discovering there were
 * ten bytes left, and asked for eight gigabytes. A limits field would not
 * have saved it: GCHRON_Limits::max_transitions of zero means no limit, which
 * is a legitimate setting, and the file is still only seventy-four bytes.
 */
TEST(Tzif, CountsAreBoundedByTheFileAndNotOnlyByTheLimits) {
  std::string path = gchrontest::data_dir() + "/tzif/oversized_counts.tzif";
  std::ifstream in(path, std::ios::binary);
  ASSERT_TRUE(in.is_open()) << "missing regression fixture: " << path;
  std::string bytes((std::istreambuf_iterator<char>(in)),
      std::istreambuf_iterator<char>());
  ASSERT_EQ(74u, bytes.size());

  GCHRON_Limits unlimited;
  gchron_limits_default(&unlimited);
  unlimited.max_tzif_bytes = 0;
  unlimited.max_transitions = 0;
  unlimited.max_zone_types = 0;

  GCHRON_ZoneDb * db = nullptr;
  EXPECT_EQ(GCHRON_ERR_CORRUPT,
      gchron_zonedb_memory(bytes.data(), bytes.size(), "test/zone", nullptr,
          &unlimited, &db));
}

TEST(Tzif, ATruncatedFileIsCorruptRatherThanShort) {
  std::vector<uint8_t> image = minimal_v1();
  for (size_t length = 0; length < image.size(); ++length) {
    std::vector<uint8_t> cut(image.begin(), image.begin() + length);
    GCHRON_Result result = load(cut);
    EXPECT_NE(GCHRON_OK, result) << "accepted " << length << " bytes";
  }
  EXPECT_EQ(GCHRON_OK, load(image));
}

TEST(Tzif, TheMagicAndTheVersionAreChecked) {
  std::vector<uint8_t> image = minimal_v1();
  image[0] = 'X';
  EXPECT_EQ(GCHRON_ERR_CORRUPT, load(image));

  image = minimal_v1();
  image[4] = '9';
  // A version this reader does not know is refused rather than guessed at: a
  // later one may change the meaning of a field this one reads.
  EXPECT_EQ(GCHRON_ERR_UNSUPPORTED, load(image));
}

// RFC 8536 section 3.1: typecnt and charcnt must not be zero, and the two
// indicator counts must be zero or equal to typecnt.
TEST(Tzif, TheHeaderConstraintsRfc8536StatesAreEnforced) {
  std::vector<uint8_t> image = minimal_v1();
  poke32(image, 20 + 16, 0); // typecnt = 0
  EXPECT_EQ(GCHRON_ERR_CORRUPT, load(image));

  image = minimal_v1();
  poke32(image, 20 + 20, 0); // charcnt = 0
  EXPECT_EQ(GCHRON_ERR_CORRUPT, load(image));

  image = minimal_v1();
  poke32(image, 20 + 0, 2);  // isutcnt = 2, typecnt = 1
  EXPECT_EQ(GCHRON_ERR_CORRUPT, load(image));

  image = minimal_v1();
  poke32(image, 20 + 4, 2);  // isstdcnt = 2, typecnt = 1
  EXPECT_EQ(GCHRON_ERR_CORRUPT, load(image));
}

/*
 * A designation index that is in range but whose string is not terminated
 * inside the block. Reading it would run off the end of the designations and
 * into whatever follows - the classic way a format reader turns a bounds
 * check it *did* write into one it needed and did not.
 */
TEST(Tzif, ADesignationMustBeTerminatedInsideItsOwnBlock) {
  std::vector<uint8_t> image = minimal_v1();
  image[image.size() - 1] = 'X'; // was the NUL after "UTC"
  EXPECT_EQ(GCHRON_ERR_CORRUPT, load(image));
}

TEST(Tzif, ATransitionTypeIndexOutsideTheTypeTableIsCorrupt) {
  std::vector<uint8_t> image;
  auto be32 = [&image](uint32_t v) {
    image.push_back(static_cast<uint8_t>(v >> 24));
    image.push_back(static_cast<uint8_t>(v >> 16));
    image.push_back(static_cast<uint8_t>(v >> 8));
    image.push_back(static_cast<uint8_t>(v));
  };
  image.insert(image.end(), { 'T', 'Z', 'i', 'f' });
  image.push_back(0);
  image.insert(image.end(), 15, 0);
  be32(0); be32(0); be32(0);
  be32(1);  // timecnt
  be32(1);  // typecnt
  be32(4);  // charcnt
  be32(0);  // one transition time
  image.push_back(5);  // its type index, and there is only type 0
  be32(0); image.push_back(0); image.push_back(0);
  image.insert(image.end(), { 'U', 'T', 'C', '\0' });
  EXPECT_EQ(GCHRON_ERR_CORRUPT, load(image));
}

/*
 * RFC 8536 section 3.2: the transition times are in strictly ascending order.
 * A file that breaks it would make the binary search return the wrong type
 * rather than fail, which is the "answers wrongly" half of the threat model
 * and the half a reader is most likely to skip checking.
 */
TEST(Tzif, TransitionTimesMustBeStrictlyAscending) {
  std::vector<uint8_t> image;
  auto be32 = [&image](uint32_t v) {
    image.push_back(static_cast<uint8_t>(v >> 24));
    image.push_back(static_cast<uint8_t>(v >> 16));
    image.push_back(static_cast<uint8_t>(v >> 8));
    image.push_back(static_cast<uint8_t>(v));
  };
  image.insert(image.end(), { 'T', 'Z', 'i', 'f' });
  image.push_back(0);
  image.insert(image.end(), 15, 0);
  be32(0); be32(0); be32(0);
  be32(2);  // two transitions
  be32(1);
  be32(4);
  be32(100);  // out of order: 100 then 50
  be32(50);
  image.push_back(0);
  image.push_back(0);
  be32(0); image.push_back(0); image.push_back(0);
  image.insert(image.end(), { 'U', 'T', 'C', '\0' });
  EXPECT_EQ(GCHRON_ERR_CORRUPT, load(image));
}

TEST(Tzif, AnOffsetThisLibraryCannotHoldIsCorrupt) {
  std::vector<uint8_t> image = minimal_v1();
  // The ttinfo's utoff is the four bytes after the header.
  poke32(image, 44, 0x80000000u); // -2^31, which RFC 8536 forbids outright
  EXPECT_EQ(GCHRON_ERR_CORRUPT, load(image));

  image = minimal_v1();
  poke32(image, 44, 90000u); // past 24 hours
  EXPECT_EQ(GCHRON_ERR_CORRUPT, load(image));
}

TEST(Tzif, TheLimitsAreEnforcedAndZeroMeansNoLimit) {
  std::vector<uint8_t> image = minimal_v1();
  GCHRON_Limits limits;
  GCHRON_ZoneDb * db = nullptr;

  gchron_limits_default(&limits);
  limits.max_tzif_bytes = image.size() - 1;
  EXPECT_EQ(GCHRON_ERR_LIMIT,
      gchron_zonedb_memory(image.data(), image.size(), "z", nullptr, &limits,
          &db));

  limits.max_tzif_bytes = 0;
  EXPECT_EQ(GCHRON_OK,
      gchron_zonedb_memory(image.data(), image.size(), "z", nullptr, &limits,
          &db));
  gchron_zonedb_destroy(db);

  gchron_limits_default(&limits);
  limits.max_zone_types = 0; // no limit, and the file still has to fit
  EXPECT_EQ(GCHRON_OK,
      gchron_zonedb_memory(image.data(), image.size(), "z", nullptr, &limits,
          &db));
  gchron_zonedb_destroy(db);
}

/*
 * Every zone file this machine has, read. Not a differential - the zdump and
 * zoneinfo oracles are that - but a statement that the reader accepts the
 * whole of the real corpus, which a reader made stricter by a bad bounds
 * check would stop doing.
 */
TEST(Tzif, EveryZoneFileOnThisMachineParses) {
  GCHRON_ZoneDb * db = nullptr;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_system(nullptr, nullptr, &db));
  const char * const * ids = nullptr;
  size_t count = 0;
  ASSERT_EQ(GCHRON_OK, gchron_zonedb_list(db, &ids, &count));
  ASSERT_GT(count, 300u);

  size_t loaded = 0;
  for (size_t i = 0; i < count; ++i) {
    const GCHRON_Zone * zone = nullptr;
    EXPECT_EQ(GCHRON_OK, gchron_zonedb_zone(db, ids[i], &zone)) << ids[i];
    if (zone != nullptr) {
      ++loaded;
    }
  }
  EXPECT_EQ(count, loaded);
  std::printf("[          ] %zu zone files parsed\n", loaded);
  gchron_zonedb_destroy(db);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
