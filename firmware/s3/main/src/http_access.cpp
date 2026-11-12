#include "http_access.hpp"

#include "cJSON.h"
#include "core_domain.hpp"
#include "core_network.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "http_connection_policy.hpp"
#include "http_server.hpp"
#include "lwip/sockets.h"
#include "mbedtls/platform_util.h"
#include "request_rate_limiter.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace core_pilot {
namespace {
constexpr char kTag[] = "http_access";
#if CONFIG_CORE_PILOT_DEVELOPMENT_AUTH_BYPASS
constexpr bool kDevelopmentAuthBypass = true;
#else
constexpr bool kDevelopmentAuthBypass = false;
#endif

core_security::Auth auth;
core_security::RandomSource* random_source{};
CoreNetwork* network_service{};
StaticSemaphore_t auth_mutex_storage{};
SemaphoreHandle_t auth_mutex{};
StaticSemaphore_t rate_mutex_storage{};
SemaphoreHandle_t rate_mutex{};
core_domain::RequestRateLimiter request_limiter({600, 20}, {550, 20});
core_domain::RequestRateLimiter auth_limiter({4, 1}, {2, 1});
std::atomic<std::uint32_t> accepted_connections{};
std::atomic<std::uint32_t> active_connections{};
std::atomic<std::uint32_t> connection_errors{};
std::atomic<std::uint32_t> dashboard_transfers{};
std::atomic<std::uint32_t> dashboard_last_transfer_ms{};
std::atomic<std::uint32_t> dashboard_max_transfer_ms{};
core_domain::HttpConnectionPolicy connection_policy;
portMUX_TYPE connection_policy_lock = portMUX_INITIALIZER_UNLOCKED;

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

std::uint64_t socketSource(int socket) {
  sockaddr_storage peer{};
  socklen_t length = sizeof(peer);
  if (getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &length) != 0)
    return 0;
  const std::uint8_t* bytes = nullptr;
  std::size_t byte_count = 0;
  if (peer.ss_family == AF_INET) {
    const auto& address = reinterpret_cast<const sockaddr_in&>(peer);
    bytes = reinterpret_cast<const std::uint8_t*>(&address.sin_addr);
    byte_count = sizeof(address.sin_addr);
  } else if (peer.ss_family == AF_INET6) {
    const auto& address = reinterpret_cast<const sockaddr_in6&>(peer);
    bytes = reinterpret_cast<const std::uint8_t*>(&address.sin6_addr);
    byte_count = sizeof(address.sin6_addr);
  }
  if (bytes == nullptr)
    return 0;
  std::uint64_t hash = 1469598103934665603ULL;
  for (std::size_t index = 0; index < byte_count; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ULL;
  }
  return hash == 0 ? 1 : hash;
}

std::uint64_t requestSource(httpd_req_t* request) {
  return socketSource(httpd_req_to_sockfd(request));
}

esp_err_t rejectRateLimited(httpd_req_t* request, std::uint32_t retry_after) {
  char seconds[12]{};
  std::snprintf(seconds, sizeof(seconds), "%lu", static_cast<unsigned long>(retry_after));
  httpd_resp_set_status(request, "429 Too Many Requests");
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Retry-After", seconds);
  return httpd_resp_sendstr(
      request,
      "{\"error\":{\"code\":\"rate_limited\",\"message\":\"Too many requests. Retry later.\"}}");
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

bool unambiguousHeader(httpd_req_t* req, const char* name, char* out, size_t capacity) {
  return header(req, name, out, capacity) && std::strchr(out, ',') == nullptr;
}

bool validToken(const char* token) {
  if (token == nullptr || std::strlen(token) != 64)
    return false;
  for (std::size_t index = 0; index < 64; ++index) {
    if (!((token[index] >= '0' && token[index] <= '9') ||
          (token[index] >= 'a' && token[index] <= 'f')))
      return false;
  }
  return true;
}

bool cookieToken(httpd_req_t* request, char (&token)[65]) {
  char cookies[256]{};
  if (!unambiguousHeader(request, "Cookie", cookies, sizeof(cookies)))
    return false;
  constexpr char prefix[] = "core_pilot_session=";
  char* current = cookies;
  while (*current != '\0') {
    while (*current == ' ' || *current == ';')
      ++current;
    char* end = std::strchr(current, ';');
    if (end != nullptr)
      *end = '\0';
    if (std::strncmp(current, prefix, sizeof(prefix) - 1) == 0) {
      const char* value = current + sizeof(prefix) - 1;
      if (validToken(value))
        std::memcpy(token, value, sizeof(token));
      mbedtls_platform_zeroize(cookies, sizeof(cookies));
      return token[0] != '\0';
    }
    if (end == nullptr)
      break;
    current = end + 1;
  }
  mbedtls_platform_zeroize(cookies, sizeof(cookies));
  return false;
}

bool authorizeRequest(httpd_req_t* request, std::uint64_t timestamp) {
  char bearer[80]{};
  char cookie[65]{};
  bool allowed = false;
  if (unambiguousHeader(request, "Authorization", bearer, sizeof(bearer)) &&
      std::strncmp(bearer, "Bearer ", 7) == 0 && validToken(bearer + 7)) {
    allowed = auth.authorize(bearer + 7, timestamp);
  } else if (cookieToken(request, cookie)) {
    allowed = auth.authorize(cookie, timestamp);
  }
  mbedtls_platform_zeroize(bearer, sizeof(bearer));
  mbedtls_platform_zeroize(cookie, sizeof(cookie));
  return allowed;
}

esp_err_t authHandler(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json");
  if (req->method == HTTP_GET) {
    AuthLock lock;
    if (!lock.held())
      return rejectAuth(req, "503 Service Unavailable", "unavailable",
                        "Access is temporarily unavailable.");
    const std::uint64_t timestamp = now();
    const bool authenticated = kDevelopmentAuthBypass || authorizeRequest(req, timestamp);
    char response[160]{};
    std::snprintf(response, sizeof(response),
                  "{\"authentication_required\":%s,\"bootstrap_state\":\"%s\","
                  "\"session_seconds\":%llu,\"session_authenticated\":%s}",
                  kDevelopmentAuthBypass ? "false" : "true",
                  stateName(auth.bootstrap_state(timestamp)),
                  static_cast<unsigned long long>(core_security::Auth::kSessionSeconds),
                  authenticated ? "true" : "false");
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
  const cJSON* mode = document ? cJSON_GetObjectItemCaseSensitive(document, "mode") : nullptr;
  const bool cookie_mode = cJSON_IsString(mode) && std::strcmp(mode->valuestring, "cookie") == 0;
  const bool bearer_mode =
      mode == nullptr || (cJSON_IsString(mode) && std::strcmp(mode->valuestring, "bearer") == 0);
  const bool valid = parsed == core_domain::ApiError::ok && cJSON_IsObject(document) &&
                     cJSON_GetArraySize(document) == (mode == nullptr ? 1 : 2) &&
                     cJSON_IsString(code) && std::strlen(code->valuestring) == 16 &&
                     (cookie_mode || bearer_mode);
  if (!valid) {
    if (code && cJSON_IsString(code))
      mbedtls_platform_zeroize(code->valuestring, std::strlen(code->valuestring));
    core_domain::delete_json_sensitive(document);
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
  core_domain::delete_json_sensitive(document);
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
  char cookie[160]{};
  if (cookie_mode) {
    std::snprintf(cookie, sizeof(cookie),
                  "core_pilot_session=%s; HttpOnly; SameSite=Strict; Path=/; Max-Age=1800", token);
    if (httpd_resp_set_hdr(req, "Set-Cookie", cookie) != ESP_OK) {
      mbedtls_platform_zeroize(cookie, sizeof(cookie));
      mbedtls_platform_zeroize(token, sizeof(token));
      return ESP_FAIL;
    }
    std::snprintf(response, sizeof(response), "{\"authenticated\":true,\"expires_in\":%llu}",
                  static_cast<unsigned long long>(core_security::Auth::kSessionSeconds));
  } else {
    std::snprintf(response, sizeof(response), "{\"token\":\"%s\",\"expires_in\":%llu}", token,
                  static_cast<unsigned long long>(core_security::Auth::kSessionSeconds));
  }
  const auto sent = httpd_resp_sendstr(req, response);
  mbedtls_platform_zeroize(cookie, sizeof(cookie));
  mbedtls_platform_zeroize(token, sizeof(token));
  mbedtls_platform_zeroize(response, sizeof(response));
  return sent;
}

esp_err_t logoutHandler(httpd_req_t* req) {
  char body[16]{};
  if (req->content_len == 0 || req->content_len >= sizeof(body))
    return rejectAuth(req, "400 Bad Request", "invalid_request", "Invalid logout request.");
  size_t received = 0;
  while (received < req->content_len) {
    const int count = httpd_req_recv(req, body + received, req->content_len - received);
    if (count <= 0) {
      mbedtls_platform_zeroize(body, sizeof(body));
      return ESP_FAIL;
    }
    received += static_cast<size_t>(count);
  }
  cJSON* document = nullptr;
  const auto parsed = core_domain::parse_json(body, received, sizeof(body) - 1, document);
  const bool valid =
      parsed == core_domain::ApiError::ok && cJSON_IsObject(document) && document->child == nullptr;
  core_domain::delete_json_sensitive(document);
  mbedtls_platform_zeroize(body, sizeof(body));
  if (!valid)
    return rejectAuth(req, "400 Bad Request", "invalid_request", "Invalid logout request.");

  char bearer[80]{};
  char cookie[65]{};
  bool revoked = false;
  {
    AuthLock lock;
    if (lock.held() && unambiguousHeader(req, "Authorization", bearer, sizeof(bearer)) &&
        std::strncmp(bearer, "Bearer ", 7) == 0 && validToken(bearer + 7)) {
      revoked = auth.revoke(bearer + 7);
    } else if (lock.held() && cookieToken(req, cookie)) {
      revoked = auth.revoke(cookie);
    }
  }
  mbedtls_platform_zeroize(bearer, sizeof(bearer));
  mbedtls_platform_zeroize(cookie, sizeof(cookie));
  httpd_resp_set_hdr(req, "Set-Cookie",
                     "core_pilot_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
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
  if (!unambiguousHeader(req, "Sec-Fetch-Site", site, sizeof(site)) ||
      !std::strcmp(site, "cross-site") ||
      !unambiguousHeader(req, "Origin", origin, sizeof(origin)) ||
      !unambiguousHeader(req, "Host", host, sizeof(host)) || !device_host_allowed(host) ||
      core_domain::same_origin_mutation_allowed(origin[0] ? origin : nullptr, host) !=
          core_domain::ApiError::ok)
    return reject(req, "403 Forbidden", "Origin rejected");
  const auto& route = static_cast<Route*>(req->user_ctx)->original;
  const std::uint64_t source = requestSource(req);
  core_domain::RateLimitResult rate{false, 1};
  if (rate_mutex != nullptr && xSemaphoreTake(rate_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    rate = request_limiter.allow(source, now());
    if (rate.allowed && req->method == HTTP_POST &&
        std::strcmp(route.uri, "/api/v1/auth/session") == 0)
      rate = auth_limiter.allow(source, now());
    xSemaphoreGive(rate_mutex);
  }
  if (!rate.allowed)
    return rejectRateLimited(req, rate.retry_after_seconds);
  if (req->method == HTTP_POST || req->method == HTTP_PATCH) {
    char type[64]{};
    if (!unambiguousHeader(req, "Content-Type", type, sizeof(type)) ||
        std::strcmp(type, "application/json") != 0)
      return reject(req, "415 Unsupported Media Type", "Expected application/json");
  }
  const bool public_route =
      (req->method == HTTP_GET &&
       (!std::strcmp(route.uri, "/") || !std::strcmp(route.uri, "/api/v1/health") ||
        !std::strcmp(route.uri, "/api/v1/auth"))) ||
      (req->method == HTTP_POST && !std::strcmp(route.uri, "/api/v1/auth/session"));
  if (!public_route) {
    bool allowed = kDevelopmentAuthBypass;
    if (!allowed) {
      AuthLock lock;
      allowed = lock.held() && authorizeRequest(req, now());
    }
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
  if (kDevelopmentAuthBypass)
    ESP_LOGW(kTag, "DEVELOPMENT AUTH BYPASS ENABLED; NEVER USE THIS BUILD IN PRODUCTION");
  random_source = &random;
  network_service = &network;
  auth_mutex = xSemaphoreCreateMutexStatic(&auth_mutex_storage);
  rate_mutex = xSemaphoreCreateMutexStatic(&rate_mutex_storage);
  if (!auth_mutex || !rate_mutex)
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

esp_err_t trackHttpConnectionOpen(httpd_handle_t, int socket) {
  const std::uint64_t source = socketSource(socket);
  taskENTER_CRITICAL(&connection_policy_lock);
  const bool admitted = connection_policy.admit(socket, source);
  taskEXIT_CRITICAL(&connection_policy_lock);
  if (!admitted) {
    connection_errors.fetch_add(1);
    return ESP_FAIL;
  }
  const timeval timeout{.tv_sec = 5, .tv_usec = 0};
  if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
      setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
    taskENTER_CRITICAL(&connection_policy_lock);
    connection_policy.release(socket);
    taskEXIT_CRITICAL(&connection_policy_lock);
    connection_errors.fetch_add(1);
    return ESP_FAIL;
  }
  accepted_connections.fetch_add(1);
  active_connections.fetch_add(1);
  return ESP_OK;
}

void trackHttpConnectionClose(httpd_handle_t, int socket) {
  taskENTER_CRITICAL(&connection_policy_lock);
  const bool tracked = connection_policy.release(socket);
  taskEXIT_CRITICAL(&connection_policy_lock);
  std::uint32_t active = active_connections.load();
  while (tracked && active != 0 && !active_connections.compare_exchange_weak(active, active - 1)) {
  }
  close(socket);
}

void recordDashboardTransfer(std::uint32_t elapsed_ms, bool complete) {
  if (!complete) {
    connection_errors.fetch_add(1);
    return;
  }
  dashboard_transfers.fetch_add(1);
  dashboard_last_transfer_ms.store(elapsed_ms);
  std::uint32_t maximum = dashboard_max_transfer_ms.load();
  while (elapsed_ms > maximum &&
         !dashboard_max_transfer_ms.compare_exchange_weak(maximum, elapsed_ms)) {
  }
}

HttpTelemetry httpTelemetry() {
  return {.accepted_connections = accepted_connections.load(),
          .active_connections = active_connections.load(),
          .connection_errors = connection_errors.load(),
          .dashboard_transfers = dashboard_transfers.load(),
          .dashboard_last_transfer_ms = dashboard_last_transfer_ms.load(),
          .dashboard_max_transfer_ms = dashboard_max_transfer_ms.load()};
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
