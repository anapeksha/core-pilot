#include "domain_contract.hpp"

#include <cJSON.h>
#include <cmath>
#include <cstring>

namespace core_domain {
namespace {
// Types: s string, b bool, u u32, h u16, t u8, i i16, q u64
// (limited to exact JSON integers), f finite number, o object, a array,
// v arbitrary bounded JSON value. '?' accepts missing/null; '*' accepts
// missing (serde(default)) but not null.
constexpr RouteContract routes[] = {
    {"GET", "/api/v1/health", nullptr, "product:s hostname:s storage_ok:b ai_enabled:b auth:s"},
    {"GET", "/api/v1/system", nullptr,
     "product:s hostname:s firmware_version:s setup_ap:s display:s storage:s "
     "matter:s thread:s ai:s ota:s ota_phase:s ota_progress_bytes:u "
     "ota_total_bytes:u ota_restart_after_update:b recovery:s "
     "http_accepted_connections:u http_active_connections:t "
     "http_connection_errors:u dashboard_transfers:u "
     "dashboard_last_transfer_ms:u dashboard_max_transfer_ms:u"},
    {"POST", "/api/v1/system/reboot", nullptr, "accepted:b status:s detail:s"},
    {"GET", "/api/v1/smtp", nullptr,
     "enabled:b configured:b host:s port:h security:s username_configured:b "
     "password_configured:b recipients:u transport_ready:b detail:s"},
    {"POST", "/api/v1/smtp", "config:o", "accepted:b status:s detail:s"},
    {"POST", "/api/v1/smtp/test", "message:o", "accepted:b status:s detail:s"},
    {"GET", "/api/v1/network", nullptr,
     "mode:s setup_ssid:s station_ssid:s? station_bssid:s? ipv4:s "
     "station_ipv4:s? hostname:s dhcp:s http:s wan_enabled:b "
     "cloud_relay_enabled:b"},
    {"GET", "/api/v1/network/wifi/scan", nullptr, "networks:a scanning:b scan_state:s detail:s"},
    {"POST", "/api/v1/network/wifi/scan", nullptr, "networks:a scanning:b scan_state:s detail:s"},
    {"POST", "/api/v1/network/wifi/scan/page", "last_seen:s?",
     "networks:a scanning:b scan_state:s next_last_seen:s? detail:s"},
    {"POST", "/api/v1/network/wifi", "credentials:o hostname:s?", "accepted:b status:s detail:s"},
    {"GET", "/api/v1/network/wifi/saved", nullptr, "networks:a detail:s"},
    {"POST", "/api/v1/network/wifi/forget", "ssid:s", "accepted:b status:s detail:s"},
    {"GET", "/api/v1/time", nullptr,
     "ntp_server_id:s ntp_server_host:s timezone_id:s timezone_label:s "
     "timezone_offset_minutes:i timezone_offset_label:s dns:s ntp:s rtc:s "
     "last_sync_unix_seconds:u? current_unix_seconds:u? current_date:s? "
     "current_time:s? detail:s"},
    {"POST", "/api/v1/time", "ntp_server_id:s timezone_id:s timezone_offset_minutes:i",
     "accepted:b status:s detail:s"},
    {"GET", "/api/v1/devices", nullptr, "devices:a detail:s"},
    {"GET", "/api/v1/rooms", nullptr, "rooms:a detail:s"},
    {"POST", "/api/v1/rooms/upsert", "id:s? name:s", "accepted:b status:s detail:s room:o?"},
    {"POST", "/api/v1/rooms/delete", "id:s", "accepted:b status:s detail:s"},
    {"GET", "/api/v1/groups", nullptr, "groups:a detail:s"},
    {"POST", "/api/v1/groups/upsert", "id:s? name:s device_ids:a",
     "accepted:b status:s detail:s group:o?"},
    {"POST", "/api/v1/groups/delete", "id:s", "accepted:b status:s detail:s"},
    {"GET", "/api/v1/scenes", nullptr, "scenes:a detail:s"},
    {"POST", "/api/v1/scenes/upsert", "id:s? name:s actions:a",
     "accepted:b status:s detail:s scene:o?"},
    {"POST", "/api/v1/scenes/delete", "id:s", "accepted:b status:s detail:s"},
    {"GET", "/api/v1/automations", nullptr, "automations:a detail:s"},
    {"POST", "/api/v1/automations/upsert", "id:s? name:s enabled:b trigger:s actions:a",
     "accepted:b status:s detail:s automation:o?"},
    {"POST", "/api/v1/automations/delete", "id:s", "accepted:b status:s detail:s"},
    {"POST", "/api/v1/automations/run", "id:s", "accepted:b status:s detail:s"},
    {"GET", "/api/v1/matter", nullptr,
     "implementation:s commissioned:b fabric_configured:b "
     "subscriptions_active:h ipv6_ready:b mdns_ready:b dns_sd_ready:b "
     "thread_ready:b thread_dataset_configured:b wifi_commissioning_ready:b "
     "thread_commissioning_ready:b adapter_status:s parser_ready:b "
     "transport_adapter_ready:b fabric_credentials_ready:b pase_ready:b "
     "case_ready:b subscriptions_ready:b operation:o detail:s"},
    {"POST", "/api/v1/matter/commission",
     "transport:s setup_payload:s discriminator:h? thread_dataset_id:s?",
     "accepted:b operation_id:q? transport:s status:s detail:s"},
    {"POST", "/api/v1/matter/commission/cancel", "operation_id:q", "accepted:b status:s detail:s"},
    {"POST", "/api/v1/matter/on-off", "device_id:s endpoint_id:h? on:b",
     "accepted:b status:s detail:s"},
    {"POST", "/api/v1/matter/devices/remove", "device_id:s",
     "accepted:b operation_id:q? status:s detail:s"},
    {"POST", "/api/v1/matter/thread/dataset", "active_dataset_tlvs_hex:s",
     "accepted:b status:s detail:s"},
    {"GET", "/api/v1/ai", nullptr,
     "enabled:b provider_configured:b mcp_enabled:b max_tool_rounds:t "
     "max_repeated_tool_calls:t max_actions:t destructive_confirmation:s "
     "persistence:s provider_transport:v provider_transport_requirements:a"},
    {"POST", "/api/v1/ai/provider", "provider:o", "accepted:b status:s detail:s"},
    {"GET", "/api/v1/ai/chat", nullptr, "accepted:b status:s reply:s actions:a"},
    {"POST", "/api/v1/ai/chat", "message:s history:a chat_id:s? allow_actions:b",
     "accepted:b status:s reply:s actions:a"},
    {"GET", "/api/v1/ai/memory", nullptr, "sections:a encrypted:b detail:s"},
    {"POST", "/api/v1/ai/memory/section", "id:s markdown:s", "accepted:b status:s detail:s"},
    {"GET", "/api/v1/ai/prompts", nullptr, "files:a encrypted:b detail:s"},
    {"POST", "/api/v1/ai/prompts/file", "path:s markdown:s* markdown_base64:s?",
     "accepted:b status:s detail:s"},
    {"POST", "/api/v1/ai/mcp/", "jsonrpc:s id:u? method:s params:o*",
     "jsonrpc:s id:v result:v? error:o?"},
    {"GET", "/api/v1/ai/mcp/tools", nullptr, "tools:a"},
    {"GET", "/api/v1/ai/mcp/context", nullptr, "system_prompt:s context_json:s tools:a"},
    {"POST", "/api/v1/ai/mcp/tool", "tool:s enabled:b", "accepted:b status:s detail:s"},
    {"POST", "/api/v1/ai/mcp/invoke", "tool:s arguments_json:s", "accepted:b status:s detail:s"},
    {"POST", "/api/v1/ota/prepare", "version:s size_bytes:u sha256_hex:s restart_after_update:b",
     "accepted:b status:s detail:s"},
    {"POST", "/api/v1/ota/chunk", "offset:u data_base64:s final_chunk:b",
     "accepted:b status:s detail:s"},
    {"GET", "/api/v1/storage", nullptr,
     "state:s sd_root:s encrypted:b mutations_allowed:b "
     "device_seed_exportable:b detail:s"},
    {"POST", "/api/v1/storage/models/reset", "confirmation:s", "accepted:b status:s detail:s"},
    {"POST", "/api/v1/activity/page", "before_sequence:u? limit:t?",
     "events:a next_before_sequence:u? retained:h dropped_events:u detail:s"},
    {"GET", "/api/v1/diagnostics", nullptr,
     "storage:o network:o time:o matter:o system:o activity:o"},
};

struct Nested {
  const char* key;
  const char* shape;
  size_t limit;
  char element;
};
constexpr Nested nested[] = {
    {"credentials", "ssid:s password:s", 0, 0},
    {"config",
     "enabled:b host:s port:h security:s username:s password:s? from:s "
     "recipients:a",
     0, 0},
    {"message", "subject:s body:s recipients:a", 0, 0},
    {"provider",
     "backend:s* auth_type:s* base_url:s model:s max_output_tokens:u "
     "timeout_ms:u temperature:f? api_key:s? api_key_redacted:b*",
     0, 0},
    {"params", "protocolVersion:s? name:s? arguments:o*", 0, 0},
    {"arguments", "device_id:s? device:s? room:s? id:s? name:s? trigger:s? actions:a*", 0, 0},
    {"history", "role:s content:s", 16, 'o'},
    {"recipients", nullptr, 4, 's'},
    {"device_ids", nullptr, max_members, 's'},
    {"actions", nullptr, max_actions, 'v'},
    {"networks", "id:s ssid:s bssid:s connected:b signal_dbm:i channel:t secured:b auth:s", 128,
     'o'},
    {"events", "sequence:u unix_seconds:u? uptime_seconds:u kind:s detail:s", 128, 'o'},
    {"sections", "id:s title:s markdown:s", 32, 'o'},
    {"files", "path:s title:s markdown:s baseline:b", 8, 'o'},
    {"tools",
     "id:s name:s title:s category:s description:s enabled:b destructive:b "
     "available:b",
     64, 'o'},
    {"rooms", nullptr, max_models, 'o'},
    {"groups", nullptr, max_models, 'o'},
    {"scenes", nullptr, max_models, 'o'},
    {"automations", nullptr, max_models, 'o'},
    {"devices", nullptr, 128, 'o'},
    {"provider_transport_requirements", nullptr, 32, 's'},
    {"storage",
     "state:s sd_root:s encrypted:b mutations_allowed:b "
     "device_seed_exportable:b detail:s",
     0, 0},
    {"network",
     "mode:s setup_ssid:s station_ssid:s? station_bssid:s? ipv4:s "
     "station_ipv4:s? hostname:s dhcp:s http:s wan_enabled:b "
     "cloud_relay_enabled:b",
     0, 0},
    {"activity",
     "retained:h dropped_events:u last_sequence:u? writer_dropped_events:u "
     "writer_failures:u detail:s",
     0, 0},
};

const Nested* nested_for(const char* key) {
  for (const auto& entry : nested)
    if (std::strcmp(key, entry.key) == 0)
      return &entry;
  return nullptr;
}

bool bounded_value(const cJSON* value, unsigned depth) {
  if (!value || depth > 10)
    return false;
  if (cJSON_IsString(value))
    return value->valuestring && std::strlen(value->valuestring) <= max_ai_body;
  if (cJSON_IsNumber(value))
    return std::isfinite(value->valuedouble);
  if (cJSON_IsBool(value) || cJSON_IsNull(value))
    return true;
  if (!cJSON_IsObject(value) && !cJSON_IsArray(value))
    return false;
  size_t count = 0;
  for (const cJSON* child = value->child; child; child = child->next) {
    if (++count > 128 ||
        (cJSON_IsObject(value) && (!child->string || std::strlen(child->string) > 96)) ||
        !bounded_value(child, depth + 1))
      return false;
  }
  return true;
}

bool shape_valid(const cJSON* object, const char* shape, unsigned depth);
bool field_valid(const cJSON* value, char type, const char* key, unsigned depth) {
  if (!value || depth > 10)
    return false;
  const double n = value->valuedouble;
  switch (type) {
  case 's':
    return cJSON_IsString(value) && value->valuestring &&
           std::strlen(value->valuestring) <= max_ai_body;
  case 'b':
    return cJSON_IsBool(value);
  case 'u':
  case 'h':
  case 't':
  case 'q':
  case 'i':
    if (!cJSON_IsNumber(value) || !std::isfinite(n) || std::floor(n) != n)
      return false;
    if (type == 'q')
      return n >= 0 && n <= 9007199254740991.0; // cJSON uses double
    if (type == 'i')
      return n >= -32768 && n <= 32767;
    return n >= 0 && n <= (type == 't' ? 255.0 : type == 'h' ? 65535.0 : 4294967295.0);
  case 'f':
    return cJSON_IsNumber(value) && std::isfinite(n);
  case 'v':
    return bounded_value(value, depth);
  case 'o': {
    if (!cJSON_IsObject(value))
      return false;
    const Nested* entry = nested_for(key);
    return entry && entry->shape ? shape_valid(value, entry->shape, depth + 1)
                                 : bounded_value(value, depth + 1);
  }
  case 'a': {
    if (!cJSON_IsArray(value))
      return false;
    const Nested* entry = nested_for(key);
    const size_t limit = entry && entry->limit ? entry->limit : 128;
    size_t count = 0;
    for (const cJSON* item = value->child; item; item = item->next) {
      if (++count > limit ||
          !(entry && entry->element ? field_valid(item, entry->element, "", depth + 1)
                                    : bounded_value(item, depth + 1)) ||
          (entry && entry->shape && !shape_valid(item, entry->shape, depth + 1)))
        return false;
    }
    return true;
  }
  default:
    return false;
  }
}

bool shape_valid(const cJSON* object, const char* shape, unsigned depth) {
  if (!cJSON_IsObject(object) || !shape || depth > 10)
    return false;
  const char* cursor = shape;
  while (*cursor) {
    while (*cursor == ' ')
      ++cursor;
    if (!*cursor)
      break;
    char key[64]{};
    size_t length = 0;
    while (*cursor && *cursor != ':' && length < sizeof(key) - 1)
      key[length++] = *cursor++;
    if (*cursor != ':')
      return false;
    ++cursor;
    if (!*cursor)
      return false;
    const char type = *cursor++;
    const char optional = (*cursor == '?' || *cursor == '*') ? *cursor++ : 0;
    if (*cursor && *cursor != ' ')
      return false;
    const cJSON* field = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!field || cJSON_IsNull(field)) {
      if ((!field && optional) || (field && optional == '?'))
        continue;
      return false;
    }
    if (!field_valid(field, type, key, depth))
      return false;
  }
  return true;
}

bool bounded_string(const cJSON* object, const char* key, size_t max, size_t min = 0) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsString(item) && item->valuestring && std::strlen(item->valuestring) >= min &&
         std::strlen(item->valuestring) <= max;
}
bool request_bounds(const RouteContract& route, const cJSON* object) {
  const char* p = route.path;
  if (std::strcmp(p, "/api/v1/network/wifi") == 0) {
    const cJSON* c = cJSON_GetObjectItemCaseSensitive(object, "credentials");
    const cJSON* password = cJSON_GetObjectItemCaseSensitive(c, "password");
    const size_t len = std::strlen(password->valuestring);
    return bounded_string(c, "ssid", 32, 1) && (len == 0 || (len >= 8 && len <= 63));
  }
  if (std::strcmp(p, "/api/v1/ai/chat") == 0)
    return bounded_string(object, "message", 4096, 1);
  if (std::strcmp(p, "/api/v1/ai/memory/section") == 0)
    return bounded_string(object, "id", 48, 1) && bounded_string(object, "markdown", 12 * 1024);
  if (std::strcmp(p, "/api/v1/ai/prompts/file") == 0) {
    const cJSON* encoded = cJSON_GetObjectItemCaseSensitive(object, "markdown_base64");
    const cJSON* markdown = cJSON_GetObjectItemCaseSensitive(object, "markdown");
    return bounded_string(object, "path", 128, 1) &&
           (!markdown || std::strlen(markdown->valuestring) <= 4096) &&
           (!encoded || cJSON_IsNull(encoded) || std::strlen(encoded->valuestring) <= 8192);
  }
  if (std::strcmp(p, "/api/v1/ai/provider") == 0) {
    const cJSON* config = cJSON_GetObjectItemCaseSensitive(object, "provider");
    const cJSON* tokens = cJSON_GetObjectItemCaseSensitive(config, "max_output_tokens");
    const cJSON* timeout = cJSON_GetObjectItemCaseSensitive(config, "timeout_ms");
    const cJSON* key = cJSON_GetObjectItemCaseSensitive(config, "api_key");
    return bounded_string(config, "base_url", 2048, 1) && bounded_string(config, "model", 128, 1) &&
           tokens->valuedouble >= 1 && tokens->valuedouble <= 8192 &&
           timeout->valuedouble >= 1000 && timeout->valuedouble <= 120000 &&
           (!key || cJSON_IsNull(key) || std::strlen(key->valuestring) <= 256);
  }
  if (std::strcmp(p, "/api/v1/smtp") == 0 || std::strcmp(p, "/api/v1/smtp/test") == 0) {
    const cJSON* data = cJSON_GetObjectItemCaseSensitive(
        object, std::strcmp(p, "/api/v1/smtp") == 0 ? "config" : "message");
    if (std::strcmp(p, "/api/v1/smtp") == 0)
      return bounded_string(data, "host", 253) && bounded_string(data, "username", 128) &&
             (!cJSON_GetObjectItemCaseSensitive(data, "password") ||
              cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(data, "password")) ||
              bounded_string(data, "password", 256)) &&
             bounded_string(data, "from", 254);
    return bounded_string(data, "subject", 160, 1) && bounded_string(data, "body", 4096, 1);
  }
  if (std::strcmp(p, "/api/v1/matter/commission") == 0) {
    const cJSON* transport = cJSON_GetObjectItemCaseSensitive(object, "transport");
    return cJSON_IsString(transport) && (std::strcmp(transport->valuestring, "wifi") == 0 ||
                                         std::strcmp(transport->valuestring, "thread") == 0);
  }
  if (std::strcmp(p, "/api/v1/matter/thread/dataset") == 0) {
    const cJSON* dataset = cJSON_GetObjectItemCaseSensitive(object, "active_dataset_tlvs_hex");
    if (!cJSON_IsString(dataset))
      return false;
    const char* text = dataset->valuestring;
    const size_t length = std::strlen(text);
    if (!length || length > 508 || length % 2)
      return false;
    for (size_t i = 0; i < length; ++i)
      if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f') ||
            (text[i] >= 'A' && text[i] <= 'F')))
        return false;
    return true;
  }
  if (std::strcmp(p, "/api/v1/ota/prepare") == 0) {
    OtaPrepare dto{};
    return parse_ota_prepare(object, dto) == OtaError::ok;
  }
  if (std::strcmp(p, "/api/v1/ota/chunk") == 0) {
    OtaChunk dto{};
    return parse_ota_chunk(object, dto) == OtaError::ok;
  }
  if (std::strstr(p, "/upsert")) {
    ModelUpsert dto{};
    const ModelKind kind = std::strstr(p, "/rooms/")    ? ModelKind::room
                           : std::strstr(p, "/groups/") ? ModelKind::group
                           : std::strstr(p, "/scenes/") ? ModelKind::scene
                                                        : ModelKind::automation;
    return parse_model_upsert(object, kind, dto) == ModelError::ok;
  }
  if (std::strstr(p, "/delete") || std::strcmp(p, "/api/v1/automations/run") == 0) {
    char id[max_id + 1]{};
    return parse_model_delete(object, id) == ModelError::ok;
  }
  return true;
}
} // namespace

size_t contract_count() {
  return sizeof(routes) / sizeof(routes[0]);
}
const RouteContract* contract_at(size_t index) {
  return index < contract_count() ? &routes[index] : nullptr;
}
const RouteContract* find_contract(const char* method, const char* path) {
  if (!method || !path)
    return nullptr;
  for (const auto& route : routes)
    if (std::strcmp(route.method, method) == 0 && std::strcmp(route.path, path) == 0)
      return &route;
  return nullptr;
}
ApiError parse_contract_request(const RouteContract& route, const char* bytes, size_t length,
                                cJSON*& output) {
  output = nullptr;
  if (!route.request_shape)
    return length ? ApiError::invalid_json : ApiError::ok;
  const size_t limit = std::strcmp(route.path, "/api/v1/ai/chat") == 0 ? max_ai_body
                       : std::strcmp(route.path, "/api/v1/ai/memory/section") == 0
                           ? max_ai_memory_body
                           : max_json_body;
  const ApiError error = parse_json(bytes, length, limit, output);
  if (error != ApiError::ok)
    return error;
  if (shape_valid(output, route.request_shape, 0) && request_bounds(route, output))
    return ApiError::ok;
  cJSON_Delete(output);
  output = nullptr;
  return ApiError::invalid_json;
}
bool serialize_contract_response(const RouteContract& route, const cJSON* value, char* output,
                                 size_t capacity) {
  if (!output || !capacity)
    return false;
  output[0] = '\0';
  if (!route.response_shape || !shape_valid(value, route.response_shape, 0))
    return false;
  return serialize_json(const_cast<cJSON*>(value), output, capacity);
}
} // namespace core_domain
