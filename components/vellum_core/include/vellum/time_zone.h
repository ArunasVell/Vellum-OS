// Time zones and summer time. The X4 Pro's clock keeps world time (UTC);
// the reader shows it in its zone, a POSIX TZ rule such as
// "EET-2EEST,M3.5.0/3,M10.5.0/4" (Lithuania), so summer time starts and
// ends by itself. The phone page sends the phone's zone, worked out as such
// a rule, and Settings › General › Time zone picks one by hand.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vellum/hal.h"

namespace vellum {

// A day the clocks change, as POSIX writes it: the `week`th `weekday` of
// `month` (week 5 = the last), at `seconds` past midnight local time (from
// -167 to 167 hours, so "the Saturday before" works too).
struct ZoneRule {
  uint8_t month = 0;    // 1..12
  uint8_t week = 0;     // 1..5
  uint8_t weekday = 0;  // 0 = Sunday
  int32_t seconds = 2 * 3600;
};

struct TimeZone {
  int32_t offset = 0;  // seconds east of UTC in winter
  bool summer = false;
  int32_t summer_offset = 0;  // seconds east of UTC in summer
  ZoneRule start;             // summer time starts (in winter time)
  ZoneRule end;               // and ends (in summer time)
};

constexpr const char* kDefaultZoneName = "Vilnius";
constexpr const char* kDefaultZoneRule = "EET-2EEST,M3.5.0/3,M10.5.0/4";

// Reads a POSIX TZ rule ("STD offset [DST [offset] [,start[/time],end[/time]]]",
// names as letters or <+03>, rules as Mm.w.d). Returns false, leaving out
// as it was, for anything else.
bool parse_time_zone(const char* rule, TimeZone& out);

// Seconds since 1970-01-01 00:00 for a date and time, and back.
int64_t epoch_seconds(const DateTime& t);
DateTime from_epoch_seconds(int64_t s);

// World time to the zone's, and back. In the hour that comes twice in
// autumn, to_world() takes the first one.
DateTime to_local(const DateTime& world, const TimeZone& zone);
DateTime to_world(const DateTime& local, const TimeZone& zone);
// Whether summer time is on at that world time.
bool is_summer(int64_t world_seconds, const TimeZone& zone);

// The zones Settings offers, west to east.
struct ZoneChoice {
  const char* name;  // a city, as Settings shows it
  const char* rule;
};
int zone_count();
const ZoneChoice& zone_choice(int i);
// The choice with this name, or -1.
int find_zone(const char* name);

// "UTC+2" or "UTC+5:30" for an offset in seconds east (at least 10 bytes).
void format_offset(int32_t offset, char* out, size_t size);

// The reader's clock as the app sees it: local time, from a board clock that
// keeps world time (Clock::world_time()); a clock already in local time (the
// simulators follow the computer's) passes straight through.
class LocalClock : public Clock {
 public:
  void attach(Clock* world) { world_ = world; }
  // Takes a POSIX rule; a bad one leaves the zone as it was.
  void set_zone(const char* rule) { parse_time_zone(rule, zone_); }
  const TimeZone& zone() const { return zone_; }

  bool now(DateTime& out) override;
  bool set(const DateTime& local) override;

  // An older Vellum kept local time in a world-time clock: turn it into
  // world time, once, by the zone set now. Returns true when it did.
  bool convert_old_time();

 private:
  Clock* world_ = nullptr;
  TimeZone zone_;
};

}  // namespace vellum
