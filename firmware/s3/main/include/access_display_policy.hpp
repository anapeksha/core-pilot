#pragma once
#include "core_auth.hpp"
#include "core_display_policy.hpp"

namespace core_pilot {
[[nodiscard]] constexpr display_policy::AccessState
accessDisplayState(core_security::BootstrapState state) {
  using core_security::BootstrapState;
  using display_policy::AccessState;
  switch (state) {
  case BootstrapState::idle:
    return AccessState::idle;
  case BootstrapState::open:
    return AccessState::active;
  case BootstrapState::consumed:
    return AccessState::granted;
  case BootstrapState::expired:
    return AccessState::expired;
  case BootstrapState::attempts_exhausted:
    return AccessState::attempts_exhausted;
  case BootstrapState::unavailable:
    return AccessState::unavailable;
  }
  return AccessState::unavailable;
}
} // namespace core_pilot
