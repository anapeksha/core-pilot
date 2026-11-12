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

TEST_CASE("invoke callback tokens reject stale generations and invalid slots", "[core_matter]") {
  constexpr std::uintptr_t first = core_pilot::detail::encodeInvokeToken(2, 7);
  constexpr std::uintptr_t reused = core_pilot::detail::encodeInvokeToken(2, 8);
  TEST_ASSERT_TRUE(core_pilot::detail::invokeTokenMatches(first, 8, 2, 7));
  TEST_ASSERT_FALSE(core_pilot::detail::invokeTokenMatches(first, 8, 2, 8));
  TEST_ASSERT_FALSE(core_pilot::detail::invokeTokenMatches(reused, 8, 2, 7));
  TEST_ASSERT_FALSE(
      core_pilot::detail::invokeTokenMatches(core_pilot::detail::encodeInvokeToken(8, 7), 8, 8, 7));
  TEST_ASSERT_EQUAL_UINT32(0, core_pilot::detail::encodeInvokeToken(0, 0));
}

TEST_CASE("reclaimed invoke slots reject late and duplicate callbacks", "[core_matter]") {
  constexpr std::size_t slot = 2;
  constexpr std::size_t capacity = 8;
  constexpr std::uintptr_t timed_out = core_pilot::detail::encodeInvokeToken(slot, 7);
  constexpr std::uintptr_t reused = core_pilot::detail::encodeInvokeToken(slot, 8);

  TEST_ASSERT_FALSE(
      core_pilot::detail::invokeCallbackCanComplete(timed_out, capacity, slot, 0, true, false));
  TEST_ASSERT_FALSE(
      core_pilot::detail::invokeCallbackCanComplete(timed_out, capacity, slot, 8, false, false));
  TEST_ASSERT_TRUE(
      core_pilot::detail::invokeCallbackCanComplete(reused, capacity, slot, 8, false, false));
  TEST_ASSERT_FALSE(
      core_pilot::detail::invokeCallbackCanComplete(reused, capacity, slot, 8, false, true));
}

TEST_CASE("commissioning failure policy has stable reset values", "[core_matter]") {
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<unsigned>(core_pilot::CommissioningFailureClass::none));
  TEST_ASSERT_EQUAL_HEX8(0xff, core_pilot::kUnknownCommissioningStage);
}
