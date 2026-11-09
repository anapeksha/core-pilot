#include "core_matter_operation_policy.hpp"

#include <unity.h>

using core_pilot::detail::unpairCallbackMatches;
using core_pilot::detail::unpairCallbackSlotAvailable;

TEST_CASE("unpair callback slot remains reserved through timeout", "[core_matter]") {
  TEST_ASSERT_TRUE(unpairCallbackSlotAvailable(0));
  TEST_ASSERT_FALSE(unpairCallbackSlotAvailable(17));

  TEST_ASSERT_TRUE(unpairCallbackMatches(17, 0x1234, 3, 0x1234));
  TEST_ASSERT_FALSE(unpairCallbackMatches(0, 0x1234, 3, 0x1234));
  TEST_ASSERT_FALSE(unpairCallbackMatches(17, 0x1234, 0, 0x1234));
  TEST_ASSERT_FALSE(unpairCallbackMatches(17, 0x1234, 3, 0x5678));
}

TEST_CASE("commissioning failure policy has stable reset values", "[core_matter]") {
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<unsigned>(core_pilot::CommissioningFailureClass::none));
  TEST_ASSERT_EQUAL_HEX8(0xff, core_pilot::kUnknownCommissioningStage);
}
