#include "vellum/battery_log.h"

#include <cstdio>
#include <new>

#include "vellum/textfile.h"

namespace vellum {
namespace {

constexpr uint32_t kFileBytes = 64 * 1024;  // kMaxSamples lines of about 20 bytes

// Days from 2000-01-01 to y-m-d (proleptic Gregorian), after Howard Hinnant's days_from_civil.
uint32_t days_from_date(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = y / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<uint32_t>(era * 146097 + static_cast<int>(doe) - 730425);  // 730425: 0000-03-01 to 2000-01-01
}

char kind_char(BatterySample::Kind k) {
  switch (k) {
    case BatterySample::Kind::Sleep:
      return 's';
    case BatterySample::Kind::Wake:
      return 'w';
    case BatterySample::Kind::Awake:
      break;
  }
  return 'a';
}

}  // namespace

uint32_t seconds_since_2000(const DateTime& t) {
  return days_from_date(t.year, t.month, t.day) * 86400u + t.hour * 3600u + t.minute * 60u + t.second;
}

void date_from_days(uint32_t days, uint16_t& year, uint8_t& month, uint8_t& day) {
  // Hinnant's civil_from_days, counted from 2000-01-01.
  const int z = static_cast<int>(days) + 730425;
  const int era = z / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  year = static_cast<uint16_t>(static_cast<int>(yoe) + era * 400 + (m <= 2));
  month = static_cast<uint8_t>(m);
  day = static_cast<uint8_t>(d);
}

BatteryLog::BatteryLog() : samples_(new(std::nothrow) BatterySample[kMaxSamples]) {}

BatteryLog::~BatteryLog() { delete[] samples_; }

void BatteryLog::add(const BatterySample& s) {
  if (samples_ == nullptr) return;
  // The clock was set back: forget what now looks like the future.
  while (count_ > 0 && latest()->time > s.time) --count_;
  if (count_ == kMaxSamples) {
    first_ = (first_ + 1) % kMaxSamples;
    --count_;
  }
  samples_[(first_ + count_) % kMaxSamples] = s;
  ++count_;
}

void BatteryLog::load(Storage& storage) {
  count_ = 0;
  first_ = 0;
  static char* buf = new (std::nothrow) char[kFileBytes];
  if (buf == nullptr) return;
  const int32_t n = textfile::read(storage, kPath, buf, kFileBytes);
  if (n <= 0) return;
  textfile::for_each_line(std::string_view(buf, static_cast<size_t>(n)), [&](const std::string_view* f, int count) {
    if (count < 4) return;
    BatterySample s;
    s.time = textfile::to_uint(f[0]);
    s.percent = static_cast<uint8_t>(textfile::to_uint(f[1]) > 100 ? 100 : textfile::to_uint(f[1]));
    s.kind = f[2] == "s"   ? BatterySample::Kind::Sleep
             : f[2] == "w" ? BatterySample::Kind::Wake
                           : BatterySample::Kind::Awake;
    s.charging = f[3] == "c";
    add(s);
  });
}

bool BatteryLog::save(Storage& storage) const {
  static char* buf = new (std::nothrow) char[kFileBytes];
  if (buf == nullptr) return false;
  size_t n = 0;
  for (int i = 0; i < count_ && n + 32 < kFileBytes; ++i) {
    const BatterySample& s = at(i);
    n += static_cast<size_t>(std::snprintf(buf + n, kFileBytes - n, "%lu\t%u\t%c\t%c\n",
                                           static_cast<unsigned long>(s.time), s.percent, kind_char(s.kind),
                                           s.charging ? 'c' : 'b'));
  }
  return storage.write(kPath, buf, static_cast<uint32_t>(n));
}

BatteryEstimate BatteryLog::estimate() const {
  // Add up the drop and the time over each stretch between two samples on
  // battery. A stretch that starts with going to sleep counts as asleep.
  uint32_t awake_s = 0, asleep_s = 0;
  uint32_t awake_drop = 0, asleep_drop = 0;
  for (int i = 1; i < count_; ++i) {
    const BatterySample& a = at(i - 1);
    const BatterySample& b = at(i);
    if (a.charging || b.charging || b.percent > a.percent || b.time <= a.time) continue;
    const uint32_t dt = b.time - a.time;
    const uint32_t drop = a.percent - b.percent;
    if (a.kind == BatterySample::Kind::Sleep) {
      asleep_s += dt;
      asleep_drop += drop;
    } else if (dt <= 3 * BatteryLog::kIntervalS) {
      awake_s += dt;
      awake_drop += drop;
    }
    // Longer gaps while awake mean samples went missing (a restart or a
    // power loss): they say nothing reliable about either state.
  }

  BatteryEstimate e;
  constexpr uint32_t kHour = 3600;
  if (awake_s >= kHour && awake_drop >= 1)
    e.awake_tenths_per_hour = static_cast<int>(awake_drop * 10 * kHour / awake_s);
  if (asleep_s >= 12 * kHour)
    e.asleep_tenths_per_day = static_cast<int>(uint64_t{asleep_drop} * 10 * 24 * kHour / asleep_s);
  const uint32_t total_s = awake_s + asleep_s;
  const uint32_t total_drop = awake_drop + asleep_drop;
  if (const BatterySample* last = latest(); last != nullptr && total_s >= 6 * kHour && total_drop >= 2) {
    e.hours_left = static_cast<int>(uint64_t{last->percent} * total_s / (uint64_t{total_drop} * kHour));
  }
  return e;
}

}  // namespace vellum
