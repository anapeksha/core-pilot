#pragma once
#include "core_auth.hpp"
#include "esp_http_server.h"

#include <cstdint>

namespace core_pilot {
class CoreNetwork;

struct HttpAccessSnapshot final {
  core_security::BootstrapState state{core_security::BootstrapState::unavailable};
  std::uint64_t expires_at{};
  char code[17]{};
};

esp_err_t initializeHttpAccess(core_security::RandomSource& random, CoreNetwork& network);
bool snapshotHttpAccess(HttpAccessSnapshot& snapshot);
esp_err_t regenerateHttpAccessFromPhysicalPresence();
esp_err_t registerHttpAccessRoute(httpd_handle_t server, const httpd_uri_t* route);
esp_err_t registerHttpAuthRoutes(httpd_handle_t server);
} // namespace core_pilot
