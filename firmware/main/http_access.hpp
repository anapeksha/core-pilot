#pragma once
#include "core_auth.hpp"
#include "esp_http_server.h"
namespace core_pilot {
class CoreNetwork;
// Optional physical-only integration. Never forward this callback to HTTP or logs.
esp_err_t initializeHttpAccess(core_security::RandomSource& random, CoreNetwork& network,
                               core_security::Auth::BootstrapSink physical_sink = nullptr);
esp_err_t registerHttpAccessRoute(httpd_handle_t server, const httpd_uri_t* route);
esp_err_t registerHttpAuthRoutes(httpd_handle_t server);
} // namespace core_pilot
