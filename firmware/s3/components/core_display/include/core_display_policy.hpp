#pragma once

#include <algorithm>
#include <cstdint>

namespace core_pilot::display_policy {

enum class UpgradePhase : std::uint8_t {
  prepared,
  staging,
  applying,
  activated,
  rebooting,
  failed
};

struct Upgrade final {
  UpgradePhase phase;
  std::uint32_t progress_bytes;
  std::uint32_t total_bytes;
  bool restart_after_update;
};

struct Calendar final {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
};

[[nodiscard]] constexpr bool is_leap_year(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

[[nodiscard]] constexpr int days_in_month(int year, int month) {
  constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12)
    return 0;
  return month == 2 && is_leap_year(year) ? 29 : days[month - 1];
}

[[nodiscard]] constexpr bool valid_calendar(const Calendar& value) {
  return value.year >= 2000 && value.year <= 2099 && value.month >= 1 && value.month <= 12 &&
         value.day >= 1 && value.day <= days_in_month(value.year, value.month) && value.hour >= 0 &&
         value.hour <= 23 && value.minute >= 0 && value.minute <= 59 && value.second >= 0 &&
         value.second <= 59;
}

// The upgrade page must not appear until verification and boot-partition activation have succeeded.
[[nodiscard]] constexpr bool upgrade_screen_allowed(UpgradePhase phase) {
  return phase == UpgradePhase::activated || phase == UpgradePhase::rebooting;
}

[[nodiscard]] constexpr std::uint32_t upgrade_progress_bytes(const Upgrade& upgrade) {
  return std::min(upgrade.progress_bytes, upgrade.total_bytes);
}

[[nodiscard]] constexpr unsigned upgrade_progress_percent(const Upgrade& upgrade) {
  if (upgrade.total_bytes == 0)
    return 0;
  return static_cast<unsigned>(static_cast<std::uint64_t>(upgrade_progress_bytes(upgrade)) * 100U /
                               upgrade.total_bytes);
}

} // namespace core_pilot::display_policy
