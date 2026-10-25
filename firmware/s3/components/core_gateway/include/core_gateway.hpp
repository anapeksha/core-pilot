#pragma once

#include "esp_err.h"

#include <cstdint>

namespace core_pilot {

class CoreGateway final {
public:
  enum class State : std::uint8_t { stopped, probing, provisioning, ready, failed };

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

  Status status_{State::stopped, false, 0, ESP_OK};
};

} // namespace core_pilot
