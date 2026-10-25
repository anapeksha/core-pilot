#include "http_access.hpp"

#include "cJSON.h"
#include "core_domain.hpp"
#include "core_network.hpp"
#include "esp_timer.h"
#include "http_server.hpp"
#include "mbedtls/platform_util.h"

#include <cstring>

namespace core_pilot {
namespace {
core_security::Auth auth;
core_security::RandomSource* random_source{};
CoreNetwork* network_service{};
struct Route {
  httpd_uri_t original{};
};
Route routes[64]{};
size_t route_count{};
uint64_t now() {
  return esp_timer_get_time() / 1000000;
}
esp_err_t reject(httpd_req_t* req, const char* status, const char* message) {
  httpd_resp_set_status(req, status);
  return httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
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
  if (req->method == HTTP_GET)
    return httpd_resp_sendstr(
        req,
        auth.bootstrap_open(now())
            ? "{\"authentication_required\":true,\"bootstrap_open\":true,\"session_seconds\":28800}"
            : "{\"authentication_required\":true,\"bootstrap_open\":false,\"session_seconds\":"
              "28800}");
  char body[96]{};
  if (req->content_len == 0 || req->content_len >= sizeof(body))
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid session request");
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
  char token[65]{};
  const bool valid = parsed == core_domain::ApiError::ok && cJSON_IsObject(document) &&
                     cJSON_GetArraySize(document) == 1 && cJSON_IsString(code) &&
                     std::strlen(code->valuestring) == 16;
  const auto result = auth.exchange(valid ? code->valuestring : "", *random_source, now(), token);
  if (code && cJSON_IsString(code))
    mbedtls_platform_zeroize(code->valuestring, std::strlen(code->valuestring));
  cJSON_Delete(document);
  mbedtls_platform_zeroize(body, sizeof(body));
  if (result != core_security::Status::Ok)
    return reject(req, "401 Unauthorized", "Access denied");
  char response[128]{};
  std::strcpy(response, "{\"token\":\"");
  std::strcat(response, token);
  std::strcat(response, "\",\"expires_in\":28800}");
  const auto sent = httpd_resp_sendstr(req, response);
  mbedtls_platform_zeroize(token, sizeof(token));
  mbedtls_platform_zeroize(response, sizeof(response));
  return sent;
}
esp_err_t dispatch(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Connection", "close");
  char site[32]{}, origin[160]{}, host[128]{};
  if (!header(req, "Sec-Fetch-Site", site, sizeof(site)) || !std::strcmp(site, "cross-site") ||
      !header(req, "Origin", origin, sizeof(origin)) || !header(req, "Host", host, sizeof(host)) ||
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
    const bool allowed = header(req, "Authorization", bearer, sizeof(bearer)) &&
                         !std::strncmp(bearer, "Bearer ", 7) && auth.authorize(bearer + 7, now());
    mbedtls_platform_zeroize(bearer, sizeof(bearer));
    if (!allowed)
      return reject(req, "401 Unauthorized", "Access required");
    if (network_service != nullptr &&
        network_service->status().mode == CoreNetwork::Mode::setup_ap) {
      const bool network_status =
          req->method == HTTP_GET && !std::strcmp(route.uri, "/api/v1/network");
      const bool wifi_provision =
          req->method == HTTP_POST && !std::strcmp(route.uri, "/api/v1/network/wifi");
      const bool wifi_scan = !std::strcmp(route.uri, "/api/v1/network/wifi/scan") ||
                             !std::strcmp(route.uri, "/api/v1/network/wifi/scan/page");
      if (!network_status && !wifi_provision && !wifi_scan)
        return reject(req, "403 Forbidden", "Unavailable during setup");
    }
  }
  req->user_ctx = route.user_ctx;
  return route.handler(req);
}
} // namespace
esp_err_t initializeHttpAccess(core_security::RandomSource& random, CoreNetwork& network,
                               core_security::Auth::BootstrapSink sink) {
  random_source = &random;
  network_service = &network;
  return auth.begin(random, now(), sink) == core_security::Status::Ok ? ESP_OK : ESP_FAIL;
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
  if (registerHttpAccessRoute(server, &status) != ESP_OK)
    return ESP_FAIL;
  return registerHttpAccessRoute(server, &session);
}
} // namespace core_pilot
