#include "http_server.hpp"

#include "ai_chat_service.hpp"
#include "automation_service.hpp"
#include "cJSON.h"
#include "core_activity.hpp"
#include "core_ai.hpp"
#include "core_matter.hpp"
#include "core_network.hpp"
#include "core_storage.hpp"
#include "device_registry.hpp"
#include "domain_contract.hpp"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lwip/ip4_addr.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"
#include "mcp_service.hpp"
#include "model_repository.hpp"
#include "ota_service.hpp"
#include "smtp_service.hpp"
#include "thread_dataset.hpp"
#include "wifi_provision_service.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

extern "C" {
extern const std::uint8_t dashboard_gzip_start[] asm("_binary_index_html_gz_start");
extern const std::uint8_t dashboard_gzip_end[] asm("_binary_index_html_gz_end");
}

namespace core_pilot {
namespace {

constexpr char kTag[] = "core_http";
constexpr std::size_t kMaxHttpConnections = 4;
constexpr std::size_t kMaxRequestBody = 9216;

struct TimeConfig final {
  char ntp_server_id[16]{"cloudflare"};
  char ntp_server_host[32]{"time.cloudflare.com"};
  char timezone_id[64]{"UTC"};
  int timezone_offset_minutes{0};
  std::uint64_t generation{0};
  bool sntp_initialized{false};
};

TimeConfig time_config;
portMUX_TYPE time_config_lock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<std::time_t> last_time_sync{0};

class RequestBody final {
public:
  explicit RequestBody(std::size_t capacity)
      : capacity_(capacity), data_(static_cast<char*>(heap_caps_calloc(
                                 capacity, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))) {}
  ~RequestBody();
  RequestBody(const RequestBody&) = delete;
  RequestBody& operator=(const RequestBody&) = delete;
  char* data() const {
    return data_;
  }
  std::size_t capacity() const {
    return capacity_;
  }
  explicit operator bool() const {
    return data_ != nullptr;
  }

private:
  std::size_t capacity_;
  char* data_;
};

RequestBody::~RequestBody() {
  if (data_ != nullptr) {
    mbedtls_platform_zeroize(data_, capacity_);
    heap_caps_free(data_);
  }
}

void onTimeSync(struct timeval* value) {
  if (value != nullptr) {
    last_time_sync.store(value->tv_sec);
  }
}

bool applyTimezoneOffset(int offset_minutes) {
  const int posix_minutes = -offset_minutes;
  const int absolute_minutes = posix_minutes < 0 ? -posix_minutes : posix_minutes;
  char timezone[24]{};
  std::snprintf(timezone, sizeof(timezone), "UTC%c%d:%02d", posix_minutes < 0 ? '-' : '+',
                absolute_minutes / 60, absolute_minutes % 60);
  if (setenv("TZ", timezone, 1) != 0) {
    return false;
  }
  tzset();
  return true;
}

esp_err_t addCommonHeaders(httpd_req_t* request) {
  ESP_RETURN_ON_ERROR(httpd_resp_set_type(request, "application/json"), kTag, "type");
  ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "Cache-Control", "no-store"), kTag, "cache");
  ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "Connection", "close"), kTag, "connection");
  return ESP_OK;
}

esp_err_t dashboardHandler(httpd_req_t* request) {
  const auto size = static_cast<std::size_t>(dashboard_gzip_end - dashboard_gzip_start);
  ESP_RETURN_ON_ERROR(httpd_resp_set_type(request, "text/html; charset=utf-8"), kTag, "type");
  ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "Content-Encoding", "gzip"), kTag, "encoding");
  ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "Cache-Control", "no-cache"), kTag, "cache");
  ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "Connection", "close"), kTag, "connection");
  return httpd_resp_send(request, reinterpret_cast<const char*>(dashboard_gzip_start),
                         static_cast<ssize_t>(size));
}

esp_err_t healthHandler(httpd_req_t* request) {
  static constexpr char response[] =
      R"({"product":"Core Pilot","hostname":"corepilot.local","storage_ok":true,"ai_enabled":false,"auth":"none-trusted-lan"})";
  ESP_RETURN_ON_ERROR(addCommonHeaders(request), kTag, "headers");
  return httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t readRequestBody(httpd_req_t* request, char* body, std::size_t capacity,
                          std::size_t& received) {
  received = 0;
  if (request->content_len <= 0 || static_cast<std::size_t>(request->content_len) >= capacity) {
    return ESP_ERR_INVALID_SIZE;
  }
  while (received < static_cast<std::size_t>(request->content_len)) {
    const int result = httpd_req_recv(request, body + received, request->content_len - received);
    if (result <= 0) {
      return ESP_FAIL;
    }
    received += static_cast<std::size_t>(result);
  }
  body[received] = '\0';
  return ESP_OK;
}

esp_err_t sendJson(httpd_req_t* request, cJSON* response) {
  if (response == nullptr) {
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  }
  char* body = cJSON_PrintUnformatted(response);
  cJSON_Delete(response);
  if (body == nullptr) {
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  }
  const esp_err_t header_result = addCommonHeaders(request);
  const esp_err_t result = header_result == ESP_OK
                               ? httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN)
                               : header_result;
  cJSON_free(body);
  return result;
}

bool copyJsonString(const cJSON* item, char* output, std::size_t capacity);

bool exactObject(const cJSON* object, const char* const* names, std::size_t count) {
  if (!cJSON_IsObject(object))
    return false;
  std::size_t found = 0;
  for (std::size_t index = 0; index < count; ++index) {
    std::size_t matches = 0;
    for (const cJSON* child = object->child; child != nullptr; child = child->next)
      matches += child->string != nullptr && std::strcmp(child->string, names[index]) == 0 ? 1 : 0;
    if (matches != 1)
      return false;
    ++found;
  }
  for (const cJSON* child = object->child; child != nullptr; child = child->next) {
    bool known = false;
    for (std::size_t index = 0; index < count; ++index)
      known = known || (child->string != nullptr && std::strcmp(child->string, names[index]) == 0);
    if (!known)
      return false;
  }
  return found == count;
}

bool nonWhitespace(const char* value) {
  if (value == nullptr)
    return false;
  for (; *value != '\0'; ++value)
    if (*value != ' ' && *value != '\t' && *value != '\r' && *value != '\n')
      return true;
  return false;
}

const char* commissioningState(CoreMatter::CommissioningState state) {
  switch (state) {
  case CoreMatter::CommissioningState::idle:
    return "idle";
  case CoreMatter::CommissioningState::queued:
    return "queued";
  case CoreMatter::CommissioningState::pase:
    return "pase";
  case CoreMatter::CommissioningState::cancelling:
    return "cancelling";
  case CoreMatter::CommissioningState::complete:
    return "commissioning_complete";
  case CoreMatter::CommissioningState::cancelled:
    return "cancelled";
  case CoreMatter::CommissioningState::failed:
    return "failed";
  }
  return "failed";
}

const char* commandKind(CoreMatter::CommandKind kind) {
  switch (kind) {
  case CoreMatter::CommandKind::none:
    return "none";
  case CoreMatter::CommandKind::set_on_off:
    return "set_on_off";
  case CoreMatter::CommandKind::set_level:
    return "set_level";
  case CoreMatter::CommandKind::set_color_temperature:
    return "set_color_temperature";
  case CoreMatter::CommandKind::unpair_device:
    return "unpair_device";
  }
  return "none";
}

const char* commandState(CoreMatter::CommandState state) {
  switch (state) {
  case CoreMatter::CommandState::idle:
    return "idle";
  case CoreMatter::CommandState::queued:
    return "queued";
  case CoreMatter::CommandState::invoking:
    return "invoking";
  case CoreMatter::CommandState::complete:
    return "complete";
  case CoreMatter::CommandState::failed:
    return "failed";
  }
  return "failed";
}

const char* matterState(CoreMatter::State state) {
  switch (state) {
  case CoreMatter::State::stopped:
    return "stopped";
  case CoreMatter::State::starting:
    return "starting";
  case CoreMatter::State::ready:
    return "ready";
  case CoreMatter::State::failed:
    return "failed";
  }
  return "unknown";
}

void restartFromTimer(void*) {
  esp_restart();
}

esp_err_t scheduleRestart() {
  static esp_timer_handle_t timer = nullptr;
  if (timer == nullptr) {
    const esp_timer_create_args_t arguments{
        .callback = restartFromTimer,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "restart",
        .skip_unhandled_events = true,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&arguments, &timer), kTag, "create restart timer");
  }
  return esp_timer_start_once(timer, 500000);
}

esp_err_t rebootHandler(httpd_req_t* request) {
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", true);
  cJSON_AddStringToObject(response, "status", "rebooting");
  cJSON_AddStringToObject(response, "detail", "Core Pilot is restarting.");
  const esp_err_t result = sendJson(request, response);
  if (result == ESP_OK) {
    const esp_err_t restart_result = scheduleRestart();
    if (restart_result != ESP_OK)
      ESP_LOGE(kTag, "failed to schedule restart: %s", esp_err_to_name(restart_result));
  }
  return result;
}

esp_err_t activityPageHandler(httpd_req_t* request) {
  const auto* runtime = static_cast<const HttpRuntime*>(request->user_ctx);
  if (runtime == nullptr)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Missing runtime");

  RequestBody body(256);
  std::size_t received = 0;
  if (!body || readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  }
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  core_domain::ActivityPageRequest page{};
  const bool valid =
      document != nullptr &&
      core_domain::parse_activity_page_request(document, page) == core_domain::ApiError::ok &&
      (!page.has_limit || (page.limit >= 1 && page.limit <= 32));
  cJSON_Delete(document);
  if (!valid) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid activity page request");
  }

  ActivityService::Page result{};
  const std::uint8_t limit = page.has_limit ? page.limit : 20;
  if (!runtime->activity.page(page.has_before_sequence, page.before_sequence, limit, result)) {
    httpd_resp_set_status(request, "503 Service Unavailable");
    return httpd_resp_sendstr(request, "Activity unavailable");
  }

  cJSON* response = cJSON_CreateObject();
  cJSON* events = cJSON_CreateArray();
  if (response == nullptr || events == nullptr) {
    cJSON_Delete(response);
    cJSON_Delete(events);
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Allocation failed");
  }
  cJSON_AddItemToObject(response, "events", events);
  for (std::size_t index = 0; index < result.count; ++index) {
    const auto& event = result.events[index];
    cJSON* item = cJSON_CreateObject();
    if (item == nullptr) {
      cJSON_Delete(response);
      return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Allocation failed");
    }
    cJSON_AddNumberToObject(item, "sequence", event.sequence);
    if (event.unix_seconds > 0)
      cJSON_AddNumberToObject(item, "unix_seconds", static_cast<double>(event.unix_seconds));
    else
      cJSON_AddNullToObject(item, "unix_seconds");
    cJSON_AddNumberToObject(item, "uptime_seconds", event.uptime_seconds);
    cJSON_AddStringToObject(item, "kind", event.kind);
    cJSON_AddStringToObject(item, "detail", event.detail);
    cJSON_AddItemToArray(events, item);
  }
  if (result.has_next)
    cJSON_AddNumberToObject(response, "next_before_sequence", result.next_before_sequence);
  else
    cJSON_AddNullToObject(response, "next_before_sequence");
  cJSON_AddNumberToObject(response, "retained", result.retained);
  cJSON_AddNumberToObject(response, "dropped_events", result.dropped_events);
  cJSON_AddStringToObject(response, "detail",
                          result.count == 0 ? "No activity events match this page."
                                            : "Activity events loaded.");
  return sendJson(request, response);
}

esp_err_t unavailableMutationHandler(httpd_req_t* request) {
  httpd_resp_set_status(request, "503 Service Unavailable");
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", false);
  cJSON_AddStringToObject(response, "status", "SERVICE_NOT_READY");
  cJSON_AddStringToObject(response, "detail",
                          "This operation is unavailable until its encrypted "
                          "persistence and runtime are ready.");
  return sendJson(request, response);
}

const char* otaState(CoreOta::State state) {
  switch (state) {
  case CoreOta::State::idle:
    return "idle";
  case CoreOta::State::prepared:
    return "prepared";
  case CoreOta::State::staging:
    return "staging";
  case CoreOta::State::activated:
    return "activated";
  case CoreOta::State::failed:
    return "failed";
  }
  return "failed";
}

esp_err_t sendOtaMutation(httpd_req_t* request, bool accepted, const char* status,
                          const char* detail, const char* http_status = nullptr) {
  if (http_status != nullptr)
    httpd_resp_set_status(request, http_status);
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", accepted);
  cJSON_AddStringToObject(response, "status", status);
  cJSON_AddStringToObject(response, "detail", detail);
  return sendJson(request, response);
}

esp_err_t otaMutationHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  if (runtime == nullptr)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Missing runtime");
  RequestBody body(kMaxRequestBody + 1);
  std::size_t received = 0;
  if (!body || readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
    return sendOtaMutation(request, false, core_domain::error_code::OTA_CHUNK_REJECTED,
                           "OTA request body is empty or exceeds the bounded limit.",
                           "413 Payload Too Large");
  cJSON* document = nullptr;
  if (core_domain::parse_json(body.data(), received, kMaxRequestBody, document) !=
      core_domain::ApiError::ok)
    return sendOtaMutation(request, false, core_domain::error_code::OTA_CHUNK_REJECTED,
                           "OTA request body is not valid bounded JSON.", "400 Bad Request");

  const bool prepare = std::strcmp(request->uri, "/api/v1/ota/prepare") == 0;
  CoreOta::Result result{};
  bool final_chunk = false;
  bool restart_after_response = false;
  if (prepare) {
    core_domain::OtaPrepare parsed{};
    const auto parse_result = core_domain::parse_ota_prepare(document, parsed);
    cJSON_Delete(document);
    if (parse_result != core_domain::OtaError::ok)
      return sendOtaMutation(request, false, core_domain::error_code::OTA_METADATA_INVALID,
                             "OTA metadata failed strict validation.", "400 Bad Request");
    result = runtime->ota.prepare(parsed);
  } else {
    core_domain::OtaChunk parsed{};
    const auto parse_result = core_domain::parse_ota_chunk(document, parsed);
    cJSON_Delete(document);
    if (parse_result != core_domain::OtaError::ok)
      return sendOtaMutation(request, false, core_domain::error_code::OTA_CHUNK_REJECTED,
                             "OTA chunk failed strict validation.", "400 Bad Request");
    final_chunk = parsed.final_chunk;
    result = runtime->ota.write(parsed);
    const auto ota_status = runtime->ota.status();
    restart_after_response =
        result && ota_status.state == CoreOta::State::activated && ota_status.restart_after_update;
  }

  if (!result) {
    const bool busy = result.error == CoreOta::Error::invalid_state;
    return sendOtaMutation(request, false,
                           prepare ? core_domain::error_code::OTA_METADATA_INVALID
                                   : core_domain::error_code::OTA_CHUNK_REJECTED,
                           busy ? "OTA service is busy or the upload state is invalid."
                                : "OTA request was rejected; no unauthenticated image was "
                                  "activated.",
                           busy ? "409 Conflict" : "400 Bad Request");
  }

  const esp_err_t response_result = sendOtaMutation(
      request, true,
      prepare       ? "prepared"
      : final_chunk ? "activated"
                    : "chunk_staged",
      prepare                  ? "OTA metadata accepted for integrity-checked local upload."
      : restart_after_response ? "OTA image integrity verified and activated; restart scheduled."
                               : "OTA chunk staged; final activation requires SHA-256 integrity "
                                 "verification.");
  if (response_result == ESP_OK && restart_after_response && !runtime->ota.deferRestart())
    ESP_LOGE(kTag, "failed to queue deferred OTA restart");
  return response_result;
}

esp_err_t systemHandler(httpd_req_t* request, const HttpRuntime& runtime) {
  const auto network = runtime.network.status();
  const auto matter = runtime.matter.status();
  const auto ota = runtime.ota.status();
  ai::ProviderConfig provider{};
  const bool ai_configured = runtime.ai_storage.provider.load(provider) == ai::Status::Ok;
  ai::clear_provider_config(provider);
  cJSON* response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "product", "Core Pilot");
  cJSON_AddStringToObject(response, "hostname", "corepilot.local");
  cJSON_AddStringToObject(response, "firmware_version", "0.1.0");
  cJSON_AddNumberToObject(response, "reset_reason", esp_reset_reason());
  cJSON_AddStringToObject(response, "setup_ap",
                          network.mode == CoreNetwork::Mode::setup_ap ? "enabled" : "disabled");
  cJSON_AddStringToObject(response, "display", "ready");
  cJSON_AddStringToObject(response, "storage", "sd-card-encrypted");
  cJSON_AddStringToObject(response, "matter",
                          matter.state == CoreMatter::State::ready ? "ready" : "not-ready");
  cJSON_AddStringToObject(response, "thread",
                          matter.border_router_ready && matter.thread_dataset_configured &&
                                  matter.thread_attached
                              ? "ready"
                              : "not-ready");
  cJSON_AddStringToObject(response, "ai", ai_configured ? "configured" : "disabled");
  cJSON_AddStringToObject(response, "ota", "local-integrity-only");
  cJSON_AddStringToObject(response, "ota_phase", otaState(ota.state));
  cJSON_AddNumberToObject(response, "ota_progress_bytes", ota.written_bytes);
  cJSON_AddNumberToObject(response, "ota_total_bytes", ota.size_bytes);
  cJSON_AddBoolToObject(response, "ota_restart_after_update", ota.restart_after_update);
  cJSON_AddStringToObject(response, "recovery", "ready");
  cJSON_AddNumberToObject(response, "http_accepted_connections", 0);
  cJSON_AddNumberToObject(response, "http_active_connections", 0);
  cJSON_AddNumberToObject(response, "http_connection_errors", 0);
  cJSON_AddNumberToObject(response, "dashboard_transfers", 0);
  cJSON_AddNumberToObject(response, "dashboard_last_transfer_ms", 0);
  cJSON_AddNumberToObject(response, "dashboard_max_transfer_ms", 0);
  return sendJson(request, response);
}

const char* scanStateName(CoreNetwork::ScanState state) {
  switch (state) {
  case CoreNetwork::ScanState::idle:
    return "idle";
  case CoreNetwork::ScanState::scanning:
    return "scanning";
  case CoreNetwork::ScanState::complete:
    return "complete";
  case CoreNetwork::ScanState::failed:
    return "failed";
  }
  return "failed";
}

const char* authName(wifi_auth_mode_t auth) {
  switch (auth) {
  case WIFI_AUTH_OPEN:
    return "open";
  case WIFI_AUTH_WEP:
    return "wep";
  case WIFI_AUTH_WPA_PSK:
    return "wpa";
  case WIFI_AUTH_WPA2_PSK:
    return "wpa2";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "wpa-wpa2";
  case WIFI_AUTH_WPA3_PSK:
    return "wpa3";
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "wpa2-wpa3";
  default:
    return "secured";
  }
}

cJSON* wifiNetworkJson(const wifi_ap_record_t& record, const CoreNetwork::Status& status) {
  char bssid[18]{};
  std::snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x", record.bssid[0],
                record.bssid[1], record.bssid[2], record.bssid[3], record.bssid[4],
                record.bssid[5]);
  cJSON* item = cJSON_CreateObject();
  cJSON_AddStringToObject(item, "id", bssid);
  cJSON_AddStringToObject(item, "ssid", reinterpret_cast<const char*>(record.ssid));
  cJSON_AddStringToObject(item, "bssid", bssid);
  cJSON_AddBoolToObject(item, "connected",
                        status.has_bssid && std::memcmp(record.bssid, status.station_bssid,
                                                        sizeof(record.bssid)) == 0);
  cJSON_AddNumberToObject(item, "signal_dbm", record.rssi);
  cJSON_AddNumberToObject(item, "channel", record.primary);
  cJSON_AddBoolToObject(item, "secured", record.authmode != WIFI_AUTH_OPEN);
  cJSON_AddStringToObject(item, "auth", authName(record.authmode));
  return item;
}

cJSON* wifiScanJson(const CoreNetwork::ScanSnapshot& scan, const CoreNetwork::Status& status,
                    std::size_t begin, std::size_t end, bool paged) {
  cJSON* response = cJSON_CreateObject();
  cJSON* networks = cJSON_AddArrayToObject(response, "networks");
  for (std::size_t index = begin; index < end; ++index)
    cJSON_AddItemToArray(networks, wifiNetworkJson(scan.records[index], status));
  const bool scanning = scan.state == CoreNetwork::ScanState::scanning;
  cJSON_AddBoolToObject(response, "scanning", scanning);
  cJSON_AddStringToObject(response, "scan_state", scanStateName(scan.state));
  if (paged) {
    if (end < scan.count) {
      char cursor[32]{};
      std::snprintf(cursor, sizeof(cursor), "%lu:%u", static_cast<unsigned long>(scan.generation),
                    static_cast<unsigned>(end));
      cJSON_AddStringToObject(response, "next_last_seen", cursor);
    } else {
      cJSON_AddNullToObject(response, "next_last_seen");
    }
  }
  cJSON_AddStringToObject(response, "detail",
                          scanning ? "Wi-Fi scan is in progress."
                          : scan.state == CoreNetwork::ScanState::complete ? "Wi-Fi scan completed."
                          : scan.state == CoreNetwork::ScanState::failed   ? "Wi-Fi scan failed."
                                                                           : "Scan not requested.");
  return response;
}

esp_err_t wifiProvisionHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  RequestBody body(512);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  const cJSON* credentials = cJSON_IsObject(document)
                                 ? cJSON_GetObjectItemCaseSensitive(document, "credentials")
                                 : nullptr;
  const cJSON* ssid = cJSON_GetObjectItemCaseSensitive(credentials, "ssid");
  const cJSON* password = cJSON_GetObjectItemCaseSensitive(credentials, "password");
  const std::size_t ssid_length = cJSON_IsString(ssid) ? std::strlen(ssid->valuestring) : 0;
  const std::size_t password_length =
      cJSON_IsString(password) ? std::strlen(password->valuestring) : 0;
  const network_policy::Profile profile{
      cJSON_IsString(ssid) ? ssid->valuestring : nullptr, ssid_length,
      cJSON_IsString(password) ? password->valuestring : nullptr, password_length};
  const bool valid = cJSON_IsObject(credentials) && network_policy::valid_profile(profile);
  const esp_err_t result =
      valid ? runtime->wifi_provision.submit(profile.ssid, profile.ssid_length, profile.password,
                                             profile.password_length)
            : ESP_ERR_INVALID_ARG;
  if (body.data() != nullptr)
    mbedtls_platform_zeroize(body.data(), body.capacity());
  cJSON_Delete(document);

  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", result == ESP_OK);
  cJSON_AddStringToObject(response, "status",
                          result == ESP_OK                ? "queued"
                          : result == ESP_ERR_INVALID_ARG ? "WIFI_CONFIG_INVALID"
                                                          : "WIFI_CONFIG_BUSY");
  cJSON_AddStringToObject(response, "detail",
                          result == ESP_OK ? "Wi-Fi profile queued for connection testing."
                          : result == ESP_ERR_INVALID_ARG
                              ? "The Wi-Fi profile is invalid."
                              : "Another Wi-Fi migration is already active.");
  return sendJson(request, response);
}

esp_err_t wifiScanHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  if (runtime == nullptr)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Missing runtime");
  if (request->method == HTTP_POST) {
    RequestBody body(64);
    std::size_t received = 0;
    if (!body || readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
    cJSON* document = cJSON_ParseWithLength(body.data(), received);
    const bool valid = cJSON_IsObject(document) && document->child == nullptr;
    cJSON_Delete(document);
    if (!valid)
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid scan request");
    const esp_err_t result = runtime->network.requestScan();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
      httpd_resp_set_status(request, "503 Service Unavailable");
    }
  }
  const auto scan = runtime->network.scanSnapshot();
  return sendJson(request, wifiScanJson(scan, runtime->network.status(), 0, 0, false));
}

esp_err_t wifiScanPageHandler(httpd_req_t* request) {
  const auto* runtime = static_cast<const HttpRuntime*>(request->user_ctx);
  RequestBody body(128);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  const cJSON* cursor =
      cJSON_IsObject(document) ? cJSON_GetObjectItemCaseSensitive(document, "last_seen") : nullptr;
  std::size_t begin = 0;
  const auto scan = runtime->network.scanSnapshot();
  if (cursor != nullptr && !cJSON_IsNull(cursor)) {
    unsigned long generation = 0;
    unsigned index = 0;
    char trailing = '\0';
    if (!cJSON_IsString(cursor) || std::strlen(cursor->valuestring) > 31 ||
        std::sscanf(cursor->valuestring, "%lu:%u%c", &generation, &index, &trailing) != 2 ||
        generation != scan.generation || index > scan.count) {
      cJSON_Delete(document);
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid scan cursor");
    }
    begin = index;
  }
  cJSON_Delete(document);
  constexpr std::size_t kPageSize = 8;
  const std::size_t end = std::min(begin + kPageSize, scan.count);
  return sendJson(request, wifiScanJson(scan, runtime->network.status(), begin, end, true));
}

esp_err_t networkHandler(httpd_req_t* request, const HttpRuntime& runtime) {
  const auto status = runtime.network.status();
  char address[IP4ADDR_STRLEN_MAX]{};
  cJSON* response = cJSON_CreateObject();
  const bool station = status.mode == CoreNetwork::Mode::station;
  cJSON_AddStringToObject(response, "mode", station ? "station" : "setup-ap");
  cJSON_AddStringToObject(response, "setup_ssid", station ? "" : "CorePilot-Setup");
  if (station && status.station_ssid[0] != '\0') {
    cJSON_AddStringToObject(response, "station_ssid", status.station_ssid);
  } else {
    cJSON_AddNullToObject(response, "station_ssid");
  }
  if (station && status.has_bssid) {
    char bssid[18]{};
    std::snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x", status.station_bssid[0],
                  status.station_bssid[1], status.station_bssid[2], status.station_bssid[3],
                  status.station_bssid[4], status.station_bssid[5]);
    cJSON_AddStringToObject(response, "station_bssid", bssid);
  } else {
    cJSON_AddNullToObject(response, "station_bssid");
  }
  cJSON_AddStringToObject(response, "ipv4", status.has_ipv4 ? "ready" : "not-ready");
  if (status.has_ipv4 && esp_ip4addr_ntoa(&status.ipv4, address, sizeof(address)) != nullptr) {
    cJSON_AddStringToObject(response, "station_ipv4", address);
  } else {
    cJSON_AddNullToObject(response, "station_ipv4");
  }
  cJSON_AddStringToObject(response, "hostname", "corepilot.local");
  cJSON_AddStringToObject(response, "dhcp", status.has_ipv4 ? "ready" : "not-ready");
  cJSON_AddStringToObject(response, "http", "ready");
  cJSON_AddBoolToObject(response, "wan_enabled", false);
  cJSON_AddBoolToObject(response, "cloud_relay_enabled", false);
  return sendJson(request, response);
}

constexpr char kTimePath[] = "/core-pilot/config/time.enc";
constexpr std::uint16_t kTimeSchema = 1;

const char* ntpHost(const char* id) {
  if (std::strcmp(id, "cloudflare") == 0) {
    return "time.cloudflare.com";
  }
  if (std::strcmp(id, "google") == 0) {
    return "time.google.com";
  }
  if (std::strcmp(id, "pool") == 0 || std::strcmp(id, "ntp_pool") == 0) {
    return "pool.ntp.org";
  }
  return nullptr;
}

esp_err_t startTimeService(const char* host, int offset_minutes) {
  if (!applyTimezoneOffset(offset_minutes)) {
    return ESP_FAIL;
  }
  esp_netif_sntp_deinit();
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(host);
  config.sync_cb = onTimeSync;
  return esp_netif_sntp_init(&config);
}

core_security::Status persistTimeConfig(const HttpRuntime& runtime, const char* server_id,
                                        const char* timezone_id, int offset_minutes,
                                        std::uint64_t generation) {
  const std::size_t server_length = std::strlen(server_id);
  const std::size_t timezone_length = std::strlen(timezone_id);
  std::uint8_t plaintext[96]{};
  std::uint8_t encrypted[160]{};
  std::size_t position = 0;
  plaintext[position++] = static_cast<std::uint8_t>(server_length);
  std::memcpy(plaintext + position, server_id, server_length);
  position += server_length;
  plaintext[position++] = static_cast<std::uint8_t>(timezone_length);
  std::memcpy(plaintext + position, timezone_id, timezone_length);
  position += timezone_length;
  const auto encoded_offset = static_cast<std::uint16_t>(static_cast<std::int16_t>(offset_minutes));
  plaintext[position++] = static_cast<std::uint8_t>(encoded_offset & 0xff);
  plaintext[position++] = static_cast<std::uint8_t>(encoded_offset >> 8);
  const core_security::RecordSpec spec{kTimePath,
                                       sizeof(kTimePath) - 1,
                                       core_security::Namespace::Configuration,
                                       core_security::RecordType::TimeConfig,
                                       kTimeSchema,
                                       generation};
  const auto result =
      runtime.storage.put(spec, plaintext, position, runtime.random, encrypted, sizeof(encrypted));
  mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
  return result;
}

core_security::Status loadTimeConfig(const HttpRuntime& runtime, TimeConfig& loaded) {
  std::uint8_t plaintext[96]{};
  std::uint8_t encrypted[160]{};
  std::size_t size = 0;
  core_security::Metadata metadata{};
  const core_security::RecordSpec spec{kTimePath,
                                       sizeof(kTimePath) - 1,
                                       core_security::Namespace::Configuration,
                                       core_security::RecordType::TimeConfig,
                                       kTimeSchema,
                                       0};
  auto result = runtime.storage.get(spec, encrypted, sizeof(encrypted), plaintext,
                                    sizeof(plaintext), size, metadata);
  if (result != core_security::Status::Ok) {
    return result;
  }
  std::size_t position = 0;
  const std::size_t server_length = position < size ? plaintext[position++] : 0;
  if (server_length == 0 || server_length >= sizeof(loaded.ntp_server_id) ||
      position + server_length >= size) {
    result = core_security::Status::Invalid;
  } else {
    std::memcpy(loaded.ntp_server_id, plaintext + position, server_length);
    loaded.ntp_server_id[server_length] = '\0';
    position += server_length;
    const std::size_t timezone_length = plaintext[position++];
    if (timezone_length == 0 || timezone_length >= sizeof(loaded.timezone_id) ||
        position + timezone_length + 2 != size) {
      result = core_security::Status::Invalid;
    } else {
      std::memcpy(loaded.timezone_id, plaintext + position, timezone_length);
      loaded.timezone_id[timezone_length] = '\0';
      position += timezone_length;
      const auto encoded_offset = static_cast<std::uint16_t>(plaintext[position]) |
                                  (static_cast<std::uint16_t>(plaintext[position + 1]) << 8);
      loaded.timezone_offset_minutes = static_cast<std::int16_t>(encoded_offset);
      const char* host = ntpHost(loaded.ntp_server_id);
      if (host == nullptr || loaded.timezone_offset_minutes < -840 ||
          loaded.timezone_offset_minutes > 840) {
        result = core_security::Status::Invalid;
      } else {
        std::snprintf(loaded.ntp_server_host, sizeof(loaded.ntp_server_host), "%s", host);
        loaded.generation = metadata.generation;
      }
    }
  }
  mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
  return result;
}

esp_err_t timeStatusHandler(httpd_req_t* request) {
  taskENTER_CRITICAL(&time_config_lock);
  const TimeConfig config = time_config;
  taskEXIT_CRITICAL(&time_config_lock);

  char offset[16]{};
  const int absolute_minutes = config.timezone_offset_minutes < 0 ? -config.timezone_offset_minutes
                                                                  : config.timezone_offset_minutes;
  std::snprintf(offset, sizeof(offset), "UTC%c%02d:%02d",
                config.timezone_offset_minutes < 0 ? '-' : '+', absolute_minutes / 60,
                absolute_minutes % 60);

  cJSON* response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "ntp_server_id", config.ntp_server_id);
  cJSON_AddStringToObject(response, "ntp_server_host", config.ntp_server_host);
  cJSON_AddStringToObject(response, "timezone_id", config.timezone_id);
  cJSON_AddStringToObject(response, "timezone_label", config.timezone_id);
  cJSON_AddNumberToObject(response, "timezone_offset_minutes", config.timezone_offset_minutes);
  cJSON_AddStringToObject(response, "timezone_offset_label", offset);
  const std::time_t now = std::time(nullptr);
  const bool synchronized = now >= 1704067200;
  const std::time_t last_sync = last_time_sync.load();
  std::tm local{};
  char current_date[16]{};
  char current_time[16]{};
  if (synchronized && localtime_r(&now, &local) != nullptr) {
    std::strftime(current_date, sizeof(current_date), "%Y-%m-%d", &local);
    std::strftime(current_time, sizeof(current_time), "%H:%M:%S", &local);
  }
  cJSON_AddStringToObject(response, "dns", "ready");
  cJSON_AddStringToObject(response, "ntp",
                          synchronized              ? "synchronized"
                          : config.sntp_initialized ? "syncing"
                                                    : "not-synced");
  cJSON_AddStringToObject(response, "rtc", "ready");
  if (last_sync > 0) {
    cJSON_AddNumberToObject(response, "last_sync_unix_seconds", static_cast<double>(last_sync));
  } else {
    cJSON_AddNullToObject(response, "last_sync_unix_seconds");
  }
  if (synchronized) {
    cJSON_AddNumberToObject(response, "current_unix_seconds", static_cast<double>(now));
    cJSON_AddStringToObject(response, "current_date", current_date);
    cJSON_AddStringToObject(response, "current_time", current_time);
  } else {
    cJSON_AddNullToObject(response, "current_unix_seconds");
    cJSON_AddNullToObject(response, "current_date");
    cJSON_AddNullToObject(response, "current_time");
  }
  cJSON_AddStringToObject(
      response, "detail",
      synchronized ? "Network time is synchronized and the configured offset is active."
      : config.sntp_initialized ? "NTP synchronization is in progress."
                                : "RTC available; network time is not yet synchronized.");
  return sendJson(request, response);
}

const char* smtpSecurityName(smtp::SecurityMode mode) {
  switch (mode) {
  case smtp::SecurityMode::StartTls:
    return "start_tls";
  case smtp::SecurityMode::Tls:
    return "tls";
  case smtp::SecurityMode::PlainLanOnly:
    return "plain_lan_only";
  }
  return "start_tls";
}

bool parseSmtpSecurity(const cJSON* item, smtp::SecurityMode& mode) {
  if (!cJSON_IsString(item))
    return false;
  if (std::strcmp(item->valuestring, "start_tls") == 0)
    mode = smtp::SecurityMode::StartTls;
  else if (std::strcmp(item->valuestring, "tls") == 0)
    mode = smtp::SecurityMode::Tls;
  else if (std::strcmp(item->valuestring, "plain_lan_only") == 0)
    mode = smtp::SecurityMode::PlainLanOnly;
  else
    return false;
  return true;
}

bool parseSmtpRecipients(const cJSON* array,
                         char recipients[smtp::kMaxRecipients][smtp::kMaxEmailBytes + 1],
                         std::uint8_t& count) {
  if (!cJSON_IsArray(array))
    return false;
  const int size = cJSON_GetArraySize(array);
  if (size < 0 || size > static_cast<int>(smtp::kMaxRecipients))
    return false;
  count = static_cast<std::uint8_t>(size);
  for (int index = 0; index < size; ++index) {
    const cJSON* item = cJSON_GetArrayItem(array, index);
    if (!copyJsonString(item, recipients[index], smtp::kMaxEmailBytes + 1) ||
        !smtp::valid_email_address(recipients[index]))
      return false;
  }
  return true;
}

esp_err_t smtpStatusHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  if (runtime == nullptr)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Missing runtime");
  auto status = runtime->smtp.status();
  const bool configured = status.repository_status == smtp::Status::Ok;
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "enabled", configured && status.config.enabled);
  cJSON_AddBoolToObject(response, "configured", configured);
  cJSON_AddStringToObject(response, "host", configured ? status.config.host : "");
  cJSON_AddNumberToObject(response, "port", configured ? status.config.port : 587);
  cJSON_AddStringToObject(response, "security",
                          configured ? smtpSecurityName(status.config.security) : "start_tls");
  cJSON_AddBoolToObject(response, "username_configured",
                        configured && status.config.username[0] != '\0');
  cJSON_AddBoolToObject(response, "password_configured",
                        configured && status.config.password_length != 0);
  cJSON_AddNumberToObject(response, "recipients", configured ? status.config.recipient_count : 0);
  cJSON_AddBoolToObject(response, "transport_ready",
                        configured && status.config.enabled && status.service_ready);
  cJSON_AddStringToObject(response, "detail",
                          !configured ? "SMTP notifications are not configured."
                          : !status.config.enabled
                              ? "SMTP notifications are configured but disabled."
                          : status.service_ready ? "SMTP transport is ready to queue messages."
                                                 : "SMTP transport service is unavailable.");
  smtp::clear_config(status.config);
  return sendJson(request, response);
}

esp_err_t smtpConfigHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  RequestBody body(kMaxRequestBody + 1);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  static constexpr const char* root_fields[] = {"config"};
  static constexpr const char* required_fields[] = {"enabled",  "host", "port",      "security",
                                                    "username", "from", "recipients"};
  static constexpr const char* all_fields[] = {"enabled",  "host",     "port", "security",
                                               "username", "password", "from", "recipients"};
  const cJSON* input =
      cJSON_IsObject(document) ? cJSON_GetObjectItemCaseSensitive(document, "config") : nullptr;
  const bool has_password = cJSON_GetObjectItemCaseSensitive(input, "password") != nullptr;
  bool valid =
      exactObject(document, root_fields, 1) &&
      exactObject(input, has_password ? all_fields : required_fields, has_password ? 8 : 7);
  smtp::Config config{};
  const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(input, "enabled");
  const cJSON* port = cJSON_GetObjectItemCaseSensitive(input, "port");
  const cJSON* password = cJSON_GetObjectItemCaseSensitive(input, "password");
  valid = valid && (cJSON_IsTrue(enabled) || cJSON_IsFalse(enabled)) && cJSON_IsNumber(port) &&
          port->valuedouble == port->valueint && port->valueint >= 0 && port->valueint <= 65535 &&
          copyJsonString(cJSON_GetObjectItemCaseSensitive(input, "host"), config.host,
                         sizeof(config.host)) &&
          copyJsonString(cJSON_GetObjectItemCaseSensitive(input, "username"), config.username,
                         sizeof(config.username)) &&
          copyJsonString(cJSON_GetObjectItemCaseSensitive(input, "from"), config.from,
                         sizeof(config.from)) &&
          parseSmtpSecurity(cJSON_GetObjectItemCaseSensitive(input, "security"), config.security) &&
          parseSmtpRecipients(cJSON_GetObjectItemCaseSensitive(input, "recipients"),
                              config.recipients, config.recipient_count);
  config.enabled = cJSON_IsTrue(enabled);
  config.port = valid ? static_cast<std::uint16_t>(port->valueint) : 0;
  config.password_redacted = !cJSON_IsString(password);
  if (cJSON_IsString(password)) {
    const std::size_t length = strnlen(password->valuestring, smtp::kMaxPasswordBytes + 1);
    valid = valid && length <= smtp::kMaxPasswordBytes;
    if (length <= smtp::kMaxPasswordBytes) {
      config.password_length = static_cast<std::uint16_t>(length);
      std::memcpy(config.password, password->valuestring, length);
    }
  } else {
    valid = valid && (password == nullptr || cJSON_IsNull(password));
  }
  const smtp::Status result = valid ? runtime->smtp.save(config) : smtp::Status::InvalidArgument;
  smtp::clear_config(config);
  if (cJSON_IsString(password))
    mbedtls_platform_zeroize(password->valuestring, std::strlen(password->valuestring));
  cJSON_Delete(document);
  const bool accepted = result == smtp::Status::Ok;
  if (!accepted)
    httpd_resp_set_status(request, result == smtp::Status::StorageError ? "503 Service Unavailable"
                                                                        : "400 Bad Request");
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", accepted);
  cJSON_AddStringToObject(response, "status",
                          accepted                               ? "saved"
                          : result == smtp::Status::StorageError ? "STORAGE_WRITE_FAILED"
                                                                 : "SMTP_CONFIG_REJECTED");
  cJSON_AddStringToObject(response, "detail",
                          accepted ? "SMTP configuration encrypted and saved."
                                   : "The SMTP configuration was rejected.");
  return sendJson(request, response);
}

esp_err_t smtpTestHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  RequestBody body(kMaxRequestBody + 1);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  static constexpr const char* root_fields[] = {"message"};
  static constexpr const char* message_fields[] = {"subject", "body", "recipients"};
  const cJSON* input =
      cJSON_IsObject(document) ? cJSON_GetObjectItemCaseSensitive(document, "message") : nullptr;
  smtp::Message message{};
  char message_body[smtp::kMaxBodyBytes + 1]{};
  bool valid = exactObject(document, root_fields, 1) && exactObject(input, message_fields, 3) &&
               copyJsonString(cJSON_GetObjectItemCaseSensitive(input, "subject"), message.subject,
                              sizeof(message.subject)) &&
               copyJsonString(cJSON_GetObjectItemCaseSensitive(input, "body"), message_body,
                              sizeof(message_body)) &&
               nonWhitespace(message.subject) && nonWhitespace(message_body) &&
               parseSmtpRecipients(cJSON_GetObjectItemCaseSensitive(input, "recipients"),
                                   message.recipients, message.recipient_count);
  message.body = message_body;
  message.body_length = valid ? std::strlen(message_body) : 0;
  const auto result = valid ? runtime->smtp.submit(message) : SmtpService::SubmitResult::invalid;
  mbedtls_platform_zeroize(&message, sizeof(message));
  mbedtls_platform_zeroize(message_body, sizeof(message_body));
  cJSON_Delete(document);
  const bool accepted = result == SmtpService::SubmitResult::queued;
  if (!accepted)
    httpd_resp_set_status(request, result == SmtpService::SubmitResult::invalid
                                       ? "400 Bad Request"
                                       : "503 Service Unavailable");
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", accepted);
  cJSON_AddStringToObject(response, "status",
                          accepted                                       ? "queued"
                          : result == SmtpService::SubmitResult::invalid ? "SMTP_MESSAGE_INVALID"
                          : result == SmtpService::SubmitResult::not_configured
                              ? "SMTP_NOT_CONFIGURED"
                              : "SMTP_TRANSPORT_UNAVAILABLE");
  cJSON_AddStringToObject(response, "detail",
                          accepted
                              ? "SMTP test message queued. Terminal status is recorded in activity."
                              : "The SMTP test message could not be queued.");
  return sendJson(request, response);
}

esp_err_t timeConfigHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  RequestBody body(kMaxRequestBody + 1);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  }

  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  if (document == nullptr) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid JSON");
  }
  const cJSON* server_id = cJSON_GetObjectItemCaseSensitive(document, "ntp_server_id");
  const cJSON* timezone_id = cJSON_GetObjectItemCaseSensitive(document, "timezone_id");
  const cJSON* offset = cJSON_GetObjectItemCaseSensitive(document, "timezone_offset_minutes");
  const char* host = cJSON_IsString(server_id) ? ntpHost(server_id->valuestring) : nullptr;
  const bool valid_timezone =
      cJSON_IsString(timezone_id) && timezone_id->valuestring[0] != '\0' &&
      std::strlen(timezone_id->valuestring) < sizeof(time_config.timezone_id);
  const bool valid_offset = cJSON_IsNumber(offset) && offset->valuedouble == offset->valueint &&
                            offset->valueint >= -840 && offset->valueint <= 840;
  if (host == nullptr || !valid_timezone || !valid_offset) {
    cJSON_Delete(document);
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid time configuration");
  }

  taskENTER_CRITICAL(&time_config_lock);
  const std::uint64_t generation = time_config.generation + 1;
  taskEXIT_CRITICAL(&time_config_lock);
  const auto storage_result = persistTimeConfig(
      *runtime, server_id->valuestring, timezone_id->valuestring, offset->valueint, generation);
  esp_err_t result = storage_result == core_security::Status::Ok
                         ? startTimeService(host, offset->valueint)
                         : ESP_FAIL;
  if (result == ESP_OK) {
    taskENTER_CRITICAL(&time_config_lock);
    std::snprintf(time_config.ntp_server_id, sizeof(time_config.ntp_server_id), "%s",
                  server_id->valuestring);
    std::snprintf(time_config.ntp_server_host, sizeof(time_config.ntp_server_host), "%s", host);
    std::snprintf(time_config.timezone_id, sizeof(time_config.timezone_id), "%s",
                  timezone_id->valuestring);
    time_config.timezone_offset_minutes = offset->valueint;
    time_config.generation = generation;
    time_config.sntp_initialized = true;
    taskEXIT_CRITICAL(&time_config_lock);
  }
  cJSON_Delete(document);

  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", result == ESP_OK);
  cJSON_AddStringToObject(response, "status",
                          result == ESP_OK                              ? "saved"
                          : storage_result != core_security::Status::Ok ? "STORAGE_WRITE_FAILED"
                                                                        : "NTP_START_FAILED");
  cJSON_AddStringToObject(response, "detail",
                          result == ESP_OK ? "Time configuration encrypted and applied."
                          : storage_result != core_security::Status::Ok
                              ? "Time configuration could not be written to encrypted storage."
                              : "NTP synchronization could not be started.");
  return sendJson(request, response);
}

void addCapabilities(cJSON* array, std::uint32_t capabilities) {
  struct CapabilityName final {
    std::uint32_t mask;
    const char* name;
  };
  static constexpr CapabilityName names[] = {
      {CoreMatter::on_off, "on_off"},
      {CoreMatter::level, "level"},
      {CoreMatter::color, "color"},
      {CoreMatter::thermostat, "thermostat"},
      {CoreMatter::fan, "fan"},
      {CoreMatter::lock, "lock"},
      {CoreMatter::window_covering, "window_covering"},
      {CoreMatter::occupancy, "occupancy"},
      {CoreMatter::temperature, "temperature"},
      {CoreMatter::humidity, "humidity"},
      {CoreMatter::electrical_power, "electrical_power"},
      {CoreMatter::electrical_energy, "electrical_energy"},
  };
  for (const auto& capability : names) {
    if ((capabilities & capability.mask) != 0) {
      cJSON_AddItemToArray(array, cJSON_CreateString(capability.name));
    }
  }
}

esp_err_t devicesHandler(httpd_req_t* request, const HttpRuntime& runtime) {
  const auto devices = runtime.devices.snapshot();
  cJSON* response = cJSON_CreateObject();
  cJSON* items = cJSON_AddArrayToObject(response, "devices");
  for (std::size_t index = 0; index < devices.count; ++index) {
    const auto& device = devices.entries[index];
    char id[24]{};
    char name[40]{};
    std::snprintf(id, sizeof(id), "%016llx-%u", static_cast<unsigned long long>(device.node_id),
                  static_cast<unsigned>(device.endpoint_id));
    std::snprintf(name, sizeof(name), "Matter device %u endpoint %u",
                  static_cast<unsigned>(index + 1), static_cast<unsigned>(device.endpoint_id));
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "id", id);
    cJSON_AddStringToObject(item, "name", name);
    cJSON_AddNullToObject(item, "room");
    cJSON_AddStringToObject(item, "protocol", "matter");
    cJSON* capabilities = cJSON_AddArrayToObject(item, "capabilities");
    addCapabilities(capabilities, device.capabilities);
    cJSON_AddArrayToObject(item, "state");
    cJSON_AddStringToObject(item, "availability", device.reachable ? "online" : "unknown");
    cJSON_AddNumberToObject(item, "last_seen", 0);
    cJSON_AddNumberToObject(item, "endpoint_id", device.endpoint_id);
    char node_id[17]{};
    std::snprintf(node_id, sizeof(node_id), "%016llx",
                  static_cast<unsigned long long>(device.node_id));
    cJSON_AddStringToObject(item, "node_id", node_id);
    cJSON_AddItemToArray(items, item);
  }
  cJSON_AddBoolToObject(response, "registry_writable", runtime.devices.writable());
  cJSON_AddStringToObject(response, "detail",
                          !runtime.devices.writable()
                              ? "The commissioned-device record is unsupported and must be "
                                "explicitly archived and reset."
                          : devices.count == 0 ? "No commissioned devices."
                                               : "Commissioned Matter devices loaded.");
  return sendJson(request, response);
}

const char* modelKey(core_domain::ModelKind kind) {
  static constexpr const char* keys[] = {"room", "group", "scene", "automation"};
  return keys[core_storage::model_kind_index(kind)];
}

core_domain::ModelKind modelKind(const char* uri) {
  if (std::strstr(uri, "/rooms") != nullptr)
    return core_domain::ModelKind::room;
  if (std::strstr(uri, "/groups") != nullptr)
    return core_domain::ModelKind::group;
  if (std::strstr(uri, "/scenes") != nullptr)
    return core_domain::ModelKind::scene;
  return core_domain::ModelKind::automation;
}

cJSON* modelJson(const core_domain::ModelUpsert& model, core_domain::ModelKind kind) {
  cJSON* item = cJSON_CreateObject();
  if (!item)
    return nullptr;
  cJSON_AddStringToObject(item, "id", model.id);
  cJSON_AddStringToObject(item, "name", model.name);
  if (kind == core_domain::ModelKind::automation) {
    cJSON_AddBoolToObject(item, "enabled", model.enabled);
    cJSON_AddStringToObject(item, "trigger", model.trigger);
  }
  if (kind != core_domain::ModelKind::room) {
    cJSON* details = cJSON_AddArrayToObject(
        item, kind == core_domain::ModelKind::group ? "device_ids" : "actions");
    for (std::size_t index = 0; details && index < model.detail_count; ++index)
      cJSON_AddItemToArray(details, cJSON_CreateString(model.details[index]));
  }
  return item;
}

esp_err_t sendModelError(httpd_req_t* request, const char* http_status, const char* status,
                         const char* detail) {
  httpd_resp_set_status(request, http_status);
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", false);
  cJSON_AddStringToObject(response, "status", status);
  cJSON_AddStringToObject(response, "detail", detail);
  return sendJson(request, response);
}

esp_err_t modelsHandler(httpd_req_t* request, const HttpRuntime& runtime,
                        core_domain::ModelKind kind) {
  cJSON* response = cJSON_CreateObject();
  const char* key = modelKey(kind);
  char plural[16]{};
  std::snprintf(plural, sizeof(plural), "%ss", key);
  cJSON* items = cJSON_AddArrayToObject(response, plural);
  const std::size_t count = runtime.models.count(kind);
  for (std::size_t index = 0; items && index < count; ++index) {
    core_domain::ModelUpsert model{};
    if (runtime.models.get(kind, index, model))
      cJSON_AddItemToArray(items, modelJson(model, kind));
  }
  cJSON_AddBoolToObject(response, "writable", runtime.models.writable());
  cJSON_AddStringToObject(response, "detail",
                          !runtime.models.writable()
                              ? "Model database unavailable; record preserved read-only."
                          : count == 0 ? "No models configured."
                                       : "Models loaded.");
  return sendJson(request, response);
}

esp_err_t modelMutationHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  if (!runtime)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Missing runtime");
  const auto kind = modelKind(request->uri);
  const bool remove = std::strstr(request->uri, "/delete") != nullptr;
  RequestBody body(kMaxRequestBody + 1);
  std::size_t received = 0;
  if (!body || readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
    return sendModelError(request, "413 Payload Too Large",
                          core_domain::error_code::MODEL_MUTATION_REJECTED,
                          "Request body is empty or exceeds the bounded limit.");
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  if (!document)
    return sendModelError(request, "400 Bad Request",
                          core_domain::error_code::MODEL_MUTATION_REJECTED,
                          "Request body is not valid JSON.");
  core_domain::ModelUpsert model{};
  core_domain::ModelError parse_result = core_domain::ModelError::invalid_detail;
  if (remove) {
    parse_result = core_domain::parse_model_delete(document, model.id);
    model.has_id = parse_result == core_domain::ModelError::ok;
  } else {
    parse_result = core_domain::parse_model_upsert(document, kind, model);
  }
  cJSON_Delete(document);
  if (parse_result != core_domain::ModelError::ok)
    return sendModelError(request, "400 Bad Request",
                          core_domain::error_code::MODEL_MUTATION_REJECTED,
                          "Model payload failed strict validation.");
  ModelRepository::Result result{};
  if (!runtime->models.submit(remove ? ModelRepository::Mutation::remove
                                     : ModelRepository::Mutation::upsert,
                              kind, model, result))
    return sendModelError(request, "503 Service Unavailable",
                          core_domain::error_code::MODEL_MUTATION_REJECTED,
                          "Model mutation queue is full.");
  if (result.status == core_security::Status::NotFound)
    return sendModelError(request, "404 Not Found",
                          core_domain::error_code::MODEL_MUTATION_REJECTED,
                          "Model ID was not found.");
  if (result.status == core_security::Status::UnsupportedFormat)
    return sendModelError(request, "503 Service Unavailable",
                          core_domain::error_code::STORAGE_UNAVAILABLE,
                          "Model database is unavailable and read-only.");
  if (result.status == core_security::Status::TooLarge)
    return sendModelError(request, "409 Conflict", core_domain::error_code::MODEL_MUTATION_REJECTED,
                          "Model collection capacity is exhausted.");
  if (result.status != core_security::Status::Ok)
    return sendModelError(request, "500 Internal Server Error",
                          core_domain::error_code::STORAGE_WRITE_FAILED,
                          "Encrypted atomic storage write failed; live state was unchanged.");
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", true);
  cJSON_AddStringToObject(response, "status", "ok");
  cJSON_AddStringToObject(response, "detail", remove ? "Model deleted." : "Model saved.");
  if (!remove)
    cJSON_AddItemToObject(response, modelKey(kind), modelJson(result.model, kind));
  return sendJson(request, response);
}

esp_err_t automationRunHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  RequestBody body(256);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  char id[core_domain::max_id + 1]{};
  const bool valid = document != nullptr &&
                     core_domain::parse_model_delete(document, id) == core_domain::ModelError::ok;
  cJSON_Delete(document);
  if (!valid)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid automation request");

  const auto result = runtime->automations.submit(id);
  const bool accepted = result == AutomationService::SubmitResult::queued;
  const char* status = "AUTOMATION_QUEUE_FULL";
  const char* detail = "The automation queue is full.";
  switch (result) {
  case AutomationService::SubmitResult::queued:
    status = "queued";
    detail = "Manual automation queued.";
    break;
  case AutomationService::SubmitResult::not_found:
    status = "AUTOMATION_NOT_FOUND";
    detail = "The automation does not exist.";
    break;
  case AutomationService::SubmitResult::disabled:
    status = "AUTOMATION_DISABLED";
    detail = "Enable the automation before running it.";
    break;
  case AutomationService::SubmitResult::not_manual:
    status = "AUTOMATION_NOT_MANUAL";
    detail = "Only manual automations can be run on demand.";
    break;
  case AutomationService::SubmitResult::unsupported_action:
    status = "AUTOMATION_ACTION_UNSUPPORTED";
    detail = "This automation contains an action that is not implemented.";
    break;
  case AutomationService::SubmitResult::target_unavailable:
    status = "AUTOMATION_TARGET_UNAVAILABLE";
    detail = "A Matter target is unavailable.";
    break;
  case AutomationService::SubmitResult::queue_full:
    break;
  }
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", accepted);
  cJSON_AddStringToObject(response, "status", status);
  cJSON_AddStringToObject(response, "detail", detail);
  return sendJson(request, response);
}

esp_err_t matterHandler(httpd_req_t* request, const HttpRuntime& runtime) {
  const auto status = runtime.matter.status();
  const bool ready = status.state == CoreMatter::State::ready;
  cJSON* response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "implementation", "ESP-Matter controller");
  cJSON_AddBoolToObject(response, "commissioned", status.fabric_configured);
  cJSON_AddBoolToObject(response, "fabric_configured", status.fabric_configured);
  cJSON_AddNumberToObject(response, "subscriptions_active", status.subscriptions_active);
  cJSON_AddBoolToObject(response, "ipv6_ready", runtime.network.status().has_ipv6);
  cJSON_AddBoolToObject(response, "mdns_ready", status.case_ready);
  cJSON_AddBoolToObject(response, "dns_sd_ready", status.case_ready);
  cJSON_AddBoolToObject(response, "thread_ready",
                        status.border_router_ready && status.thread_attached);
  cJSON_AddBoolToObject(response, "thread_dataset_configured", status.thread_dataset_configured);
  cJSON_AddBoolToObject(response, "wifi_commissioning_ready", status.commissioner_ready);
  cJSON_AddBoolToObject(response, "thread_commissioning_ready",
                        status.commissioner_ready && status.thread_dataset_configured);
  cJSON_AddStringToObject(response, "adapter_status", matterState(status.state));
  cJSON_AddBoolToObject(response, "parser_ready", true);
  cJSON_AddBoolToObject(response, "transport_adapter_ready", status.commissioner_ready);
  cJSON_AddBoolToObject(response, "fabric_credentials_ready", status.fabric_configured);
  cJSON_AddBoolToObject(response, "pase_ready",
                        status.fabric_configured ||
                            status.commissioning_state == CoreMatter::CommissioningState::pase);
  cJSON_AddBoolToObject(response, "case_ready", status.case_ready);
  cJSON_AddBoolToObject(response, "subscriptions_ready", status.subscriptions_active != 0);
  cJSON* operation = cJSON_AddObjectToObject(response, "operation");
  if (status.commissioning_state == CoreMatter::CommissioningState::queued ||
      status.commissioning_state == CoreMatter::CommissioningState::pase ||
      status.commissioning_state == CoreMatter::CommissioningState::cancelling) {
    cJSON* active = cJSON_AddObjectToObject(operation, "active");
    cJSON_AddNumberToObject(active, "id", status.operation_id);
    cJSON_AddStringToObject(
        active, "transport",
        status.commissioning_transport == CoreMatter::CommissioningTransport::thread ? "thread"
                                                                                     : "wifi");
    cJSON_AddStringToObject(active, "phase", commissioningState(status.commissioning_state));
    cJSON_AddNumberToObject(active, "phase_retries", 0);
    cJSON_AddNumberToObject(active, "phase_timeout_ms", 180000);
    cJSON_AddNullToObject(active, "failure");
  } else {
    cJSON_AddNullToObject(operation, "active");
  }
  cJSON_AddNullToObject(operation, "pending");
  if (status.commissioning_state == CoreMatter::CommissioningState::complete ||
      status.commissioning_state == CoreMatter::CommissioningState::cancelled ||
      status.commissioning_state == CoreMatter::CommissioningState::failed) {
    cJSON* terminal = cJSON_AddObjectToObject(operation, "last_terminal");
    cJSON_AddNumberToObject(terminal, "id", status.operation_id);
    cJSON_AddStringToObject(
        terminal, "transport",
        status.commissioning_transport == CoreMatter::CommissioningTransport::thread ? "thread"
                                                                                     : "wifi");
    cJSON_AddStringToObject(terminal, "phase", commissioningState(status.commissioning_state));
    cJSON_AddNumberToObject(terminal, "phase_retries", 0);
    cJSON_AddNumberToObject(terminal, "phase_timeout_ms", 180000);
    if (status.commissioning_state == CoreMatter::CommissioningState::failed) {
      cJSON* failure = cJSON_AddObjectToObject(terminal, "failure");
      cJSON_AddStringToObject(failure, "code", esp_err_to_name(status.last_error));
      cJSON_AddStringToObject(failure, "detail", "Matter commissioning failed.");
    } else {
      cJSON_AddNullToObject(terminal, "failure");
    }
  } else {
    cJSON_AddNullToObject(operation, "last_terminal");
  }
  const auto command = runtime.matter.commandStatus();
  cJSON* command_operation = cJSON_AddObjectToObject(response, "command");
  cJSON_AddNumberToObject(command_operation, "id", command.operation_id);
  cJSON_AddStringToObject(command_operation, "kind", commandKind(command.kind));
  cJSON_AddStringToObject(command_operation, "phase", commandState(command.state));
  if (command.node_id != 0) {
    char node_id[17]{};
    std::snprintf(node_id, sizeof(node_id), "%016llx",
                  static_cast<unsigned long long>(command.node_id));
    cJSON_AddStringToObject(command_operation, "device_id", node_id);
    cJSON_AddNumberToObject(command_operation, "endpoint_id", command.endpoint_id);
    cJSON_AddBoolToObject(command_operation, "requested_on", command.requested_on);
  } else {
    cJSON_AddNullToObject(command_operation, "device_id");
    cJSON_AddNullToObject(command_operation, "endpoint_id");
    cJSON_AddNullToObject(command_operation, "requested_on");
  }
  cJSON_AddStringToObject(command_operation, "last_error", esp_err_to_name(command.last_error));
  cJSON_AddStringToObject(response, "last_error", esp_err_to_name(status.last_error));
  cJSON_AddStringToObject(response, "detail",
                          ready && status.case_ready && status.subscriptions_active != 0
                              ? "Matter commissioning, CASE, endpoint discovery, and subscriptions "
                                "are operational."
                          : ready && status.fabric_configured
                              ? "Matter controller fabric is restored and ready for device "
                                "operations."
                          : ready
                              ? "Matter controller is ready to commission a device."
                              : "Matter controller is unavailable; dashboard remains operational.");
  return sendJson(request, response);
}

esp_err_t onOffHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  RequestBody body(kMaxRequestBody + 1);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  }
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  if (document == nullptr) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid JSON");
  }
  const cJSON* device_id = cJSON_GetObjectItemCaseSensitive(document, "device_id");
  const cJSON* endpoint = cJSON_GetObjectItemCaseSensitive(document, "endpoint_id");
  const cJSON* on = cJSON_GetObjectItemCaseSensitive(document, "on");
  errno = 0;
  char* end = nullptr;
  const std::uint64_t node_id =
      cJSON_IsString(device_id) ? std::strtoull(device_id->valuestring, &end, 16) : 0;
  const bool valid_id = cJSON_IsString(device_id) && device_id->valuestring[0] != '\0' &&
                        end != nullptr && *end == '\0' && errno == 0 && node_id != 0;
  const bool valid_endpoint =
      endpoint == nullptr ||
      (cJSON_IsNumber(endpoint) && endpoint->valuedouble == endpoint->valueint &&
       endpoint->valueint > 0 && endpoint->valueint <= 0xffff);
  const std::uint16_t endpoint_id =
      endpoint == nullptr ? 1 : static_cast<std::uint16_t>(endpoint->valueint);
  const bool valid = valid_id && valid_endpoint && cJSON_IsBool(on) &&
                     runtime->devices.contains(node_id, endpoint_id);
  std::uint32_t operation_id = 0;
  const esp_err_t result =
      valid ? runtime->matter.setOnOff(node_id, endpoint_id, cJSON_IsTrue(on), operation_id)
            : ESP_ERR_INVALID_ARG;
  cJSON_Delete(document);

  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", result == ESP_OK);
  if (result == ESP_OK) {
    cJSON_AddNumberToObject(response, "operation_id", operation_id);
  } else {
    cJSON_AddNullToObject(response, "operation_id");
  }
  cJSON_AddStringToObject(response, "status", result == ESP_OK ? "queued" : "rejected");
  cJSON_AddStringToObject(response, "detail",
                          result == ESP_OK ? "Matter OnOff command queued."
                                           : "Matter device or endpoint is unavailable.");
  return sendJson(request, response);
}

esp_err_t resetModelsHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  RequestBody body(kMaxRequestBody + 1);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  const cJSON* confirmation =
      document == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(document, "confirmation");
  const bool confirmed =
      cJSON_IsString(confirmation) &&
      std::strcmp(confirmation->valuestring, "RESET UNREADABLE MODEL DATABASE") == 0;
  cJSON_Delete(document);

  ModelRepository::Result reset_result{};
  const bool submitted = confirmed && runtime->models.resetUnreadable(reset_result);
  const bool accepted = submitted && reset_result.status == core_security::Status::Ok;
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", accepted);
  cJSON_AddStringToObject(response, "status", accepted ? "reset" : "rejected");
  cJSON_AddStringToObject(response, "detail",
                          accepted ? "The unreadable encrypted database was archived and replaced "
                                     "with an empty one."
                          : !confirmed ? "Provide the exact reset confirmation."
                                       : "The encrypted model database could not be reset safely.");
  return sendJson(request, response);
}

esp_err_t resetDeviceRegistryHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  RequestBody body(kMaxRequestBody + 1);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  }
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  const cJSON* confirmation =
      document == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(document, "confirmation");
  const bool confirmed =
      cJSON_IsString(confirmation) &&
      std::strcmp(confirmation->valuestring, "RESET MATTER DEVICE REGISTRY") == 0;
  const auto result =
      confirmed ? runtime->devices.resetUnsupported() : core_security::Status::Invalid;
  cJSON_Delete(document);

  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", result == core_security::Status::Ok);
  cJSON_AddStringToObject(response, "status",
                          result == core_security::Status::Ok ? "reset" : "rejected");
  cJSON_AddStringToObject(response, "detail",
                          result == core_security::Status::Ok
                              ? "The unsupported encrypted registry was "
                                "archived and a new registry was created."
                              : "Reset rejected; provide the exact "
                                "confirmation or inspect storage state.");
  return sendJson(request, response);
}

esp_err_t cancelCommissionHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  RequestBody body(kMaxRequestBody + 1);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  }
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  if (document == nullptr) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid JSON");
  }
  const cJSON* operation = cJSON_GetObjectItemCaseSensitive(document, "operation_id");
  const bool valid = cJSON_IsNumber(operation) && operation->valuedouble == operation->valueint &&
                     operation->valueint > 0;
  const esp_err_t result =
      valid ? runtime->matter.cancelCommissioning(static_cast<std::uint32_t>(operation->valueint))
            : ESP_ERR_INVALID_ARG;
  cJSON_Delete(document);

  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", result == ESP_OK);
  cJSON_AddStringToObject(response, "status", result == ESP_OK ? "cancelling" : "rejected");
  cJSON_AddStringToObject(response, "detail",
                          result == ESP_OK ? "Matter commissioning cancellation queued."
                                           : "No matching commissioning operation is active.");
  return sendJson(request, response);
}

esp_err_t threadDatasetHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  RequestBody body(640);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  const cJSON* encoded =
      document == nullptr ? nullptr
                          : cJSON_GetObjectItemCaseSensitive(document, "active_dataset_tlvs_hex");
  ThreadDataset dataset;
  bool valid = cJSON_IsString(encoded) && decodeThreadDatasetHex(encoded->valuestring, dataset);
  if (valid) {
    ThreadDataset current;
    const auto load_result = loadThreadDataset(runtime->storage, current);
    if (load_result == core_security::Status::Ok)
      dataset.generation = current.generation;
    else if (load_result != core_security::Status::NotFound &&
             load_result != core_security::Status::Invalid)
      valid = false;
    current.clear();
  }
  const auto save_result = valid ? saveThreadDataset(runtime->storage, runtime->random, dataset)
                                 : core_security::Status::Invalid;
  const esp_err_t apply_result =
      save_result == core_security::Status::Ok
          ? runtime->matter.applyThreadDataset(dataset.tlvs, dataset.length)
          : ESP_ERR_INVALID_STATE;
  dataset.clear();
  cJSON_Delete(document);

  cJSON* response = cJSON_CreateObject();
  const bool accepted = save_result == core_security::Status::Ok && apply_result == ESP_OK;
  cJSON_AddBoolToObject(response, "accepted", accepted);
  cJSON_AddStringToObject(response, "status",
                          accepted                                   ? "queued"
                          : !valid                                   ? "THREAD_DATASET_INVALID"
                          : save_result != core_security::Status::Ok ? "STORAGE_WRITE_FAILED"
                                                                     : "MATTER_QUEUE_FULL");
  cJSON_AddStringToObject(response, "detail",
                          accepted ? "Active Thread dataset encrypted and queued for application."
                          : !valid ? "Active Thread dataset is missing, malformed, or unsupported."
                          : save_result != core_security::Status::Ok
                              ? "Active Thread dataset could not be saved to encrypted storage."
                              : "Active Thread dataset was saved and will be applied after "
                                "restart.");
  return sendJson(request, response);
}

esp_err_t commissionHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  if (runtime == nullptr || request->content_len <= 0 || request->content_len > 192) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  }
  char body[193]{};
  std::size_t received = 0;
  while (received < static_cast<std::size_t>(request->content_len)) {
    const int result = httpd_req_recv(request, body + received, request->content_len - received);
    if (result <= 0) {
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete request");
    }
    received += static_cast<std::size_t>(result);
  }
  cJSON* document = cJSON_ParseWithLength(body, received);
  if (document == nullptr) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid JSON");
  }
  const cJSON* transport = cJSON_GetObjectItemCaseSensitive(document, "transport");
  const cJSON* payload = cJSON_GetObjectItemCaseSensitive(document, "setup_payload");
  const bool has_transport = cJSON_IsString(transport);
  const bool requested_thread = has_transport && std::strcmp(transport->valuestring, "thread") == 0;
  const bool requested_wifi = has_transport && std::strcmp(transport->valuestring, "wifi") == 0;
  const bool valid = cJSON_IsString(payload) && (requested_wifi || requested_thread);
  std::uint32_t operation_id = 0;
  const esp_err_t result =
      valid
          ? runtime->matter.commission(requested_thread ? CoreMatter::CommissioningTransport::thread
                                                        : CoreMatter::CommissioningTransport::wifi,
                                       payload->valuestring, operation_id)
          : ESP_ERR_INVALID_ARG;
  cJSON_Delete(document);

  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", result == ESP_OK);
  if (result == ESP_OK) {
    cJSON_AddNumberToObject(response, "operation_id", operation_id);
  } else {
    cJSON_AddNullToObject(response, "operation_id");
  }
  cJSON_AddStringToObject(response, "transport", requested_thread ? "thread" : "wifi");
  cJSON_AddStringToObject(response, "status", result == ESP_OK ? "queued" : "rejected");
  cJSON_AddStringToObject(response, "detail",
                          result == ESP_OK ? "Matter commissioning queued."
                                           : "Matter commissioning request was rejected.");
  return sendJson(request, response);
}

const char* aiMutationStatus(ai::Status status, const char* invalid_code) {
  switch (status) {
  case ai::Status::Ok:
    return "saved";
  case ai::Status::InvalidArgument:
  case ai::Status::TooLarge:
  case ai::Status::CapacityExceeded:
    return invalid_code;
  case ai::Status::NotReady:
  case ai::Status::StorageError:
  case ai::Status::CorruptData:
  case ai::Status::UnsupportedFormat:
  case ai::Status::NotFound:
    return core_domain::error_code::STORAGE_UNAVAILABLE;
  }
  return core_domain::error_code::STORAGE_UNAVAILABLE;
}

esp_err_t sendAiMutation(httpd_req_t* request, ai::Status status, const char* invalid_code,
                         const char* success_detail) {
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", status == ai::Status::Ok);
  cJSON_AddStringToObject(response, "status", aiMutationStatus(status, invalid_code));
  cJSON_AddStringToObject(response, "detail",
                          status == ai::Status::Ok ? success_detail
                          : status == ai::Status::CapacityExceeded
                              ? "The encrypted AI repository has reached its item limit."
                          : status == ai::Status::TooLarge
                              ? "The AI content exceeds the encrypted repository limit."
                          : status == ai::Status::InvalidArgument
                              ? "The AI storage request is invalid."
                              : "Encrypted AI storage is unavailable or unreadable.");
  return sendJson(request, response);
}

bool addMarkdownItem(cJSON* array, const char* key, const std::uint8_t* markdown,
                     std::size_t markdown_size, bool prompt, bool baseline) {
  RequestBody text(markdown_size + 1);
  if (!text)
    return false;
  if (markdown_size)
    std::memcpy(text.data(), markdown, markdown_size);
  cJSON* item = cJSON_CreateObject();
  if (!item)
    return false;
  const bool ok = cJSON_AddStringToObject(item, prompt ? "path" : "id", key) != nullptr &&
                  cJSON_AddStringToObject(item, "title", key) != nullptr &&
                  cJSON_AddStringToObject(item, "markdown", text.data()) != nullptr &&
                  (!prompt || cJSON_AddBoolToObject(item, "baseline", baseline) != nullptr) &&
                  cJSON_AddItemToArray(array, item);
  if (!ok)
    cJSON_Delete(item);
  return ok;
}

struct AiListContext final {
  cJSON* array;
  bool ok{true};
};

bool addMemorySection(const ai::MemorySectionView& section, void* raw) {
  auto& context = *static_cast<AiListContext*>(raw);
  context.ok = addMarkdownItem(context.array, section.id, section.markdown, section.markdown_size,
                               false, false);
  return context.ok;
}

bool addPromptFile(const ai::PromptFileView& file, void* raw) {
  auto& context = *static_cast<AiListContext*>(raw);
  context.ok = addMarkdownItem(context.array, file.path, file.markdown, file.markdown_size, true,
                               file.baseline);
  return context.ok;
}

esp_err_t aiStatusHandler(httpd_req_t* request, HttpRuntime& runtime) {
  ai::ProviderConfig provider{};
  const ai::Status provider_status = runtime.ai_storage.provider.load(provider);
  const bool configured = provider_status == ai::Status::Ok;
  ai::clear_provider_config(provider);

  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "enabled", false);
  cJSON_AddBoolToObject(response, "provider_configured", configured);
  cJSON_AddBoolToObject(response, "mcp_enabled", true);
  cJSON_AddNumberToObject(response, "max_tool_rounds", core_domain::max_ai_tool_rounds);
  cJSON_AddNumberToObject(response, "max_repeated_tool_calls", 3);
  cJSON_AddNumberToObject(response, "max_actions", core_domain::max_actions);
  cJSON_AddStringToObject(response, "destructive_confirmation", "CONFIRM");
  cJSON_AddStringToObject(response, "persistence", "encrypted flash with optional SD replication");
  cJSON* transport = cJSON_AddObjectToObject(response, "provider_transport");
  cJSON_AddBoolToObject(transport, "http_ipv4_literal", false);
  cJSON_AddBoolToObject(transport, "dns", true);
  cJSON_AddBoolToObject(transport, "tls", true);
  cJSON_AddBoolToObject(transport, "websockets", false);
  cJSON_AddBoolToObject(transport, "certificate_validation", true);
  cJSON_AddStringToObject(transport, "detail",
                          configured ? "HTTPS provider configured; availability is checked by each "
                                       "request."
                                     : "Configure an HTTPS AI provider to enable chat.");
  cJSON_AddArrayToObject(response, "provider_transport_requirements");
  return sendJson(request, response);
}

esp_err_t aiListHandler(httpd_req_t* request, HttpRuntime& runtime, bool prompts) {
  cJSON* response = cJSON_CreateObject();
  cJSON* entries = cJSON_AddArrayToObject(response, prompts ? "files" : "sections");
  AiListContext context{entries};
  const ai::Status status = prompts ? runtime.ai_storage.prompts.visit(addPromptFile, &context)
                                    : runtime.ai_storage.memory.visit(addMemorySection, &context);
  const bool empty = status == ai::Status::NotFound;
  cJSON_AddBoolToObject(response, "encrypted", true);
  cJSON_AddStringToObject(
      response, "detail",
      status == ai::Status::Ok && context.ok
          ? (prompts ? "Encrypted AI prompts loaded." : "Encrypted AI memory loaded.")
      : empty ? (prompts ? "No AI prompts configured." : "No AI memory configured.")
              : "Encrypted AI storage is unavailable or unreadable.");
  return sendJson(request, response);
}

bool copyJsonString(const cJSON* item, char* output, std::size_t capacity) {
  if (!cJSON_IsString(item) || !item->valuestring || capacity == 0)
    return false;
  const std::size_t size = strnlen(item->valuestring, capacity);
  if (size >= capacity)
    return false;
  std::memcpy(output, item->valuestring, size + 1);
  return true;
}

esp_err_t aiProviderHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  RequestBody body(core_domain::max_json_body + 1);
  std::size_t received{};
  cJSON* document = nullptr;
  const auto* contract = core_domain::find_contract("POST", "/api/v1/ai/provider");
  if (!runtime || !body || !contract ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK ||
      core_domain::parse_contract_request(*contract, body.data(), received, document) !=
          core_domain::ApiError::ok)
    return sendAiMutation(request, ai::Status::InvalidArgument,
                          core_domain::error_code::AI_PROVIDER_CONFIG_INVALID,
                          "Encrypted AI provider configuration saved.");

  const cJSON* input = cJSON_GetObjectItemCaseSensitive(document, "provider");
  const cJSON* backend = cJSON_GetObjectItemCaseSensitive(input, "backend");
  const cJSON* auth = cJSON_GetObjectItemCaseSensitive(input, "auth_type");
  const cJSON* temperature = cJSON_GetObjectItemCaseSensitive(input, "temperature");
  const cJSON* api_key = cJSON_GetObjectItemCaseSensitive(input, "api_key");
  ai::ProviderConfig config{};
  bool valid = copyJsonString(cJSON_GetObjectItemCaseSensitive(input, "base_url"), config.base_url,
                              sizeof(config.base_url)) &&
               copyJsonString(cJSON_GetObjectItemCaseSensitive(input, "model"), config.model,
                              sizeof(config.model));
  if (valid && std::strcmp(backend->valuestring, "open_ai_compatible") == 0)
    config.backend = ai::ProviderBackend::OpenAiCompatible;
  else if (valid && std::strcmp(backend->valuestring, "anthropic_compatible") == 0)
    config.backend = ai::ProviderBackend::AnthropicCompatible;
  else
    valid = false;
  if (valid && std::strcmp(auth->valuestring, "bearer") == 0)
    config.auth_type = ai::AuthType::Bearer;
  else if (valid && std::strcmp(auth->valuestring, "api_key") == 0)
    config.auth_type = ai::AuthType::ApiKey;
  else if (valid && std::strcmp(auth->valuestring, "none") == 0)
    config.auth_type = ai::AuthType::None;
  else
    valid = false;
  config.max_output_tokens = static_cast<std::uint32_t>(
      cJSON_GetObjectItemCaseSensitive(input, "max_output_tokens")->valuedouble);
  config.timeout_ms = static_cast<std::uint32_t>(
      cJSON_GetObjectItemCaseSensitive(input, "timeout_ms")->valuedouble);
  config.has_temperature = cJSON_IsNumber(temperature);
  if (config.has_temperature)
    config.temperature = static_cast<float>(temperature->valuedouble);
  config.api_key_redacted =
      cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(input, "api_key_redacted"));
  if (cJSON_IsString(api_key)) {
    const std::size_t key_size = std::strlen(api_key->valuestring);
    valid = valid && key_size <= sizeof(config.api_key);
    if (valid) {
      config.api_key_length = static_cast<std::uint16_t>(key_size);
      std::memcpy(config.api_key, api_key->valuestring, key_size);
    }
  }
  const ai::Status status =
      valid ? runtime->ai_storage.provider.save(config) : ai::Status::InvalidArgument;
  ai::clear_provider_config(config);
  if (cJSON_IsString(api_key))
    mbedtls_platform_zeroize(api_key->valuestring, std::strlen(api_key->valuestring));
  cJSON_Delete(document);
  return sendAiMutation(request, status, core_domain::error_code::AI_PROVIDER_CONFIG_INVALID,
                        "Encrypted AI provider configuration saved.");
}

esp_err_t aiContentHandler(httpd_req_t* request, bool prompt) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  const char* path = prompt ? "/api/v1/ai/prompts/file" : "/api/v1/ai/memory/section";
  const std::size_t body_capacity =
      prompt ? core_domain::max_json_body + 1 : core_domain::max_ai_memory_body + 1;
  RequestBody body(body_capacity);
  std::size_t received{};
  cJSON* document = nullptr;
  const auto* contract = core_domain::find_contract("POST", path);
  if (!runtime || !body || !contract ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK ||
      core_domain::parse_contract_request(*contract, body.data(), received, document) !=
          core_domain::ApiError::ok)
    return sendAiMutation(request, ai::Status::InvalidArgument,
                          prompt ? core_domain::error_code::AI_PROMPT_INVALID
                                 : core_domain::error_code::AI_MEMORY_TOO_LARGE,
                          prompt ? "Encrypted AI prompt saved." : "Encrypted AI memory saved.");

  const cJSON* key = cJSON_GetObjectItemCaseSensitive(document, prompt ? "path" : "id");
  const cJSON* markdown = cJSON_GetObjectItemCaseSensitive(document, "markdown");
  const cJSON* encoded = cJSON_GetObjectItemCaseSensitive(document, "markdown_base64");
  RequestBody decoded(prompt ? ai::kMaxPromptMarkdownBytes + 1 : 1);
  static constexpr char empty_markdown[] = "";
  const char* markdown_text = cJSON_IsString(markdown) ? markdown->valuestring : empty_markdown;
  const std::uint8_t* content = reinterpret_cast<const std::uint8_t*>(markdown_text);
  std::size_t content_size = std::strlen(markdown_text);
  bool valid = true;
  if (prompt && cJSON_IsString(encoded)) {
    std::size_t decoded_size{};
    const int decode_result =
        decoded
            ? mbedtls_base64_decode(reinterpret_cast<unsigned char*>(decoded.data()),
                                    ai::kMaxPromptMarkdownBytes, &decoded_size,
                                    reinterpret_cast<const unsigned char*>(encoded->valuestring),
                                    std::strlen(encoded->valuestring))
            : MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
    valid = decode_result == 0 && decoded_size <= ai::kMaxPromptMarkdownBytes;
    content = reinterpret_cast<const std::uint8_t*>(decoded.data());
    content_size = decoded_size;
  }
  const ai::Status status =
      !valid   ? ai::Status::InvalidArgument
      : prompt ? runtime->ai_storage.prompts.upsert(key->valuestring, content, content_size, false)
               : runtime->ai_storage.memory.upsert(key->valuestring, content, content_size);
  cJSON_Delete(document);
  return sendAiMutation(request, status,
                        prompt ? core_domain::error_code::AI_PROMPT_INVALID
                               : core_domain::error_code::AI_MEMORY_TOO_LARGE,
                        prompt ? "Encrypted AI prompt saved." : "Encrypted AI memory saved.");
}

esp_err_t aiMemoryMutationHandler(httpd_req_t* request) {
  return aiContentHandler(request, false);
}

esp_err_t aiPromptMutationHandler(httpd_req_t* request) {
  return aiContentHandler(request, true);
}

const char* aiChatState(AiChatService::State state) {
  switch (state) {
  case AiChatService::State::idle:
    return "idle";
  case AiChatService::State::queued:
  case AiChatService::State::processing:
    return "processing";
  case AiChatService::State::complete:
    return "complete";
  case AiChatService::State::failed:
    return "failed";
  }
  return "failed";
}

esp_err_t sendAiChatStatus(httpd_req_t* request, HttpRuntime& runtime) {
  RequestBody reply(AiChatService::kMaxResponseBytes + 1);
  AiChatService::Snapshot snapshot{};
  if (!reply || !runtime.ai_chat.status(snapshot, reply.data(), reply.capacity())) {
    httpd_resp_set_status(request, "503 Service Unavailable");
    cJSON* response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "accepted", false);
    cJSON_AddStringToObject(response, "status", "AI_CHAT_BUSY");
    cJSON_AddStringToObject(response, "reply", "AI chat status is momentarily unavailable.");
    cJSON_AddArrayToObject(response, "actions");
    return sendJson(request, response);
  }
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", snapshot.state != AiChatService::State::failed);
  cJSON_AddStringToObject(response, "status", aiChatState(snapshot.state));
  cJSON_AddStringToObject(response, "reply",
                          snapshot.state == AiChatService::State::complete ? reply.data()
                          : snapshot.state == AiChatService::State::failed ? snapshot.error_detail
                                                                           : "");
  cJSON_AddArrayToObject(response, "actions");
  return sendJson(request, response);
}

esp_err_t aiChatHandler(httpd_req_t* request) {
  auto* runtime = static_cast<HttpRuntime*>(request->user_ctx);
  if (runtime == nullptr)
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Missing runtime");
  if (request->method == HTTP_GET)
    return sendAiChatStatus(request, *runtime);

  RequestBody body(kMaxRequestBody + 1);
  std::size_t received = 0;
  if (!body || readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_413_CONTENT_TOO_LARGE, "Chat request is too large");
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  static constexpr const char* fields[] = {"message", "history", "allow_actions"};
  static constexpr const char* fields_with_id[] = {"message", "history", "chat_id",
                                                   "allow_actions"};
  const bool has_chat_id =
      cJSON_IsObject(document) && cJSON_GetObjectItemCaseSensitive(document, "chat_id") != nullptr;
  bool valid = exactObject(document, has_chat_id ? fields_with_id : fields, has_chat_id ? 4 : 3);
  const cJSON* message = cJSON_GetObjectItemCaseSensitive(document, "message");
  const cJSON* history = cJSON_GetObjectItemCaseSensitive(document, "history");
  const cJSON* allow_actions = cJSON_GetObjectItemCaseSensitive(document, "allow_actions");
  const int history_count = cJSON_IsArray(history) ? cJSON_GetArraySize(history) : -1;
  valid = valid && cJSON_IsString(message) && cJSON_IsArray(history) &&
          (cJSON_IsTrue(allow_actions) || cJSON_IsFalse(allow_actions)) && history_count >= 0 &&
          history_count <= static_cast<int>(AiChatService::kMaxHistoryEntries);
  AiChatService::HistoryEntry entries[AiChatService::kMaxHistoryEntries]{};
  for (int index = 0; valid && index < history_count; ++index) {
    const cJSON* item = cJSON_GetArrayItem(history, index);
    static constexpr const char* history_fields[] = {"role", "content"};
    const cJSON* role = cJSON_GetObjectItemCaseSensitive(item, "role");
    const cJSON* content = cJSON_GetObjectItemCaseSensitive(item, "content");
    valid = exactObject(item, history_fields, 2) && cJSON_IsString(role) && cJSON_IsString(content);
    if (!valid)
      break;
    if (std::strcmp(role->valuestring, "user") == 0)
      entries[index].role = AiChatService::Role::user;
    else if (std::strcmp(role->valuestring, "assistant") == 0)
      entries[index].role = AiChatService::Role::assistant;
    else if (std::strcmp(role->valuestring, "tool") == 0)
      entries[index].role = AiChatService::Role::tool;
    else
      valid = false;
    entries[index].content = content->valuestring;
    entries[index].content_length = std::strlen(content->valuestring);
  }
  AiChatService::Request chat{};
  if (valid) {
    chat.message = message->valuestring;
    chat.message_length = std::strlen(message->valuestring);
    chat.history = entries;
    chat.history_count = static_cast<std::size_t>(history_count);
    chat.allow_actions = cJSON_IsTrue(allow_actions);
  }
  std::uint64_t operation_id = 0;
  const auto submitted =
      valid ? runtime->ai_chat.submit(chat, operation_id) : AiChatService::SubmitResult::invalid;
  cJSON_Delete(document);
  const bool accepted = submitted == AiChatService::SubmitResult::queued;
  if (!accepted)
    httpd_resp_set_status(request, submitted == AiChatService::SubmitResult::busy
                                       ? "409 Conflict"
                                       : "400 Bad Request");
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", accepted);
  cJSON_AddStringToObject(response, "status", accepted ? "processing" : "AI_CHAT_REJECTED");
  cJSON_AddStringToObject(response, "reply", "");
  cJSON_AddArrayToObject(response, "actions");
  return sendJson(request, response);
}

esp_err_t mcpToolsHandler(httpd_req_t* request) {
  return sendJson(request, McpService::tools());
}

esp_err_t mcpContextHandler(httpd_req_t* request) {
  const auto* runtime = static_cast<const HttpRuntime*>(request->user_ctx);
  return runtime == nullptr
             ? httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Missing runtime")
             : sendJson(request, McpService::context(*runtime));
}

esp_err_t mcpInvokeHandler(httpd_req_t* request) {
  const auto* runtime = static_cast<const HttpRuntime*>(request->user_ctx);
  RequestBody body(kMaxRequestBody + 1);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  const cJSON* tool =
      document == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(document, "tool");
  const cJSON* arguments_json =
      document == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(document, "arguments_json");
  cJSON* arguments =
      cJSON_IsString(arguments_json) ? cJSON_Parse(arguments_json->valuestring) : nullptr;
  cJSON* invocation = nullptr;
  const esp_err_t result =
      cJSON_IsString(tool) && cJSON_IsObject(arguments)
          ? McpService::invoke(*runtime, tool->valuestring, arguments, invocation)
          : ESP_ERR_INVALID_ARG;
  cJSON_Delete(arguments);
  cJSON_Delete(document);
  cJSON_Delete(invocation);
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", result == ESP_OK);
  cJSON_AddStringToObject(response, "status", result == ESP_OK ? "queued" : "AI_TOOL_REJECTED");
  cJSON_AddStringToObject(response, "detail",
                          result == ESP_OK ? "MCP tool accepted."
                                           : "Tool unavailable or arguments invalid.");
  return sendJson(request, response);
}

esp_err_t mcpJsonRpcHandler(httpd_req_t* request) {
  const auto* runtime = static_cast<const HttpRuntime*>(request->user_ctx);
  RequestBody body(kMaxRequestBody + 1);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  if (document == nullptr) {
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddNullToObject(response, "id");
    cJSON* error = cJSON_AddObjectToObject(response, "error");
    cJSON_AddNumberToObject(error, "code", -32700);
    cJSON_AddStringToObject(error, "message", "Parse error");
    return sendJson(request, response);
  }
  bool has_response = true;
  cJSON* response = McpService::jsonRpc(*runtime, document, has_response);
  cJSON_Delete(document);
  if (!has_response) {
    httpd_resp_set_status(request, "202 Accepted");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "Cache-Control", "no-store"), kTag, "cache");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "Connection", "close"), kTag, "connection");
    return httpd_resp_send(request, nullptr, 0);
  }
  return sendJson(request, response);
}

esp_err_t mcpPermissionHandler(httpd_req_t* request) {
  const auto* runtime = static_cast<const HttpRuntime*>(request->user_ctx);
  RequestBody body(kMaxRequestBody + 1);
  std::size_t received = 0;
  if (runtime == nullptr || !body ||
      readRequestBody(request, body.data(), body.capacity(), received) != ESP_OK)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
  cJSON* document = cJSON_ParseWithLength(body.data(), received);
  const cJSON* tool =
      document == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(document, "tool");
  const cJSON* enabled =
      document == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(document, "enabled");
  const esp_err_t result =
      cJSON_IsString(tool) && cJSON_IsBool(enabled)
          ? McpService::setPermission(*runtime, tool->valuestring, cJSON_IsTrue(enabled))
          : ESP_ERR_INVALID_ARG;
  cJSON_Delete(document);
  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "accepted", result == ESP_OK);
  cJSON_AddStringToObject(response, "status", result == ESP_OK ? "saved" : "AI_TOOL_REJECTED");
  char detail[96]{};
  if (result == ESP_OK)
    std::snprintf(detail, sizeof(detail), "%s", "Encrypted MCP tool permission updated.");
  else
    std::snprintf(detail, sizeof(detail), "Tool permission update failed: %s.",
                  esp_err_to_name(result));
  cJSON_AddStringToObject(response, "detail", detail);
  return sendJson(request, response);
}

esp_err_t statusHandler(httpd_req_t* request) {
  const auto* runtime = static_cast<const HttpRuntime*>(request->user_ctx);
  if (runtime == nullptr) {
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Missing runtime");
  }
  if (std::strcmp(request->uri, "/api/v1/system") == 0) {
    return systemHandler(request, *runtime);
  }
  if (std::strcmp(request->uri, "/api/v1/network") == 0) {
    return networkHandler(request, *runtime);
  }
  if (std::strcmp(request->uri, "/api/v1/matter") == 0) {
    return matterHandler(request, *runtime);
  }
  if (std::strcmp(request->uri, "/api/v1/time") == 0) {
    return timeStatusHandler(request);
  }
  if (std::strcmp(request->uri, "/api/v1/devices") == 0) {
    return devicesHandler(request, *runtime);
  }
  if (std::strcmp(request->uri, "/api/v1/storage") == 0) {
    const auto io = core_storage::last_io_diagnostic();
    const auto replication = core_storage::replication_diagnostic();
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "state", replication.storage_ready ? "ready" : "unavailable");
    cJSON_AddStringToObject(response, "primary", "encrypted-sd-card");
    cJSON_AddStringToObject(response, "sd_root", "/core-pilot/");
    cJSON_AddStringToObject(response, "sd_storage",
                            !replication.sd_available     ? "unavailable"
                            : replication.sd_synchronized ? "ready"
                                                          : "error");
    cJSON_AddBoolToObject(response, "encrypted", true);
    cJSON_AddBoolToObject(response, "mutations_allowed", replication.storage_ready);
    cJSON_AddBoolToObject(response, "device_seed_exportable", false);
    cJSON_AddStringToObject(response, "last_sd_failure", io.operation);
    cJSON_AddNumberToObject(response, "last_sd_errno", io.error_number);
    cJSON_AddNumberToObject(response, "successful_sd_writes", replication.successful_sd_writes);
    cJSON_AddStringToObject(response, "detail",
                            !replication.storage_ready || !replication.sd_available
                                ? "Required encrypted SD storage is unavailable."
                            : replication.sd_synchronized ? "Encrypted SD storage is ready."
                                                          : "Encrypted SD storage has failed.");
    return sendJson(request, response);
  }
  if (std::strcmp(request->uri, "/api/v1/smtp") == 0) {
    return smtpStatusHandler(request);
  }
  if (std::strcmp(request->uri, "/api/v1/ai") == 0) {
    return aiStatusHandler(request, *const_cast<HttpRuntime*>(runtime));
  }
  if (std::strcmp(request->uri, "/api/v1/ai/memory") == 0) {
    return aiListHandler(request, *const_cast<HttpRuntime*>(runtime), false);
  }
  if (std::strcmp(request->uri, "/api/v1/ai/prompts") == 0) {
    return aiListHandler(request, *const_cast<HttpRuntime*>(runtime), true);
  }
  if (std::strcmp(request->uri, "/api/v1/ai/mcp/tools") == 0) {
    return mcpToolsHandler(request);
  }
  if (std::strcmp(request->uri, "/api/v1/ai/mcp/context") == 0) {
    return mcpContextHandler(request);
  }
  if (std::strcmp(request->uri, "/api/v1/rooms") == 0 ||
      std::strcmp(request->uri, "/api/v1/groups") == 0 ||
      std::strcmp(request->uri, "/api/v1/scenes") == 0 ||
      std::strcmp(request->uri, "/api/v1/automations") == 0) {
    return modelsHandler(request, *runtime, modelKind(request->uri));
  }

  return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Not found");
}

} // namespace

esp_err_t initializeTimeService(const HttpRuntime& runtime) {
  TimeConfig loaded{};
  const auto load_result = loadTimeConfig(runtime, loaded);
  if (load_result == core_security::Status::NotFound) {
    return applyTimezoneOffset(0) ? ESP_OK : ESP_FAIL;
  }
  if (load_result != core_security::Status::Ok) {
    return ESP_ERR_INVALID_RESPONSE;
  }
  const esp_err_t result = startTimeService(loaded.ntp_server_host, loaded.timezone_offset_minutes);
  if (result != ESP_OK) {
    return result;
  }
  loaded.sntp_initialized = true;
  taskENTER_CRITICAL(&time_config_lock);
  time_config = loaded;
  taskEXIT_CRITICAL(&time_config_lock);
  return ESP_OK;
}

httpd_handle_t startHttpServer(const HttpRuntime& runtime) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_open_sockets = kMaxHttpConnections;
  config.max_uri_handlers = 56;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.lru_purge_enable = true;
  config.stack_size = 12288;

  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) {
    return nullptr;
  }

  const httpd_uri_t dashboard{
      .uri = "/", .method = HTTP_GET, .handler = dashboardHandler, .user_ctx = nullptr};
  const httpd_uri_t health{
      .uri = "/api/v1/health", .method = HTTP_GET, .handler = healthHandler, .user_ctx = nullptr};
  const httpd_uri_t reboot{.uri = "/api/v1/system/reboot",
                           .method = HTTP_POST,
                           .handler = rebootHandler,
                           .user_ctx = nullptr};
  const httpd_uri_t ota_prepare{.uri = "/api/v1/ota/prepare",
                                .method = HTTP_POST,
                                .handler = otaMutationHandler,
                                .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t ota_chunk{.uri = "/api/v1/ota/chunk",
                              .method = HTTP_POST,
                              .handler = otaMutationHandler,
                              .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t commission{.uri = "/api/v1/matter/commission",
                               .method = HTTP_POST,
                               .handler = commissionHandler,
                               .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t thread_dataset{.uri = "/api/v1/matter/thread/dataset",
                                   .method = HTTP_POST,
                                   .handler = threadDatasetHandler,
                                   .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t automation_run{.uri = "/api/v1/automations/run",
                                   .method = HTTP_POST,
                                   .handler = automationRunHandler,
                                   .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t on_off{.uri = "/api/v1/matter/on-off",
                           .method = HTTP_POST,
                           .handler = onOffHandler,
                           .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t registry_reset{.uri = "/api/v1/matter/devices/reset",
                                   .method = HTTP_POST,
                                   .handler = resetDeviceRegistryHandler,
                                   .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t models_reset{.uri = "/api/v1/storage/models/reset",
                                 .method = HTTP_POST,
                                 .handler = resetModelsHandler,
                                 .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t commission_cancel{.uri = "/api/v1/matter/commission/cancel",
                                      .method = HTTP_POST,
                                      .handler = cancelCommissionHandler,
                                      .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t time_config{.uri = "/api/v1/time",
                                .method = HTTP_POST,
                                .handler = timeConfigHandler,
                                .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t smtp_config{.uri = "/api/v1/smtp",
                                .method = HTTP_POST,
                                .handler = smtpConfigHandler,
                                .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t smtp_test{.uri = "/api/v1/smtp/test",
                              .method = HTTP_POST,
                              .handler = smtpTestHandler,
                              .user_ctx = const_cast<HttpRuntime*>(&runtime)};

  const httpd_uri_t activity_page{.uri = "/api/v1/activity/page",
                                  .method = HTTP_POST,
                                  .handler = activityPageHandler,
                                  .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t wifi_provision{.uri = "/api/v1/network/wifi",
                                   .method = HTTP_POST,
                                   .handler = wifiProvisionHandler,
                                   .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t wifi_scan_get{.uri = "/api/v1/network/wifi/scan",
                                  .method = HTTP_GET,
                                  .handler = wifiScanHandler,
                                  .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t wifi_scan_start{.uri = "/api/v1/network/wifi/scan",
                                    .method = HTTP_POST,
                                    .handler = wifiScanHandler,
                                    .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t wifi_scan_page{.uri = "/api/v1/network/wifi/scan/page",
                                   .method = HTTP_POST,
                                   .handler = wifiScanPageHandler,
                                   .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t ai_provider{.uri = "/api/v1/ai/provider",
                                .method = HTTP_POST,
                                .handler = aiProviderHandler,
                                .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t ai_memory{.uri = "/api/v1/ai/memory/section",
                              .method = HTTP_POST,
                              .handler = aiMemoryMutationHandler,
                              .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t ai_prompt{.uri = "/api/v1/ai/prompts/file",
                              .method = HTTP_POST,
                              .handler = aiPromptMutationHandler,
                              .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t ai_chat{.uri = "/api/v1/ai/chat",
                            .method = HTTP_POST,
                            .handler = aiChatHandler,
                            .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t ai_chat_status{.uri = "/api/v1/ai/chat",
                                   .method = HTTP_GET,
                                   .handler = aiChatHandler,
                                   .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t mcp_invoke{.uri = "/api/v1/ai/mcp/invoke",
                               .method = HTTP_POST,
                               .handler = mcpInvokeHandler,
                               .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t mcp_permission{.uri = "/api/v1/ai/mcp/tool",
                                   .method = HTTP_POST,
                                   .handler = mcpPermissionHandler,
                                   .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  const httpd_uri_t mcp_json_rpc{.uri = "/api/v1/ai/mcp/",
                                 .method = HTTP_POST,
                                 .handler = mcpJsonRpcHandler,
                                 .user_ctx = const_cast<HttpRuntime*>(&runtime)};
  static constexpr const char* mutation_paths[] = {
      "/api/v1/rooms/upsert",       "/api/v1/rooms/delete",       "/api/v1/groups/upsert",
      "/api/v1/groups/delete",      "/api/v1/scenes/upsert",      "/api/v1/scenes/delete",
      "/api/v1/automations/upsert", "/api/v1/automations/delete",
  };
  const httpd_uri_t unavailable{.uri = "/api/v1/*",
                                .method = HTTP_POST,
                                .handler = unavailableMutationHandler,
                                .user_ctx = nullptr};
  if (httpd_register_uri_handler(server, &dashboard) != ESP_OK ||
      httpd_register_uri_handler(server, &health) != ESP_OK ||
      httpd_register_uri_handler(server, &reboot) != ESP_OK ||
      httpd_register_uri_handler(server, &ota_prepare) != ESP_OK ||
      httpd_register_uri_handler(server, &ota_chunk) != ESP_OK ||
      httpd_register_uri_handler(server, &commission) != ESP_OK ||
      httpd_register_uri_handler(server, &thread_dataset) != ESP_OK ||
      httpd_register_uri_handler(server, &automation_run) != ESP_OK ||
      httpd_register_uri_handler(server, &on_off) != ESP_OK ||
      httpd_register_uri_handler(server, &registry_reset) != ESP_OK ||
      httpd_register_uri_handler(server, &models_reset) != ESP_OK ||
      httpd_register_uri_handler(server, &commission_cancel) != ESP_OK ||
      httpd_register_uri_handler(server, &time_config) != ESP_OK ||
      httpd_register_uri_handler(server, &smtp_config) != ESP_OK ||
      httpd_register_uri_handler(server, &smtp_test) != ESP_OK ||
      httpd_register_uri_handler(server, &activity_page) != ESP_OK ||
      httpd_register_uri_handler(server, &wifi_provision) != ESP_OK ||
      httpd_register_uri_handler(server, &wifi_scan_get) != ESP_OK ||
      httpd_register_uri_handler(server, &wifi_scan_start) != ESP_OK ||
      httpd_register_uri_handler(server, &wifi_scan_page) != ESP_OK ||
      httpd_register_uri_handler(server, &ai_provider) != ESP_OK ||
      httpd_register_uri_handler(server, &ai_memory) != ESP_OK ||
      httpd_register_uri_handler(server, &ai_prompt) != ESP_OK ||
      httpd_register_uri_handler(server, &ai_chat) != ESP_OK ||
      httpd_register_uri_handler(server, &ai_chat_status) != ESP_OK ||
      httpd_register_uri_handler(server, &mcp_invoke) != ESP_OK ||
      httpd_register_uri_handler(server, &mcp_permission) != ESP_OK ||
      httpd_register_uri_handler(server, &mcp_json_rpc) != ESP_OK) {
    httpd_stop(server);
    return nullptr;
  }
  for (const char* path : mutation_paths) {
    const httpd_uri_t route{.uri = path,
                            .method = HTTP_POST,
                            .handler = modelMutationHandler,
                            .user_ctx = const_cast<HttpRuntime*>(&runtime)};
    if (httpd_register_uri_handler(server, &route) != ESP_OK) {
      httpd_stop(server);
      return nullptr;
    }
  }

  if (httpd_register_uri_handler(server, &unavailable) != ESP_OK) {
    httpd_stop(server);
    return nullptr;
  }

  static constexpr const char* status_paths[] = {
      "/api/v1/system",    "/api/v1/network",     "/api/v1/storage",      "/api/v1/time",
      "/api/v1/smtp",      "/api/v1/devices",     "/api/v1/rooms",        "/api/v1/groups",
      "/api/v1/scenes",    "/api/v1/automations", "/api/v1/matter",       "/api/v1/ai",
      "/api/v1/ai/memory", "/api/v1/ai/prompts",  "/api/v1/ai/mcp/tools", "/api/v1/ai/mcp/context",
  };
  for (const char* path : status_paths) {
    const httpd_uri_t route{.uri = path,
                            .method = HTTP_GET,
                            .handler = statusHandler,
                            .user_ctx = const_cast<HttpRuntime*>(&runtime)};
    if (httpd_register_uri_handler(server, &route) != ESP_OK) {
      httpd_stop(server);
      return nullptr;
    }
  }
  return server;
}

} // namespace core_pilot
