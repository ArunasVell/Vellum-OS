// Battery history: the charge level over time, kept on the card so real
// battery life can be measured. Samples are taken while the reader is in
// use, just before it sleeps and when it wakes, which separates the drain
// of reading from the drain of sleeping.
#pragma once

#include <cstdint>

#include "vellum/hal.h"

namespace vellum {

// Seconds from 2000-01-01 00:00 to `t`, both local time.
uint32_t seconds_since_2000(const DateTime& t);
// The calendar date `days` days after 2000-01-01.
void date_from_days(uint32_t days, uint16_t& year, uint8_t& month, uint8_t& day);

struct BatterySample {
  enum class Kind : uint8_t {
    Awake,  // a reading taken while in use
    Sleep,  // just before sleeping
    Wake,   // on starting or waking
  };
  uint32_t time = 0;  // seconds_since_2000
  uint8_t percent = 0;
  Kind kind = Kind::Awake;
  bool charging = false;
};

// Drain worked out from the log; -1 where there is not enough data yet.
struct BatteryEstimate {
  int awake_tenths_per_hour = -1;  // percent per hour while in use, x10
  int asleep_tenths_per_day = -1;  // percent per day while asleep, x10
  int hours_left = -1;             // at the overall rate, from the latest level
};

class BatteryLog {
 public:
  static constexpr int kMaxSamples = 2048;             // a few weeks
  static constexpr uint32_t kIntervalS = 10 * 60;      // between readings while in use
  static constexpr uint32_t kSaveIntervalS = 60 * 60;  // card writes while in use
  static constexpr const char* kPath = "vellum/battery.txt";

  BatteryLog();
  ~BatteryLog();
  BatteryLog(const BatteryLog&) = delete;
  BatteryLog& operator=(const BatteryLog&) = delete;

  void load(Storage& storage);
  bool save(Storage& storage) const;
  // Adds a sample; the oldest is dropped when the log is full. Samples
  // newer than this one (the clock was set back) are dropped.
  void add(const BatterySample& s);
  void clear() { count_ = 0; }

  int count() const { return count_; }
  // Oldest first.
  const BatterySample& at(int i) const { return samples_[(first_ + i) % kMaxSamples]; }
  const BatterySample* latest() const { return count_ > 0 ? &at(count_ - 1) : nullptr; }

  BatteryEstimate estimate() const;

 private:
  BatterySample* samples_;
  int first_ = 0;
  int count_ = 0;
};

}  // namespace vellum
