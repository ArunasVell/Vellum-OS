#include "vellum/time_zone.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace vellum {

namespace {

constexpr int64_t kDay = 86400;

// Days since 1970-01-01 for a civil date (H. Hinnant's algorithm).
int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return int64_t{era} * 146097 + static_cast<int64_t>(doe) - 719468;
}

int days_in_month(int y, int m) {
  constexpr uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : kDays[m - 1];
}

// When `rule` falls in year y, in world time, for clocks at `offset`.
int64_t change_at(int y, const ZoneRule& rule, int32_t offset) {
  const int64_t first = days_from_civil(y, rule.month, 1);
  const int first_weekday = static_cast<int>(((first % 7) + 7 + 4) % 7);  // 1970-01-01 was a Thursday
  int day = 1 + (rule.weekday - first_weekday + 7) % 7 + 7 * (rule.week - 1);
  while (day > days_in_month(y, rule.month)) day -= 7;
  return (first + day - 1) * kDay + rule.seconds - offset;
}

// The parts of a rule, each moving `p` past what it read.
bool parse_name(const char*& p) {
  if (*p == '<') {
    const char* close = std::strchr(p, '>');
    if (close == nullptr || close - p < 2) return false;
    p = close + 1;
    return true;
  }
  const char* start = p;
  while (std::isalpha(static_cast<unsigned char>(*p))) ++p;
  return p - start >= 3;
}

// [+-]hh[:mm[:ss]] into seconds, up to `max_hours`.
bool parse_hms(const char*& p, int max_hours, int32_t& out) {
  int sign = 1;
  if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
  int32_t parts[3] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    if (i > 0 && *p != ':') break;
    if (i > 0) ++p;
    if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
    int v = 0;
    for (int n = 0; std::isdigit(static_cast<unsigned char>(*p)); ++n) {
      if (n == 3) return false;
      v = v * 10 + (*p++ - '0');
    }
    parts[i] = v;
  }
  if (parts[0] > max_hours || parts[1] > 59 || parts[2] > 59) return false;
  out = sign * (parts[0] * 3600 + parts[1] * 60 + parts[2]);
  return true;
}

bool parse_number(const char*& p, int lo, int hi, uint8_t& out) {
  if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
  int v = 0;
  while (std::isdigit(static_cast<unsigned char>(*p))) {
    v = v * 10 + (*p++ - '0');
    if (v > hi) return false;
  }
  if (v < lo) return false;
  out = static_cast<uint8_t>(v);
  return true;
}

// Mm.w.d[/time]
bool parse_rule(const char*& p, ZoneRule& out) {
  ZoneRule r;
  if (*p++ != 'M' || !parse_number(p, 1, 12, r.month) || *p++ != '.' || !parse_number(p, 1, 5, r.week) || *p++ != '.' ||
      !parse_number(p, 0, 6, r.weekday)) {
    return false;
  }
  if (*p == '/') {
    ++p;
    if (!parse_hms(p, 167, r.seconds)) return false;
  }
  out = r;
  return true;
}

// West to east. The rules are the ones the zones have used since 2023.
constexpr ZoneChoice kZones[] = {
    {"Honolulu", "HST10"},
    {"Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Phoenix", "MST7"},
    {"Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"Mexico City", "CST6"},
    {"Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"New York", "EST5EDT,M3.2.0,M11.1.0"},
    {"Halifax", "AST4ADT,M3.2.0,M11.1.0"},
    {"Santiago", "<-04>4<-03>,M9.1.6/24,M4.1.6/24"},
    {"Buenos Aires", "<-03>3"},
    {"S\xC3\xA3o Paulo", "<-03>3"},
    {"Azores", "<-01>1<+00>,M3.5.0/0,M10.5.0/1"},
    {"UTC", "UTC0"},
    {"Reykjavik", "GMT0"},
    {"London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},
    {"Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Lagos", "WAT-1"},
    {"Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Johannesburg", "SAST-2"},
    {"Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0"},
    {"Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Bucharest", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Tallinn", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Riga", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Vilnius", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Istanbul", "<+03>-3"},
    {"Nairobi", "EAT-3"},
    {"Tehran", "<+0330>-3:30"},
    {"Dubai", "<+04>-4"},
    {"Karachi", "PKT-5"},
    {"Kolkata", "IST-5:30"},
    {"Kathmandu", "<+0545>-5:45"},
    {"Dhaka", "<+06>-6"},
    {"Bangkok", "<+07>-7"},
    {"Singapore", "<+08>-8"},
    {"Shanghai", "CST-8"},
    {"Tokyo", "JST-9"},
    {"Seoul", "KST-9"},
    {"Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Brisbane", "AEST-10"},
    {"Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};

}  // namespace

bool parse_time_zone(const char* rule, TimeZone& out) {
  if (rule == nullptr) return false;
  const char* p = rule;
  TimeZone z;
  int32_t west = 0;
  if (!parse_name(p) || !parse_hms(p, 24, west)) return false;
  z.offset = -west;  // POSIX counts hours west of UTC
  if (*p != '\0') {
    if (!parse_name(p)) return false;
    z.summer = true;
    z.summer_offset = z.offset + 3600;
    if (*p != ',' && *p != '\0') {
      if (!parse_hms(p, 24, west)) return false;
      z.summer_offset = -west;
    }
    if (*p == '\0') {
      // No days given: the US ones, as POSIX has it.
      z.start = {3, 2, 0, 2 * 3600};
      z.end = {11, 1, 0, 2 * 3600};
    } else if (*p++ != ',' || !parse_rule(p, z.start) || *p++ != ',' || !parse_rule(p, z.end) || *p != '\0') {
      return false;
    }
  }
  out = z;
  return true;
}

int64_t epoch_seconds(const DateTime& t) {
  return days_from_civil(t.year, t.month, t.day) * kDay + t.hour * 3600 + t.minute * 60 + t.second;
}

DateTime from_epoch_seconds(int64_t s) {
  int64_t days = s / kDay;
  int64_t rest = s % kDay;
  if (rest < 0) {
    rest += kDay;
    --days;
  }
  // Back from days (H. Hinnant's civil_from_days).
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  const int64_t y = static_cast<int64_t>(yoe) + era * 400 + (m <= 2);
  return {static_cast<uint16_t>(y),
          static_cast<uint8_t>(m),
          static_cast<uint8_t>(d),
          static_cast<uint8_t>(rest / 3600),
          static_cast<uint8_t>(rest / 60 % 60),
          static_cast<uint8_t>(rest % 60)};
}

bool is_summer(int64_t world, const TimeZone& zone) {
  if (!zone.summer) return false;
  const int year = from_epoch_seconds(world + zone.offset).year;
  const int64_t start = change_at(year, zone.start, zone.offset);
  const int64_t end = change_at(year, zone.end, zone.summer_offset);
  // South of the equator summer runs over the new year.
  return start < end ? world >= start && world < end : world < end || world >= start;
}

DateTime to_local(const DateTime& world, const TimeZone& zone) {
  const int64_t s = epoch_seconds(world);
  return from_epoch_seconds(s + (is_summer(s, zone) ? zone.summer_offset : zone.offset));
}

DateTime to_world(const DateTime& local, const TimeZone& zone) {
  const int64_t s = epoch_seconds(local);
  if (zone.summer && is_summer(s - zone.summer_offset, zone)) return from_epoch_seconds(s - zone.summer_offset);
  return from_epoch_seconds(s - zone.offset);
}

int zone_count() { return static_cast<int>(sizeof(kZones) / sizeof(kZones[0])); }

const ZoneChoice& zone_choice(int i) { return kZones[i]; }

int find_zone(const char* name) {
  for (int i = 0; i < zone_count(); ++i) {
    if (std::strcmp(kZones[i].name, name) == 0) return i;
  }
  return -1;
}

void format_offset(int32_t offset, char* out, size_t size) {
  const char sign = offset < 0 ? '-' : '+';
  const int32_t a = offset < 0 ? -offset : offset;
  if (a % 3600 == 0) {
    std::snprintf(out, size, "UTC%c%d", sign, static_cast<int>(a / 3600));
  } else {
    std::snprintf(out, size, "UTC%c%d:%02d", sign, static_cast<int>(a / 3600), static_cast<int>(a / 60 % 60));
  }
}

bool LocalClock::now(DateTime& out) {
  if (world_ == nullptr || !world_->now(out)) return false;
  if (world_->world_time()) out = to_local(out, zone_);
  return true;
}

bool LocalClock::set(const DateTime& local) {
  if (world_ == nullptr) return false;
  return world_->set(world_->world_time() ? to_world(local, zone_) : local);
}

bool LocalClock::convert_old_time() {
  if (world_ == nullptr || !world_->world_time() || !world_->holds_local_time()) return false;
  DateTime t;
  return world_->now(t) && world_->set(to_world(t, zone_));
}

}  // namespace vellum
