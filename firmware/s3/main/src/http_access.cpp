#include "http_access.hpp"

#include "cJSON.h"
#include "core_domain.hpp"
#include "core_network.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "http_server.hpp"
#include "mbedtls/platform_util.h"

#include <cstdio>
#include <cstring>

namespace core_pilot {
namespace {
core_security::Auth auth;
core_security::RandomSource* random_source{};
CoreNetwork* network_service{};
StaticSemaphore_t auth_mutex_storage{};
SemaphoreHandle_t auth_mutex{};

class AuthLock final {
public:
  AuthLock() : held_(auth_mutex && xSemaphoreTake(auth_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {}
  ~AuthLock() {
    if (held_)
      xSemaphoreGive(auth_mutex);
  }
  bool held() const {
    return held_;
  }

private:
  bool held_;
};

struct Route {
  httpd_uri_t original{};
};
Route routes[64]{};
size_t route_count{};

uint64_t now() {
  return esp_timer_get_time() / 1000000;
}

bool authority_matches(const char* host, const char* authority) {
  if (host == nullptr || authority == nullptr)
    return false;
  if (std::strcmp(host, authority) == 0)
    return true;
  const std::size_t length = std::strlen(authority);
  return std::strncmp(host, authority, length) == 0 && std::strcmp(host + length, ":80") == 0;
}

bool device_host_allowed(const char* host) {
  if (authority_matches(host, "corepilot.local"))
    return true;
  if (network_service == nullptr)
    return false;
  const auto status = network_service->status();
  if (status.mode == CoreNetwork::Mode::setup_ap && authority_matches(host, "192.168.4.1"))
    return true;
  char address[16]{};
  return status.has_ipv4 && esp_ip4addr_ntoa(&status.ipv4, address, sizeof(address)) != nullptr &&
         authority_matches(host, address);
}

esp_err_t reject(httpd_req_t* req, const char* status, const char* message) {
  httpd_resp_set_status(req, status);
  return httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
}

esp_err_t rejectAuth(httpd_req_t* req, const char* status, const char* code, const char* message) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  char body[192]{};
  std::snprintf(body, sizeof(body), "{\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}", code,
                message);
  return httpd_resp_sendstr(req, body);
}

const char* stateName(core_security::BootstrapState state) {
  switch (state) {
  case core_security::BootstrapState::open:
    return "open";
  case core_security::BootstrapState::consumed:
    return "consumed";
  case core_security::BootstrapState::expired:
    return "expired";
  case core_security::BootstrapState::attempts_exhausted:
    return "attempts_exhausted";
  case core_security::BootstrapState::unavailable:
    return "unavailable";
  }
  return "unavailable";
}

bool header(httpd_req_t* req, const char* name, char* out, size_t capacity) {
  if (!httpd_req_get_hdr_value_len(req, name)) {
    out[0] = 0;
    return true;
  }
  return httpd_req_get_hdr_value_str(req, name, out, capacity) == ESP_OK;
}

esp_err_t authHandler(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json");
  if (req->method == HTTP_GET) {
    AuthLock lock;
    if (!lock.held())
      return rejectAuth(req, "503 Service Unavailable", "unavailable",
                        "Access is temporarily unavailable.");
    char response[128]{};
    std::snprintf(response, sizeof(response),
                  "{\"authentication_required\":true,\"bootstrap_state\":\"%s\","
                  "\"session_seconds\":%llu}",
                  stateName(auth.bootstrap_state(now())),
                  static_cast<unsigned long long>(core_security::Auth::kSessionSeconds));
    return httpd_resp_sendstr(req, response);
  }

  char body[96]{};
  if (req->content_len == 0 || req->content_len >= sizeof(body))
    return rejectAuth(req, "400 Bad Request", "invalid_request", "Invalid session request.");
  size_t received = 0;
  while (received < req->content_len) {
    const int count = httpd_req_recv(req, body + received, req->content_len - received);
    if (count <= 0) {
      mbedtls_platform_zeroize(body, sizeof(body));
      return ESP_FAIL;
    }
    received += count;
  }
  cJSON* document = nullptr;
  const auto parsed = core_domain::parse_json(body, received, sizeof(body) - 1, document);
  const cJSON* code = document ? cJSON_GetObjectItemCaseSensitive(document, "code") : nullptr;
  const bool valid = parsed == core_domain::ApiError::ok && cJSON_IsObject(document) &&
                     cJSON_GetArraySize(document) == 1 && cJSON_IsString(code) &&
                     std::strlen(code->valuestring) == 16;
  if (!valid) {
    if (code && cJSON_IsString(code))
      mbedtls_platform_zeroize(code->valuestring, std::strlen(code->valuestring));
    cJSON_Delete(document);
    mbedtls_platform_zeroize(body, sizeof(body));
    return rejectAuth(req, "400 Bad Request", "invalid_request", "Invalid session request.");
  }

  char token[65]{};
  core_security::ExchangeResult result{core_security::ExchangeResult::unavailable};
  {
    AuthLock lock;
    if (lock.held())
      result = auth.exchange(code->valuestring, *random_source, now(), token);
  }
  mbedtls_platform_zeroize(code->valuestring, std::strlen(code->valuestring));
  cJSON_Delete(document);
  mbedtls_platform_zeroize(body, sizeof(body));

  if (result != core_security::ExchangeResult::ok) {
    mbedtls_platform_zeroize(token, sizeof(token));
    switch (result) {
    case core_security::ExchangeResult::incorrect:
      return rejectAuth(req, "401 Unauthorized", "incorrect", "The access code is incorrect.");
    case core_security::ExchangeResult::expired:
      return rejectAuth(req, "410 Gone", "expired", "The access code has expired.");
    case core_security::ExchangeResult::attempts_exhausted:
      return rejectAuth(req, "423 Locked", "attempts_exhausted", "Access attempts are exhausted.");
    case core_security::ExchangeResult::unavailable:
    case core_security::ExchangeResult::ok:
      return rejectAuth(req, "503 Service Unavailable", "unavailable",
                        "Access is temporarily unavailable.");
    }
  }

  char response[128]{};
  std::snprintf(response, sizeof(response), "{\"token\":\"%s\",\"expires_in\":%llu}", token,
                static_cast<unsigned long long>(core_security::Auth::kSessionSeconds));
  const auto sent = httpd_resp_sendstr(req, response);
  mbedtls_platform_zeroize(token, sizeof(token));
  mbedtls_platform_zeroize(response, sizeof(response));
  return sent;
}

esp_err_t logoutHandler(httpd_req_t* req) {
  char bearer[80]{};
  bool revoked = false;
  {
    AuthLock lock;
    revoked = lock.held() && header(req, "Authorization", bearer, sizeof(bearer)) &&
              !std::strncmp(bearer, "Bearer ", 7) && auth.revoke(bearer + 7);
  }
  mbedtls_platform_zeroize(bearer, sizeof(bearer));
  if (!revoked)
    return reject(req, "401 Unauthorized", "Access required");
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(
      req, "{\"accepted\":true,\"status\":\"revoked\",\"detail\":\"Session revoked.\"}");
}

esp_err_t dispatch(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(req, "Cross-Origin-Resource-Policy", "same-origin");
  httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
  char site[32]{}, origin[160]{}, host[128]{};
  if (!header(req, "Sec-Fetch-Site", site, sizeof(site)) || !std::strcmp(site, "cross-site") ||
      !header(req, "Origin", origin, sizeof(origin)) || !header(req, "Host", host, sizeof(host)) ||
      !device_host_allowed(host) ||
      core_domain::same_origin_mutation_allowed(origin[0] ? origin : nullptr, host) !=
          core_domain::ApiError::ok)
    return reject(req, "403 Forbidden", "Origin rejected");
  if (req->method == HTTP_POST) {
    char type[64]{};
    if (!header(req, "Content-Type", type, sizeof(type)) ||
        std::strcmp(type, "application/json") != 0)
      return reject(req, "415 Unsupported Media Type", "Expected application/json");
  }
  const auto& route = static_cast<Route*>(req->user_ctx)->original;
  const bool public_route =
      (req->method == HTTP_GET &&
       (!std::strcmp(route.uri, "/") || !std::strcmp(route.uri, "/api/v1/health") ||
        !std::strcmp(route.uri, "/api/v1/auth"))) ||
      (req->method == HTTP_POST && !std::strcmp(route.uri, "/api/v1/auth/session"));
  if (!public_route) {
    char bearer[80]{};
    bool allowed = false;
    {
      AuthLock lock;
      allowed = lock.held() && header(req, "Authorization", bearer, sizeof(bearer)) &&
                !std::strncmp(bearer, "Bearer ", 7) && auth.authorize(bearer + 7, now());
    }
    mbedtls_platform_zeroize(bearer, sizeof(bearer));
    if (!allowed)
      return reject(req, "401 Unauthorized", "Access required");
    if (network_service != nullptr &&
        network_service->status().mode == CoreNetwork::Mode::setup_ap) {
      const bool network_status =
          req->method == HTTP_GET && !std::strcmp(route.uri, "/api/v1/network");
      const bool auth_logout =
          req->method == HTTP_POST && !std::strcmp(route.uri, "/api/v1/auth/logout");
      const bool wifi_provision =
          req->method == HTTP_POST && !std::strcmp(route.uri, "/api/v1/network/wifi");
      const bool wifi_saved = !std::strcmp(route.uri, "/api/v1/network/wifi/saved") ||
                              !std::strcmp(route.uri, "/api/v1/network/wifi/forget");
      const bool wifi_scan = !std::strcmp(route.uri, "/api/v1/network/wifi/scan") ||
                             !std::strcmp(route.uri, "/api/v1/network/wifi/scan/page");
      if (!network_status && !auth_logout && !wifi_provision && !wifi_saved && !wifi_scan)
        return reject(req, "403 Forbidden", "Unavailable during setup");
    }
  }
  req->user_ctx = route.user_ctx;
  return route.handler(req);
}
} // namespace

esp_err_t initializeHttpAccess(core_security::RandomSource& random, CoreNetwork& network) {
  random_source = &random;
  network_service = &network;
  auth_mutex = xSemaphoreCreateMutexStatic(&auth_mutex_storage);
  if (!auth_mutex)
    return ESP_ERR_NO_MEM;
  AuthLock lock;
  return lock.held() && auth.begin(random, now()) == core_security::Status::Ok ? ESP_OK : ESP_FAIL;
}

bool snapshotHttpAccess(HttpAccessSnapshot& snapshot) {
  mbedtls_platform_zeroize(&snapshot, sizeof(snapshot));
  AuthLock lock;
  if (!lock.held()) {
    snapshot.state = core_security::BootstrapState::unavailable;
    return false;
  }
  snapshot.state = auth.bootstrap_state(now());
  snapshot.expires_at = auth.bootstrap_expires();
  auth.copy_bootstrap(snapshot.code);
  return true;
}

esp_err_t regenerateHttpAccessFromPhysicalPresence() {
  if (!random_source)
    return ESP_ERR_INVALID_STATE;
  AuthLock lock;
  if (!lock.held())
    return ESP_ERR_TIMEOUT;
  return auth.regenerate(*random_source, now()) == core_security::Status::Ok ? ESP_OK : ESP_FAIL;
}

esp_err_t registerHttpAccessRoute(httpd_handle_t server, const httpd_uri_t* route) {
  if (route_count >= 64 || !random_source)
    return ESP_ERR_INVALID_STATE;
  auto& entry = routes[route_count++];
  entry.original = *route;
  auto wrapped = *route;
  wrapped.handler = dispatch;
  wrapped.user_ctx = &entry;
  return httpd_register_uri_handler(server, &wrapped);
}

esp_err_t registerHttpAuthRoutes(httpd_handle_t server) {
  route_count = 0;
  const httpd_uri_t status{
      .uri = "/api/v1/auth", .method = HTTP_GET, .handler = authHandler, .user_ctx = nullptr};
  const httpd_uri_t session{.uri = "/api/v1/auth/session",
                            .method = HTTP_POST,
                            .handler = authHandler,
                            .user_ctx = nullptr};
  const httpd_uri_t logout{.uri = "/api/v1/auth/logout",
                           .method = HTTP_POST,
                           .handler = logoutHandler,
                           .user_ctx = nullptr};
  if (registerHttpAccessRoute(server, &status) != ESP_OK ||
      registerHttpAccessRoute(server, &session) != ESP_OK)
    return ESP_FAIL;
  return registerHttpAccessRoute(server, &logout);
}
} // namespace core_pilot
