/**
 * @file
 *
 * Random sequences of operations on random values, for UBSan.
 *
 * design.md section 7.1: there is no signed overflow in this library, and
 * `make test-asan`'s UBSan half is the gate that proves it. A unit test can
 * only assert the overflow cases somebody thought of; this drives the
 * arithmetic with values nobody chose, which is the only way to find the one
 * that was missed (design.md, mistake M24).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <ghoti.io/chron/chron.h>

namespace {

/** Take some bytes off the front of the input, or zeroes when it runs out. */
class Bytes {
public:
  Bytes(const uint8_t * data, size_t size) : data_(data), size_(size) {}

  template <typename T> T take() {
    T value{};
    if (size_ >= sizeof(T)) {
      std::memcpy(&value, data_, sizeof(T));
      data_ += sizeof(T);
      size_ -= sizeof(T);
    }
    return value;
  }

  size_t remaining() const { return size_; }

private:
  const uint8_t * data_;
  size_t size_;
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  Bytes bytes(data, size);

  while (bytes.remaining() >= 9) {
    const uint8_t op = bytes.take<uint8_t>();
    const int64_t a = bytes.take<int64_t>();

    switch (op % 12) {
      case 0: {
        GCHRON_Date date;
        gchron_date_from_epoch_day(a, &date);
        break;
      }
      case 1: {
        GCHRON_Date date;
        GCHRON_Date moved;
        int64_t day = 0;
        date.year = static_cast<int32_t>(a);
        date.month = static_cast<uint8_t>(a >> 32);
        date.day = static_cast<uint8_t>(a >> 40);
        gchron_date_to_epoch_day(&date, &day);
        gchron_date_add_days(&date, bytes.take<int64_t>(), &moved);
        break;
      }
      case 2: {
        GCHRON_Instant i;
        gchron_instant_normalize(a, bytes.take<int64_t>(), &i);
        break;
      }
      case 3: {
        GCHRON_Instant i;
        GCHRON_DateTime dt;
        if (gchron_instant_create(a, static_cast<int32_t>(
                bytes.take<uint32_t>() % 1000000000u), &i) == GCHRON_OK) {
          gchron_instant_to_utc(&i, &dt);
        }
        break;
      }
      case 4: {
        GCHRON_Duration d{};
        GCHRON_Instant i{};
        GCHRON_Instant out;
        d.hours = a;
        d.minutes = bytes.take<int64_t>();
        d.seconds = bytes.take<int64_t>();
        i.sec = bytes.take<int64_t>();
        gchron_instant_add(&i, &d, &out);
        gchron_instant_subtract(&i, &d, &out);
        break;
      }
      case 5: {
        GCHRON_Instant x{};
        GCHRON_Instant y{};
        GCHRON_Duration d;
        x.sec = a;
        y.sec = bytes.take<int64_t>();
        gchron_instant_until(&x, &y, &d);
        break;
      }
      case 6: {
        int64_t out = 0;
        gchron_epoch_day_to_jdn(a, &out);
        gchron_jdn_to_epoch_day(a, &out);
        gchron_epoch_day_to_rd(a, &out);
        gchron_rd_to_epoch_day(a, &out);
        break;
      }
      case 7: {
        GCHRON_Date out;
        gchron_date_nth_weekday(static_cast<int32_t>(a),
            static_cast<int>(a >> 32) % 20, static_cast<int>(a >> 40) % 12,
            static_cast<int>(a >> 48) % 14 - 7, &out);
        break;
      }
      case 8: {
        GCHRON_IsoWeekDate week;
        GCHRON_Date out;
        week.week_year = static_cast<int32_t>(a);
        week.week = static_cast<uint8_t>(a >> 32);
        week.day = static_cast<uint8_t>(a >> 40);
        gchron_date_from_iso_week(&week, &out);
        break;
      }
      case 9: {
        GCHRON_Duration d{};
        GCHRON_Duration out;
        d.years = a;
        d.months = bytes.take<int64_t>();
        d.days = bytes.take<int64_t>();
        gchron_duration_negate(&d, &out);
        gchron_duration_is_valid(&d);
        break;
      }
      case 10: {
        GCHRON_Instant i{};
        int64_t out = 0;
        double value = 0;
        i.sec = a;
        i.nsec = static_cast<int32_t>(bytes.take<uint32_t>() % 1000000000u);
        gchron_instant_to_unix_millis(&i, &out);
        gchron_instant_to_unix_micros(&i, &out);
        gchron_instant_to_unix_nanos(&i, &out);
        gchron_instant_as_double(&i, &value);
        break;
      }
      case 11: {
        GCHRON_Time t;
        gchron_time_from_nanos_of_day(a, &t);
        break;
      }
      default:
        break;
    }
  }
  return 0;
}
