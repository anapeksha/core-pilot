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
