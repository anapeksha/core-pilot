#include "core_board_policy.hpp"
#include "core_display_policy.hpp"
#include "core_gateway_policy.hpp"
#include "core_matter_lifecycle.hpp"
#include "core_matter_operation_policy.hpp"
#include "core_matter_terminal_history.hpp"
#include "core_network_policy.hpp"
#include "core_ota_policy.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

#define CHECK(condition)                                                                           \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition << '\n';           \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

using core_pilot::detail::LifecycleTable;
using core_pilot::detail::SubscriptionPhase;

bool lifecycle_test() {
  LifecycleTable<2, 3> table;
  constexpr std::uint16_t endpoints[]{1, 2};
  CHECK(table.configure(0x11, endpoints, 2));
  CHECK(!table.configure(0, endpoints, 2));

  const std::uint32_t first_generation = table.beginSubscribe(0x11);
  CHECK(first_generation != 0);
  CHECK(table.established(0x11, first_generation));
  CHECK(table.report(0x11, first_generation, 2, true));
  CHECK(table.activeSubscriptionCount() == 1);
  CHECK(table.anyCaseReady());

  const std::uint32_t second_generation = table.beginSubscribe(0x11);
  CHECK(second_generation != first_generation);
  CHECK(!table.report(0x11, first_generation, 2, false));
  CHECK(table.established(0x11, second_generation));
  CHECK(table.terminated(0x11, second_generation, true));

  std::uint64_t recovery_node = 0;
  CHECK(table.takeRecovery(recovery_node));
  CHECK(recovery_node == 0x11);
  CHECK(!table.takeRecovery(recovery_node));
  CHECK(table.requeueRecovery(0x11));

  const std::uint32_t shutdown_generation = table.beginShutdown(0x11);
  CHECK(shutdown_generation != 0);
  const auto* node = table.snapshot(0x11);
  CHECK(node != nullptr);
  CHECK(node->phase == SubscriptionPhase::shutting_down);
  CHECK(!node->recovery_pending);
  CHECK(table.erase(0x11, shutdown_generation));
  CHECK(table.nodeCount() == 0);
  return true;
}

struct HistoryEntry final {
  std::uint64_t sequence;
  std::uint32_t value;
};

bool matter_operation_policy_test() {
  using core_pilot::detail::unpairCallbackMatches;
  using core_pilot::detail::unpairCallbackSlotAvailable;

  CHECK(unpairCallbackSlotAvailable(0));
  CHECK(!unpairCallbackSlotAvailable(17));
  CHECK(unpairCallbackMatches(17, 0x1234, 3, 0x1234));
  CHECK(!unpairCallbackMatches(0, 0x1234, 3, 0x1234));
  CHECK(!unpairCallbackMatches(17, 0x1234, 0, 0x1234));
  CHECK(!unpairCallbackMatches(17, 0x1234, 3, 0x5678));
  CHECK(core_pilot::CommissioningFailureClass::none != core_pilot::CommissioningFailureClass::pase);
  CHECK(core_pilot::kUnknownCommissioningStage == 0xff);
  return true;
}

bool terminal_history_test() {
  core_pilot::detail::TerminalHistory<HistoryEntry, 4> history;
  for (std::uint32_t operation = 1; operation <= 6; ++operation) {
    CHECK(history.append(operation, {operation, operation * 10}));
  }

  HistoryEntry entry{};
  CHECK(!history.find(1, entry));
  CHECK(history.find(3, entry));
  CHECK(entry.sequence == 3);
  CHECK(!history.append(3, {7, 30}));

  std::array<HistoryEntry, 2> entries{};
  CHECK(history.after(3, entries.data(), entries.size()) == 2);
  CHECK(entries[0].sequence == 4);
  CHECK(entries[1].sequence == 5);
  CHECK(history.after(0, nullptr, 1) == 0);
  return true;
}

bool safety_policy_test() {
  using core_pilot::board::valid_lcd_region;
  CHECK(valid_lcd_region({0, 0, 320, 240, 320U * 240U * 2U}));
  CHECK(!valid_lcd_region({319, 239, 2, 1, 4}));

  using namespace core_pilot::display_policy;
  CHECK(valid_calendar({2024, 2, 29, 23, 59, 59}));
  CHECK(!valid_calendar({2023, 2, 29, 0, 0, 0}));
  CHECK(!upgrade_screen_allowed(UpgradePhase::staging));
  CHECK(upgrade_screen_allowed(UpgradePhase::activated));
  CHECK(upgrade_progress_percent({UpgradePhase::activated, UINT32_MAX, 1, false}) == 100);

  using namespace core_pilot::gateway_policy;
  CHECK(segment_action(true, true, false) == SegmentAction::accept);
  CHECK(segment_action(true, false, false) == SegmentAction::provision);
  CHECK(segment_action(false, false, true) == SegmentAction::fail);

  using namespace core_pilot::ota_policy;
  Version version{};
  CHECK(parse_version("1.2.3-rc.1+build.5", version));
  CHECK(newer_version("1.2.3", "1.2.3-rc.1"));
  CHECK(!newer_version("1.2.3+new", "1.2.3+old"));
  const std::uint8_t bytes[]{1, 2, 3, 4};
  CHECK(validate_chunk(bytes, 4, 4, 0, 0, 4, true) == ChunkError::ok);
  CHECK(validate_chunk(bytes, 4, 4, 0, 0, 4, false) == ChunkError::final_mismatch);
  return true;
}

bool network_policy_test() {
  using namespace core_pilot::network_policy;

  std::array<char, kMaxSsidBytes> ssid{};
  std::array<char, kMaxPasswordBytes> password{};
  ssid.fill('s');
  password.fill('p');
  CHECK(valid_profile({ssid.data(), ssid.size(), password.data(), password.size()}));
  CHECK(!valid_profile({ssid.data(), 0, nullptr, 0}));
  CHECK(!valid_profile({ssid.data(), ssid.size() + 1, nullptr, 0}));
  CHECK(!valid_profile({ssid.data(), ssid.size(), password.data(), 7}));

  ssid[4] = '\0';
  CHECK(!valid_profile({ssid.data(), ssid.size(), nullptr, 0}));
  ssid[4] = 's';
  CHECK(scan_limit(0) == 0);
  CHECK(scan_limit(kMaxScanResults + 10) == kMaxScanResults);

  constexpr std::uint8_t mac[6]{0, 1, 2, 0xAB, 0xCD, 0xEF};
  std::array<char, sizeof(kSetupPrefix) + 6> setup_name{};
  CHECK(!setup_ssid(mac, setup_name.data(), setup_name.size() - 1));
  CHECK(setup_ssid(mac, setup_name.data(), setup_name.size()));
  CHECK(std::strcmp(setup_name.data(), "CorePilot-Setup-ABCDEF") == 0);
  return true;
}

} // namespace

int main() {
  if (!lifecycle_test() || !matter_operation_policy_test() || !terminal_history_test() ||
      !safety_policy_test() || !network_policy_test()) {
    return EXIT_FAILURE;
  }
  std::cout << "firmware logic tests passed\n";
  return EXIT_SUCCESS;
}
