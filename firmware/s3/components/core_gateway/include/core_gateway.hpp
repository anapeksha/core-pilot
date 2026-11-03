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

  esp_err_t ensureReady();
  Status status() const;

private:
  esp_err_t provision();

  gateway_policy::Status status_{};
};

} // namespace core_pilot
