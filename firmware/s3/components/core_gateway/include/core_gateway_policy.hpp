#pragma once

#include <cstdint>

namespace core_pilot::gateway_policy {

constexpr std::uint8_t kMaxProvisionAttempts = 2;

// At most 20 notifications at 5% boundaries, including completion. Use a fresh
// instance for each segment/phase; empty work never reports completion.
struct Progress final {
  std::uint8_t last_percent{};

  constexpr bool advance(std::uint32_t completed, std::uint32_t total) {
    if (total == 0)
      return false;
    const auto percent = static_cast<std::uint8_t>(
        completed >= total ? 100 : (static_cast<std::uint64_t>(completed) * 20 / total) * 5);
    if (percent <= last_percent)
      return false;
    last_percent = percent;
    return true;
  }
};

enum class State : std::uint8_t { stopped, probing, provisioning, ready, failed };
enum class SegmentAction : std::uint8_t { accept, provision, fail };

struct Status final {
  State state{State::stopped};
  bool manifest_current{};
  std::uint8_t flash_attempts{};
  int last_error{};
};

constexpr SegmentAction segment_action(bool md5_matches, bool sha256_matches,
                                       bool md5_check_failed) {
  if (md5_check_failed)
    return SegmentAction::fail;
  return md5_matches && sha256_matches ? SegmentAction::accept : SegmentAction::provision;
}

constexpr Status provisioning_started() {
  return {State::provisioning, false, 0, 0};
}

constexpr void provisioning_attempt(Status& status, std::uint8_t attempt) {
  status.flash_attempts = attempt;
}

constexpr void provisioning_succeeded(Status& status) {
  status.state = State::ready;
  status.manifest_current = true;
  status.last_error = 0;
}

constexpr void provisioning_failed(Status& status, int error) {
  status.state = State::failed;
  status.manifest_current = false;
  status.last_error = error;
}

} // namespace core_pilot::gateway_policy
