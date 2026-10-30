#include "core_matter_lifecycle.hpp"

#include <unity.h>

using core_pilot::detail::LifecycleTable;
using core_pilot::detail::SubscriptionPhase;

TEST_CASE("lifecycle tracks nodes endpoints reports and aggregate counts", "[core_matter]") {
  LifecycleTable<2, 3> table;
  const std::uint16_t first_endpoints[]{1, 2};
  const std::uint16_t second_endpoints[]{3};

  TEST_ASSERT_TRUE(table.configure(0x11, first_endpoints, 2));
  TEST_ASSERT_TRUE(table.configure(0x22, second_endpoints, 1));
  TEST_ASSERT_EQUAL_UINT32(2, table.nodeCount());
  TEST_ASSERT_EQUAL_UINT32(3, table.configuredEndpointCount());
  TEST_ASSERT_EQUAL_UINT32(0, table.activeSubscriptionCount());

  const std::uint32_t generation = table.beginSubscribe(0x11);
  TEST_ASSERT_NOT_EQUAL(0, generation);
  TEST_ASSERT_TRUE(table.established(0x11, generation));
  TEST_ASSERT_TRUE(table.report(0x11, generation, 2, true));
  TEST_ASSERT_EQUAL_UINT32(1, table.activeSubscriptionCount());
  TEST_ASSERT_TRUE(table.anyCaseReady());

  const auto* endpoint = table.endpointSnapshot(0x11, 2);
  TEST_ASSERT_NOT_NULL(endpoint);
  TEST_ASSERT_TRUE(endpoint->report_known);
  TEST_ASSERT_TRUE(endpoint->on);
}

TEST_CASE("lifecycle rejects invalid configuration and capacity overflow", "[core_matter]") {
  LifecycleTable<1, 2> table;
  const std::uint16_t duplicate[]{1, 1};
  const std::uint16_t valid[]{1, 2};
  const std::uint16_t other[]{3};

  TEST_ASSERT_FALSE(table.configure(1, duplicate, 2));
  TEST_ASSERT_TRUE(table.configure(1, valid, 2));
  TEST_ASSERT_FALSE(table.configure(2, other, 1));
  TEST_ASSERT_FALSE(table.configure(0, other, 1));
}

TEST_CASE("generation invalidates stale callbacks and shutdown blocks recovery", "[core_matter]") {
  LifecycleTable<2, 2> table;
  const std::uint16_t endpoints[]{1};
  TEST_ASSERT_TRUE(table.configure(0x11, endpoints, 1));
  const std::uint32_t first = table.beginSubscribe(0x11);
  TEST_ASSERT_TRUE(table.established(0x11, first));

  const std::uint32_t second = table.beginSubscribe(0x11);
  TEST_ASSERT_FALSE(table.report(0x11, first, 1, true));
  TEST_ASSERT_FALSE(table.terminated(0x11, first, true));
  TEST_ASSERT_TRUE(table.established(0x11, second));

  const std::uint32_t shutdown = table.beginShutdown(0x11);
  TEST_ASSERT_FALSE(table.terminated(0x11, second, true));
  const auto* node = table.snapshot(0x11);
  TEST_ASSERT_NOT_NULL(node);
  TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(SubscriptionPhase::shutting_down),
                          static_cast<unsigned>(node->phase));
  TEST_ASSERT_FALSE(node->recovery_pending);
  TEST_ASSERT_TRUE(table.erase(0x11, shutdown));
  TEST_ASSERT_EQUAL_UINT32(0, table.nodeCount());

  TEST_ASSERT_NOT_EQUAL(0, table.beginShutdown(0x22));
  const auto* unconfigured = table.snapshot(0x22);
  TEST_ASSERT_NOT_NULL(unconfigured);
  TEST_ASSERT_TRUE(table.abandonShutdown(0x22, unconfigured->generation));
  TEST_ASSERT_FALSE(table.erase(0x22, unconfigured->generation));
}

TEST_CASE("recovery is event driven and bounded to configured nodes", "[core_matter]") {
  LifecycleTable<2, 2> table;
  const std::uint16_t endpoints[]{1};
  TEST_ASSERT_TRUE(table.configure(0x11, endpoints, 1));
  const std::uint32_t generation = table.beginSubscribe(0x11);
  TEST_ASSERT_TRUE(table.established(0x11, generation));
  TEST_ASSERT_TRUE(table.terminated(0x11, generation, true));

  table.connectivityRestored();
  std::uint64_t node_id = 0;
  TEST_ASSERT_TRUE(table.takeRecovery(node_id));
  TEST_ASSERT_EQUAL_HEX64(0x11, node_id);
  TEST_ASSERT_FALSE(table.takeRecovery(node_id));
  TEST_ASSERT_TRUE(table.requeueRecovery(0x11));
  TEST_ASSERT_TRUE(table.takeRecovery(node_id));
  TEST_ASSERT_EQUAL_HEX64(0x11, node_id);
  TEST_ASSERT_FALSE(table.requeueRecovery(0x22));
}
