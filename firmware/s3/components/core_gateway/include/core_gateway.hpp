#pragma once

#include "core_gateway_policy.hpp"
#include "esp_err.h"

#include <cstdint>

namespace core_pilot {

class CoreGateway final {
public:
  using State = gateway_policy::State;

  struct Status final {
    State state;
    bool manifest_current;
    std::uint8_t flash_attempts;
    esp_err_t last_error;
  };

  using ProgressCallback = void (*)(void* context, const char* message);

  // Runs synchronously on the caller task (core 0 for boot). The callback must be
  // bounded, must not reenter the gateway, and must not retain the message pointer.
  esp_err_t ensureReady(ProgressCallback progress = nullptr, void* context = nullptr);
  Status status() const;

private:
  esp_err_t provision(ProgressCallback progress, void* context);

  gateway_policy::Status status_{};
};

} // namespace core_pilot
