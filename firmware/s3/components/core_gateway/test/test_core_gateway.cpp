#include "core_gateway_policy.hpp"

#include <unity.h>
#include <unity_test_runner.h>

using namespace core_pilot::gateway_policy;

TEST_CASE("gateway segment policy provisions only mismatched approved segments", "[core_gateway]") {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SegmentAction::accept),
                        static_cast<int>(segment_action(true, true, false)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SegmentAction::provision),
                        static_cast<int>(segment_action(true, false, false)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SegmentAction::provision),
                        static_cast<int>(segment_action(false, false, false)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SegmentAction::fail),
                        static_cast<int>(segment_action(false, false, true)));
}

TEST_CASE("gateway provisioning status preserves attempts and terminal result", "[core_gateway]") {
  auto status = provisioning_started();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(State::provisioning), static_cast<int>(status.state));
  TEST_ASSERT_FALSE(status.manifest_current);
  TEST_ASSERT_EQUAL_UINT8(0, status.flash_attempts);

  provisioning_attempt(status, 2);
  provisioning_succeeded(status);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(State::ready), static_cast<int>(status.state));
  TEST_ASSERT_TRUE(status.manifest_current);
  TEST_ASSERT_EQUAL_UINT8(2, status.flash_attempts);
  TEST_ASSERT_EQUAL_INT(0, status.last_error);

  status = provisioning_started();
  provisioning_attempt(status, kMaxProvisionAttempts);
  provisioning_failed(status, -7);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(State::failed), static_cast<int>(status.state));
  TEST_ASSERT_FALSE(status.manifest_current);
  TEST_ASSERT_EQUAL_UINT8(kMaxProvisionAttempts, status.flash_attempts);
  TEST_ASSERT_EQUAL_INT(-7, status.last_error);
}
