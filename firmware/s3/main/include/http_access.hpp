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

struct HttpTelemetry final {
  std::uint32_t accepted_connections;
  std::uint32_t active_connections;
  std::uint32_t connection_errors;
  std::uint32_t dashboard_transfers;
  std::uint32_t dashboard_last_transfer_ms;
  std::uint32_t dashboard_max_transfer_ms;
};

esp_err_t initializeHttpAccess(core_security::RandomSource& random, CoreNetwork& network);
bool snapshotHttpAccess(HttpAccessSnapshot& snapshot);
esp_err_t regenerateHttpAccessFromPhysicalPresence();
esp_err_t registerHttpAccessRoute(httpd_handle_t server, const httpd_uri_t* route);
esp_err_t registerHttpAuthRoutes(httpd_handle_t server);
esp_err_t trackHttpConnectionOpen(httpd_handle_t server, int socket);
void trackHttpConnectionClose(httpd_handle_t server, int socket);
void recordDashboardTransfer(std::uint32_t elapsed_ms, bool complete);
HttpTelemetry httpTelemetry();
} // namespace core_pilot
