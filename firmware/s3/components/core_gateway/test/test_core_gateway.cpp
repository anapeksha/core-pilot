#include "core_gateway_policy.hpp"

#include <unity.h>
#include <unity_test_runner.h>

using namespace core_pilot::gateway_policy;

TEST_CASE("gateway progress is monotonic and bounded per phase", "[core_gateway]") {
  Progress progress{};
  TEST_ASSERT_FALSE(progress.advance(0, 0));
  TEST_ASSERT_FALSE(progress.advance(0, 10000));
  unsigned notifications = 0;
  for (std::uint32_t offset = 1; offset <= 10000; ++offset) {
    if (progress.advance(offset, 10000)) {
      ++notifications;
      TEST_ASSERT_EQUAL_UINT8(notifications * 5, progress.last_percent);
    }
  }
  TEST_ASSERT_EQUAL_UINT(20, notifications);
  TEST_ASSERT_EQUAL_UINT8(100, progress.last_percent);
  TEST_ASSERT_FALSE(progress.advance(10000, 10000));
  TEST_ASSERT_FALSE(progress.advance(1, 10000));
  TEST_ASSERT_FALSE(progress.advance(UINT32_MAX, 10000));
}

TEST_CASE("gateway progress reports only crossed five percent boundaries", "[core_gateway]") {
  Progress progress{};
  TEST_ASSERT_FALSE(progress.advance(49, 1000));
  TEST_ASSERT_TRUE(progress.advance(50, 1000));
  TEST_ASSERT_EQUAL_UINT8(5, progress.last_percent);
  TEST_ASSERT_FALSE(progress.advance(99, 1000));
  TEST_ASSERT_FALSE(progress.advance(49, 1000));
  TEST_ASSERT_TRUE(progress.advance(179, 1000));
  TEST_ASSERT_EQUAL_UINT8(15, progress.last_percent);
  TEST_ASSERT_FALSE(progress.advance(150, 1000));
  TEST_ASSERT_TRUE(progress.advance(999, 1000));
  TEST_ASSERT_EQUAL_UINT8(95, progress.last_percent);
  TEST_ASSERT_TRUE(progress.advance(1000, 1000));
  TEST_ASSERT_EQUAL_UINT8(100, progress.last_percent);
}

TEST_CASE("gateway progress handles short chunks and full width lengths", "[core_gateway]") {
  Progress progress{};
  TEST_ASSERT_TRUE(progress.advance(1024, 1025));
  TEST_ASSERT_EQUAL_UINT8(95, progress.last_percent);
  TEST_ASSERT_TRUE(progress.advance(1025, 1025));
  progress = {};
  TEST_ASSERT_TRUE(progress.advance(UINT32_MAX / 2, UINT32_MAX));
  TEST_ASSERT_EQUAL_UINT8(45, progress.last_percent);
  TEST_ASSERT_TRUE(progress.advance(UINT32_MAX, UINT32_MAX));
  TEST_ASSERT_EQUAL_UINT8(100, progress.last_percent);
}

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
