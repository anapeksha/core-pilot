#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

struct cJSON;

namespace core_domain {
inline constexpr char product_name[] = "Core Pilot";
inline constexpr char repository_name[] = "core-pilot";
inline constexpr char crate_prefix[] = "core_pilot";
inline constexpr char local_hostname[] = "corepilot.local";
inline constexpr char sd_app_root[] = "/core-pilot/";
inline constexpr char setup_ap_prefix[] = "CorePilot-Setup";
inline constexpr size_t max_json_body = 16 * 1024;
inline constexpr size_t max_ai_body = 32 * 1024;
inline constexpr size_t max_ai_memory_body = 16 * 1024;
inline constexpr uint8_t max_ai_tool_rounds = 8;
inline constexpr uint8_t max_http_connections = 3;
inline constexpr size_t max_models = 32;
inline constexpr size_t max_id = 48;
inline constexpr size_t max_name = 64;
inline constexpr size_t max_detail = 96;
inline constexpr size_t max_actions = 8;
inline constexpr size_t max_members = 32;
inline constexpr size_t max_notification = 72;
inline constexpr uint32_t max_delay_ms = 60000;
inline constexpr uint32_t max_ota_image = 0x400000;
inline constexpr size_t max_ota_chunk = 2 * 1024;
inline constexpr size_t max_ota_chunk_base64 = 3 * 1024;

// Exact wire codes from firmware/src/api/common.rs; never use prose as status.
namespace error_code {
inline constexpr char STORAGE_UNAVAILABLE[] = "STORAGE_UNAVAILABLE";
inline constexpr char STORAGE_WRITE_FAILED[] = "STORAGE_WRITE_FAILED";
inline constexpr char WIFI_CONFIG_INVALID[] = "WIFI_CONFIG_INVALID";
inline constexpr char NTP_SERVER_UNSUPPORTED[] = "NTP_SERVER_UNSUPPORTED";
inline constexpr char TIMEZONE_UNSUPPORTED[] = "TIMEZONE_UNSUPPORTED";
inline constexpr char SMTP_CONFIG_REJECTED[] = "SMTP_CONFIG_REJECTED";
inline constexpr char SMTP_NOT_CONFIGURED[] = "SMTP_NOT_CONFIGURED";
inline constexpr char SMTP_CONFIG_INVALID[] = "SMTP_CONFIG_INVALID";
inline constexpr char SMTP_MESSAGE_INVALID[] = "SMTP_MESSAGE_INVALID";
inline constexpr char SMTP_TRANSPORT_UNAVAILABLE[] = "SMTP_TRANSPORT_UNAVAILABLE";
inline constexpr char MODEL_MUTATION_REJECTED[] = "MODEL_MUTATION_REJECTED";
inline constexpr char AUTOMATION_NOT_FOUND[] = "AUTOMATION_NOT_FOUND";
inline constexpr char AUTOMATION_NOT_MANUAL[] = "AUTOMATION_NOT_MANUAL";
inline constexpr char AUTOMATION_QUEUE_FULL[] = "AUTOMATION_QUEUE_FULL";
inline constexpr char MATTER_QUEUE_FULL[] = "MATTER_QUEUE_FULL";
inline constexpr char MATTER_REQUEST_INVALID[] = "MATTER_REQUEST_INVALID";
inline constexpr char MATTER_NOT_READY[] = "MATTER_NOT_READY";
inline constexpr char MATTER_OPERATION_IDS_EXHAUSTED[] = "MATTER_OPERATION_IDS_EXHAUSTED";
inline constexpr char MATTER_OPERATION_NOT_FOUND[] = "MATTER_OPERATION_NOT_FOUND";
inline constexpr char MATTER_OPERATION_TERMINAL[] = "MATTER_OPERATION_TERMINAL";
inline constexpr char MATTER_CANCELLATION_UNSAFE[] = "MATTER_CANCELLATION_UNSAFE";
inline constexpr char MATTER_COMMISSIONING_BOOT_BUSY[] = "MATTER_COMMISSIONING_BOOT_BUSY";
inline constexpr char MATTER_COMMISSIONING_PERSIST_FAILED[] = "MATTER_COMMISSIONING_PERSIST_FAILED";
inline constexpr char MATTER_COMMISSIONING_TIME_UNAVAILABLE[] =
    "MATTER_COMMISSIONING_TIME_UNAVAILABLE";
inline constexpr char MATTER_COMMISSIONING_RECOVERY_REQUIRED[] =
    "MATTER_COMMISSIONING_RECOVERY_REQUIRED";
inline constexpr char MATTER_COMMAND_QUEUE_FULL[] = "MATTER_COMMAND_QUEUE_FULL";
inline constexpr char MATTER_COMMAND_TARGET_INVALID[] = "MATTER_COMMAND_TARGET_INVALID";
inline constexpr char MATTER_COMMAND_TARGET_AMBIGUOUS[] = "MATTER_COMMAND_TARGET_AMBIGUOUS";
inline constexpr char MATTER_COMMAND_UNSUPPORTED[] = "MATTER_COMMAND_UNSUPPORTED";
inline constexpr char MATTER_COMMAND_NOT_READY[] = "MATTER_COMMAND_NOT_READY";
inline constexpr char THREAD_DATASET_INVALID[] = "THREAD_DATASET_INVALID";
inline constexpr char AI_REQUEST_INVALID[] = "AI_REQUEST_INVALID";
inline constexpr char AI_BUSY[] = "AI_BUSY";
inline constexpr char AI_PROVIDER_UNAVAILABLE[] = "AI_PROVIDER_UNAVAILABLE";
inline constexpr char AI_PROMPT_INVALID[] = "AI_PROMPT_INVALID";
inline constexpr char AI_MEMORY_TOO_LARGE[] = "AI_MEMORY_TOO_LARGE";
inline constexpr char AI_PROVIDER_CONFIG_INVALID[] = "AI_PROVIDER_CONFIG_INVALID";
inline constexpr char AI_TOOL_UNAVAILABLE[] = "AI_TOOL_UNAVAILABLE";
inline constexpr char AI_TOOL_REQUEST_INVALID[] = "AI_TOOL_REQUEST_INVALID";
inline constexpr char AI_TOOL_REJECTED[] = "AI_TOOL_REJECTED";
inline constexpr char OTA_METADATA_INVALID[] = "OTA_METADATA_INVALID";
inline constexpr char OTA_CHUNK_REJECTED[] = "OTA_CHUNK_REJECTED";
} // namespace error_code

enum class ApiError { ok, body_too_large, invalid_json, cross_origin_mutation };
enum class ModelError {
  ok,
  invalid_id,
  invalid_name,
  invalid_detail,
  too_many_items,
  too_many_members,
  too_many_actions,
  in_use,
  not_found
};
enum class OtaError {
  ok,
  missing_version,
  image_too_large,
  invalid_digest,
  digest_mismatch,
  write_failed,
  offset_mismatch,
  chunk_too_large,
  not_prepared
};

// Input is length-delimited. On failure, output is null; caller owns a
// successful cJSON tree.
ApiError parse_json(const char* bytes, size_t length, size_t max_length, cJSON*& output);
ApiError same_origin_mutation_allowed(const char* origin, const char* host);
// Prints into a caller-owned buffer (including the trailing NUL).
bool serialize_json(cJSON* value, char* output, size_t capacity);
// firmware/src/api/common.rs MutationResponse wire shape.
bool serialize_mutation_response(bool accepted, const char* status, const char* detail,
                                 char* output, size_t capacity);
struct ActivityPageRequest {
  bool has_before_sequence = false;
  uint32_t before_sequence = 0;
  bool has_limit = false;
  uint8_t limit = 0;
};
ApiError parse_activity_page_request(const cJSON* object, ActivityPageRequest& result);

// An RAII lease around a shared atomic counter; noncopyable to avoid double
// release.
class HttpConnectionLease {
public:
  explicit HttpConnectionLease(std::atomic<uint8_t>& active);
  ~HttpConnectionLease();
  HttpConnectionLease(const HttpConnectionLease&) = delete;
  HttpConnectionLease& operator=(const HttpConnectionLease&) = delete;
  bool acquired() const {
    return acquired_;
  }

private:
  std::atomic<uint8_t>& active_;
  bool acquired_ = false;
};

ModelError normalize_id(const char* input, char (&output)[max_id + 1]);
ModelError normalize_name(const char* input, char (&output)[max_name + 1]);
ModelError normalize_detail(const char* input, char (&output)[max_detail + 1]);

enum class ModelKind { room, group, scene, automation };
struct ModelUpsert {
  bool has_id = false;
  char id[max_id + 1]{};
  char name[max_name + 1]{};
  bool enabled = false;                        // automation only
  char trigger[max_detail + 1]{};              // automation only
  char details[max_members][max_detail + 1]{}; // device_ids or actions
  size_t detail_count = 0;
};
// Validates and normalizes the stable dashboard upsert DTO. Does not generate
// IDs or mutate storage.
ModelError parse_model_upsert(const cJSON* object, ModelKind kind, ModelUpsert& result);
bool serialize_model(const ModelUpsert& model, ModelKind kind, char* output, size_t capacity);
ModelError parse_model_delete(const cJSON* object, char (&id)[max_id + 1]);

struct TriggerSpec {
  enum class Kind { manual, once, weekly } kind = Kind::manual;
  uint16_t year = 0;
  uint8_t month = 0, day = 0, weekdays_mask = 0;
  uint16_t minute_of_day = 0;
  int16_t timezone_offset_minutes = 0;
};
struct ActionSpec {
  enum class Kind { on, level, scene, notify, delay } kind = Kind::on;
  char target[max_notification + 1]{};
  uint32_t value = 0;
};
bool parse_trigger_spec(const char* value, TriggerSpec& output);
bool parse_action_spec(const char* value, ActionSpec& output);
struct DateTime {
  uint16_t year;
  uint8_t month, day, hour, minute, weekday;
}; // weekday: Sunday=0
bool schedule_matches(const TriggerSpec& spec, DateTime local);

struct OtaPrepare {
  char version[65]{}; // Bounded printable firmware version.
  uint32_t size_bytes = 0;
  char sha256_hex[65]{};
  bool restart_after_update = false;
};
struct OtaChunk {
  uint32_t offset = 0;
  char data_base64[max_ota_chunk_base64 + 1]{};
  bool final_chunk = false;
};
OtaError validate_ota_prepare(const OtaPrepare& request);
OtaError parse_ota_prepare(const cJSON* object, OtaPrepare& result);
OtaError parse_ota_chunk(const cJSON* object, OtaChunk& result);
bool serialize_ota_prepare(const OtaPrepare& request, char* output, size_t capacity);
bool serialize_ota_chunk(const OtaChunk& chunk, char* output, size_t capacity);
// Validates base64 alphabet, padding, decoded byte count and upload ordering;
// no flash writes.
OtaError validate_ota_chunk(const OtaChunk& chunk, uint32_t written, uint32_t expected_size,
                            size_t& decoded_size);
} // namespace core_domain
