#include "core_display_policy.hpp"

#include <cstdint>
#include <unity.h>
#include <unity_test_runner.h>

using namespace core_pilot::display_policy;

TEST_CASE("RTC calendar policy accepts only real bounded calendar values", "[core_display]") {
  TEST_ASSERT_TRUE(valid_calendar({2000, 2, 29, 0, 0, 0}));
  TEST_ASSERT_TRUE(valid_calendar({2099, 12, 31, 23, 59, 59}));
  TEST_ASSERT_FALSE(valid_calendar({2100, 1, 1, 0, 0, 0}));
  TEST_ASSERT_FALSE(valid_calendar({2023, 2, 29, 12, 0, 0}));
  TEST_ASSERT_FALSE(valid_calendar({2024, 4, 31, 12, 0, 0}));
  TEST_ASSERT_FALSE(valid_calendar({2024, 1, 1, 24, 0, 0}));
  TEST_ASSERT_FALSE(valid_calendar({2024, 1, 1, 0, 60, 0}));
  TEST_ASSERT_FALSE(valid_calendar({2024, 1, 1, 0, 0, 60}));
}

TEST_CASE("upgrade screen is gated until boot partition activation", "[core_display]") {
  TEST_ASSERT_FALSE(upgrade_screen_allowed(UpgradePhase::prepared));
  TEST_ASSERT_FALSE(upgrade_screen_allowed(UpgradePhase::staging));
  TEST_ASSERT_FALSE(upgrade_screen_allowed(UpgradePhase::applying));
  TEST_ASSERT_FALSE(upgrade_screen_allowed(UpgradePhase::failed));
  TEST_ASSERT_TRUE(upgrade_screen_allowed(UpgradePhase::activated));
  TEST_ASSERT_TRUE(upgrade_screen_allowed(UpgradePhase::rebooting));
}

TEST_CASE("upgrade progress clamps malformed counters without overflow", "[core_display]") {
  Upgrade upgrade{UpgradePhase::activated, 25, 100, false};
  TEST_ASSERT_EQUAL_UINT32(25, upgrade_progress_bytes(upgrade));
  TEST_ASSERT_EQUAL_UINT32(25, upgrade_progress_percent(upgrade));

  upgrade = {UpgradePhase::activated, UINT32_MAX, 1, false};
  TEST_ASSERT_EQUAL_UINT32(1, upgrade_progress_bytes(upgrade));
  TEST_ASSERT_EQUAL_UINT32(100, upgrade_progress_percent(upgrade));

  upgrade = {UpgradePhase::activated, UINT32_MAX, UINT32_MAX, false};
  TEST_ASSERT_EQUAL_UINT32(100, upgrade_progress_percent(upgrade));

  upgrade = {UpgradePhase::activated, 1, 0, false};
  TEST_ASSERT_EQUAL_UINT32(0, upgrade_progress_bytes(upgrade));
  TEST_ASSERT_EQUAL_UINT32(0, upgrade_progress_percent(upgrade));
}
