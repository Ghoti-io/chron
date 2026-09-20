/**
 * @file
 *
 * Duration arithmetic on random values.
 *
 * design.md section 12.2 lists this harness, and it earns its place by
 * asserting the two properties the arithmetic promises rather than merely
 * surviving:
 *
 * 1. Every duration this library produces is **valid** - every non-zero field
 *    shares one sign. A `until` or a `balance` that let two fields disagree
 *    would hand the caller a value no operation here accepts.
 * 2. `from + until(from, to) == to`, for every pair and every largest unit.
 *    That is the whole contract of a difference, and it is the property that
 *    caught `until` composing its units differently from `add`.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <ghoti.io/chron/calendar.h>
#include <ghoti.io/chron/chron.h>

namespace {

/** Take bytes off the front of the input, or zeroes when it runs out. */
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

/** A civil date-time inside the range the arithmetic is interesting over. */
bool make_datetime(Bytes & bytes, GCHRON_DateTime * out) {
  int32_t year = 1 + static_cast<int32_t>(bytes.take<uint16_t>() % 9999u);
  int month = 1 + bytes.take<uint8_t>() % 12;
  int day = 1 + bytes.take<uint8_t>() % 31;
  int hour = bytes.take<uint8_t>() % 24;
  int minute = bytes.take<uint8_t>() % 60;
  int second = bytes.take<uint8_t>() % 60;
  int32_t nsec =
      static_cast<int32_t>(bytes.take<uint32_t>() % 1000000000u);

  if (gchron_date_create(year, month, day, &out->date) != GCHRON_OK) {
    return false;
  }
  return gchron_time_create(hour, minute, second, nsec, &out->time)
      == GCHRON_OK;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 24) {
    return 0;
  }
  Bytes bytes(data, size);

  const uint8_t selector = bytes.take<uint8_t>();
  const GCHRON_Calendar * calendar =
      (selector & 1) ? gchron_calendar_julian() : nullptr;
  const GCHRON_Overflow overflow = (selector & 2)
      ? GCHRON_OVERFLOW_CONSTRAIN : GCHRON_OVERFLOW_REJECT;

  GCHRON_DateTime from{};
  GCHRON_DateTime to{};
  if (!make_datetime(bytes, &from) || !make_datetime(bytes, &to)) {
    return 0;
  }

  static const GCHRON_Unit kUnits[] = {
    GCHRON_UNIT_NANOSECOND, GCHRON_UNIT_SECOND, GCHRON_UNIT_MINUTE,
    GCHRON_UNIT_HOUR, GCHRON_UNIT_DAY, GCHRON_UNIT_WEEK, GCHRON_UNIT_MONTH,
    GCHRON_UNIT_YEAR,
  };

  for (GCHRON_Unit unit : kUnits) {
    GCHRON_Duration difference{};
    if (gchron_datetime_until(&from, &to, unit, calendar, &difference)
        != GCHRON_OK) {
      continue;
    }
    // Property 1: what this library produces, this library accepts.
    assert(gchron_duration_is_valid(&difference));

    // Property 2: the difference adds back up to its own endpoint. Under
    // CONSTRAIN, which is what `until` measured with.
    GCHRON_DateTime back{};
    if (gchron_datetime_add(&from, &difference, calendar,
            GCHRON_OVERFLOW_CONSTRAIN, &back) == GCHRON_OK) {
      assert(gchron_datetime_compare(&to, &back) == 0);
    }

    // Balancing and rounding must also produce something valid.
    GCHRON_Duration balanced{};
    if (gchron_duration_balance(&difference, unit, &from, calendar, &balanced)
        == GCHRON_OK) {
      assert(gchron_duration_is_valid(&balanced));
    }
    for (int mode = 0; mode <= GCHRON_ROUND_HALF_EVEN; ++mode) {
      GCHRON_Duration rounded{};
      if (gchron_duration_round(&difference, unit,
              static_cast<GCHRON_Rounding>(mode), &from, calendar, &rounded)
          == GCHRON_OK) {
        assert(gchron_duration_is_valid(&rounded));
      }
    }
  }

  // And an arbitrary duration applied to an arbitrary date-time.
  {
    GCHRON_Duration arbitrary{};
    arbitrary.years = bytes.take<int16_t>();
    arbitrary.months = bytes.take<int16_t>();
    arbitrary.days = bytes.take<int16_t>();
    arbitrary.hours = bytes.take<int16_t>();
    if (gchron_duration_is_valid(&arbitrary)) {
      GCHRON_DateTime moved{};
      gchron_datetime_add(&from, &arbitrary, calendar, overflow, &moved);
      gchron_datetime_subtract(&from, &arbitrary, calendar, overflow, &moved);
    }
  }

  // The text grammars, round-tripped.
  {
    GCHRON_Duration parsed{};
    char text[GCHRON_RFC3339_DURATION_MAX];
    size_t length = 0;
    const char * input = reinterpret_cast<const char *>(data);
    if (gchron_parse_iso8601_duration(input, size, nullptr, &parsed, nullptr,
            nullptr) == GCHRON_OK) {
      assert(gchron_duration_is_valid(&parsed));
      if (gchron_write_iso8601_duration(&parsed, text, sizeof(text), &length)
          == GCHRON_OK) {
        GCHRON_Duration again{};
        assert(gchron_parse_iso8601_duration(text, length, nullptr, &again,
                   nullptr, nullptr) == GCHRON_OK);
        assert(gchron_duration_identical(&parsed, &again));
      }
    }
  }
  return 0;
}
