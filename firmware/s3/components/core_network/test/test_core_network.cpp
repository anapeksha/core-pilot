#include "core_network_policy.hpp"

#include <cstring>
#include <unity.h>
#include <unity_test_runner.h>

using namespace core_pilot::network_policy;

TEST_CASE("station profile validates explicit lengths without reading past them",
          "[core_network]") {
  char ssid[33]{};
  char password[64]{};
  std::memset(ssid, 's', sizeof ssid);
  std::memset(password, 'p', sizeof password);
  Profile p{ssid, 32, password, 63};
  TEST_ASSERT_TRUE(valid_profile(p));
  p.ssid_length = 33;
  TEST_ASSERT_FALSE(valid_profile(p));
  p.ssid_length = 0;
  TEST_ASSERT_FALSE(valid_profile(p));
  p.ssid_length = 1;
  p.password_length = 7;
  TEST_ASSERT_FALSE(valid_profile(p));
  p.password_length = 8;
  TEST_ASSERT_TRUE(valid_profile(p));
  p.password = nullptr;
  TEST_ASSERT_FALSE(valid_profile(p));
  p.password_length = 0;
  TEST_ASSERT_TRUE(valid_profile(p)); // open station
  p.ssid = nullptr;
  TEST_ASSERT_FALSE(valid_profile(p));
  p.ssid = ssid;
  ssid[0] = 0;
  TEST_ASSERT_FALSE(valid_profile(p));
  ssid[0] = 's';
  p.password = password;
  p.password_length = 8;
  password[7] = 0;
  TEST_ASSERT_FALSE(valid_profile(p));
}

TEST_CASE("association identity rejects stale and mismatched events", "[core_network]") {
  const std::uint8_t expected_bssid[6]{1, 2, 3, 4, 5, 6};
  const std::uint8_t other_bssid[6]{6, 5, 4, 3, 2, 1};
  const std::uint8_t expected_ssid[]{'h', 'o', 'm', 'e'};
  const std::uint8_t other_ssid[]{'o', 'l', 'd'};
  TEST_ASSERT_TRUE(association_matches("home", nullptr, false, expected_ssid, sizeof(expected_ssid),
                                       expected_bssid));
  TEST_ASSERT_TRUE(association_matches("home", expected_bssid, true, expected_ssid,
                                       sizeof(expected_ssid), expected_bssid));
  TEST_ASSERT_FALSE(association_matches("home", expected_bssid, true, expected_ssid,
                                        sizeof(expected_ssid), other_bssid));
  TEST_ASSERT_FALSE(
      association_matches("home", nullptr, false, other_ssid, sizeof(other_ssid), other_bssid));
  TEST_ASSERT_FALSE(association_matches("home", nullptr, false, expected_ssid,
                                        sizeof(expected_ssid) - 1, expected_bssid));
}

TEST_CASE("scan events cannot cross a cancellation boundary", "[core_network]") {
  TEST_ASSERT_EQUAL(static_cast<int>(ScanEventAction::publish),
                    static_cast<int>(scan_event_action(true, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(ScanEventAction::ignore),
                    static_cast<int>(scan_event_action(false, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(ScanEventAction::finish_cancellation),
                    static_cast<int>(scan_event_action(false, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(ScanEventAction::finish_cancellation),
                    static_cast<int>(scan_event_action(true, true)));
  TEST_ASSERT_TRUE(scan_result_is_current(true, false, 7, 7));
  TEST_ASSERT_FALSE(scan_result_is_current(true, true, 7, 7));
  TEST_ASSERT_FALSE(scan_result_is_current(false, false, 7, 7));
  TEST_ASSERT_FALSE(scan_result_is_current(true, false, 8, 7));
}

TEST_CASE("scan timeout policy is bounded", "[core_network]") {
  TEST_ASSERT_GREATER_THAN_UINT32(kJoinTimeoutMs, kScanTimeoutMs);
  TEST_ASSERT_LESS_THAN_UINT32(kBootJoinBudgetMs, kScanTimeoutMs);
}

TEST_CASE("scan and setup name are bounded", "[core_network]") {
  TEST_ASSERT_EQUAL_UINT32(0, scan_limit(0));
  TEST_ASSERT_EQUAL_UINT32(32, scan_limit(32));
  TEST_ASSERT_EQUAL_UINT32(32, scan_limit(1000));
  const std::uint8_t mac[6]{0, 1, 2, 0xAB, 0xCD, 0xEF};
  char name[sizeof(kSetupPrefix) + 6]{};
  TEST_ASSERT_FALSE(setup_ssid(mac, name, sizeof name - 1));
  TEST_ASSERT_TRUE(setup_ssid(mac, name, sizeof name));
  TEST_ASSERT_EQUAL_STRING("CorePilot-Setup-ABCDEF", name);
  TEST_ASSERT_FALSE(setup_ssid(nullptr, name, sizeof name));
}
