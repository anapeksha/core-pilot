#include "core_domain.hpp"

#include <cJSON.h>
#include <cmath>
#include <cstring>
#include <limits>

namespace core_domain {
namespace {
bool ascii_space(char c) {
  return c == ' ' || (c >= '\t' && c <= '\r');
}
bool digit(char c) {
  return c >= '0' && c <= '9';
}
bool hex(char c) {
  return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
bool id_char(char c, bool action) {
  return digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_' ||
         (action && c == '%');
}
template <size_t N> bool copy(const char* input, char (&output)[N]) {
  if (!input)
    return false;
  const size_t n = strnlen(input, N);
  if (n == N)
    return false;
  std::memcpy(output, input, n + 1);
  return true;
}
template <size_t N> bool trimmed(const char* input, char (&output)[N], bool empty_ok) {
  if (!input)
    return false;
  const size_t length = strnlen(input, max_json_body + 1);
  if (length > max_json_body)
    return false;
  size_t start = 0, end = length;
  while (start < end && ascii_space(input[start]))
    ++start;
  while (end > start && ascii_space(input[end - 1]))
    --end;
  if ((!empty_ok && start == end) || end - start >= N)
    return false;
  std::memcpy(output, input + start, end - start);
  output[end - start] = '\0';
  return true;
}
bool unsigned_number(const char* s, uint32_t max, uint32_t& out) {
  if (!s || !*s)
    return false;
  uint32_t value = 0;
  for (; *s; ++s) {
    if (!digit(*s) || value > (max - static_cast<uint32_t>(*s - '0')) / 10)
      return false;
    value = value * 10 + static_cast<uint32_t>(*s - '0');
  }
  out = value;
  return true;
}
bool json_u32(const cJSON* item, uint32_t& value) {
  if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble < 0 ||
      item->valuedouble > UINT32_MAX || std::floor(item->valuedouble) != item->valuedouble)
    return false;
  value = static_cast<uint32_t>(item->valuedouble);
  return true;
}
bool split(const char* text, char (&parts)[4][max_detail + 1], size_t& count) {
  if (!text || strnlen(text, max_detail + 2) > max_detail)
    return false;
  count = 0;
  const char* start = text;
  for (const char* p = text;; ++p) {
    if (*p != '|' && *p != '\0')
      continue;
    if (count == 4 || static_cast<size_t>(p - start) > max_detail)
      return false;
    std::memcpy(parts[count], start, p - start);
    parts[count++][p - start] = '\0';
    if (!*p)
      return true;
    start = p + 1;
  }
}
bool action_id(const char* id) {
  const size_t n = strnlen(id, max_id + 1);
  if (!n || n > max_id)
    return false;
  for (size_t i = 0; i < n; ++i)
    if (!id_char(id[i], true))
      return false;
  return true;
}
bool add_string(cJSON* object, const char* key, const char* value) {
  return cJSON_AddStringToObject(object, key, value) != nullptr;
}
} // namespace

ApiError parse_json(const char* bytes, size_t length, size_t max_length, cJSON*& output) {
  output = nullptr;
  if (length > max_length || length > max_ai_body)
    return ApiError::body_too_large;
  if (!bytes || length == 0)
    return ApiError::invalid_json;
  const char* end = nullptr;
  output = cJSON_ParseWithLengthOpts(bytes, length, &end, 0);
  if (!output)
    return ApiError::invalid_json;
  // cJSON permits trailing whitespace; serde_json does too, but not additional
  // JSON values.
  while (end < bytes + length && ascii_space(*end))
    ++end;
  if (end != bytes + length) {
    cJSON_Delete(output);
    output = nullptr;
    return ApiError::invalid_json;
  }
  return ApiError::ok;
}
ApiError same_origin_mutation_allowed(const char* origin, const char* host) {
  if (!origin)
    return ApiError::ok;
  if (!host || std::strncmp(origin, "http://", 7) != 0 || std::strcmp(origin + 7, host) != 0)
    return ApiError::cross_origin_mutation;
  return ApiError::ok;
}
bool serialize_json(cJSON* value, char* output, size_t capacity) {
  return value && output && capacity && capacity <= static_cast<size_t>(INT32_MAX) &&
         cJSON_PrintPreallocated(value, output, static_cast<int>(capacity), 0);
}
bool serialize_mutation_response(bool accepted, const char* status, const char* detail,
                                 char* output, size_t capacity) {
  if (!output || !capacity)
    return false;
  output[0] = '\0';
  if (!status || !detail || strnlen(status, max_json_body + 1) > max_json_body ||
      strnlen(detail, max_json_body + 1) > max_json_body)
    return false;
  cJSON* object = cJSON_CreateObject();
  if (!object)
    return false;
  bool ok = cJSON_AddBoolToObject(object, "accepted", accepted) &&
            add_string(object, "status", status) && add_string(object, "detail", detail) &&
            serialize_json(object, output, capacity);
  cJSON_Delete(object);
  if (!ok)
    output[0] = '\0';
  return ok;
}
ApiError parse_activity_page_request(const cJSON* object, ActivityPageRequest& result) {
  if (!cJSON_IsObject(object))
    return ApiError::invalid_json;
  ActivityPageRequest temp{};
  const cJSON* before = cJSON_GetObjectItemCaseSensitive(object, "before_sequence");
  const cJSON* limit = cJSON_GetObjectItemCaseSensitive(object, "limit");
  if (before && !cJSON_IsNull(before)) {
    if (!json_u32(before, temp.before_sequence))
      return ApiError::invalid_json;
    temp.has_before_sequence = true;
  }
  if (limit && !cJSON_IsNull(limit)) {
    uint32_t value = 0;
    if (!json_u32(limit, value) || value > UINT8_MAX)
      return ApiError::invalid_json;
    temp.has_limit = true;
    temp.limit = static_cast<uint8_t>(value);
  }
  result = temp;
  return ApiError::ok;
}
HttpConnectionLease::HttpConnectionLease(std::atomic<uint8_t>& active) : active_(active) {
  uint8_t current = active_.load(std::memory_order_acquire);
  while (current < max_http_connections) {
    if (active_.compare_exchange_weak(current, static_cast<uint8_t>(current + 1),
                                      std::memory_order_acq_rel, std::memory_order_acquire)) {
      acquired_ = true;
      break;
    }
  }
}
HttpConnectionLease::~HttpConnectionLease() {
  if (acquired_)
    active_.fetch_sub(1, std::memory_order_acq_rel);
}
ModelError normalize_id(const char* input, char (&output)[max_id + 1]) {
  char temp[max_id + 1]{};
  if (!trimmed(input, temp, false))
    return ModelError::invalid_id;
  for (const char* p = temp; *p; ++p)
    if (!id_char(*p, false))
      return ModelError::invalid_id;
  std::memcpy(output, temp, sizeof temp);
  return ModelError::ok;
}
ModelError normalize_name(const char* input, char (&output)[max_name + 1]) {
  char temp[max_name + 1]{};
  if (!trimmed(input, temp, false))
    return ModelError::invalid_name;
  std::memcpy(output, temp, sizeof temp);
  return ModelError::ok;
}
ModelError normalize_detail(const char* input, char (&output)[max_detail + 1]) {
  char temp[max_detail + 1]{};
  if (!trimmed(input, temp, true))
    return ModelError::invalid_detail;
  std::memcpy(output, temp, sizeof temp);
  return ModelError::ok;
}
ModelError parse_model_upsert(const cJSON* object, ModelKind kind, ModelUpsert& result) {
  if (!cJSON_IsObject(object))
    return ModelError::invalid_detail;
  ModelUpsert temp{};
  const cJSON* id = cJSON_GetObjectItemCaseSensitive(object, "id");
  if (id && !cJSON_IsNull(id)) {
    if (!cJSON_IsString(id) || normalize_id(id->valuestring, temp.id) != ModelError::ok)
      return ModelError::invalid_id;
    temp.has_id = true;
  }
  const cJSON* name = cJSON_GetObjectItemCaseSensitive(object, "name");
  if (!cJSON_IsString(name) || normalize_name(name->valuestring, temp.name) != ModelError::ok)
    return ModelError::invalid_name;
  if (kind == ModelKind::automation) {
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(object, "enabled");
    const cJSON* trigger = cJSON_GetObjectItemCaseSensitive(object, "trigger");
    TriggerSpec parsed{};
    if (!cJSON_IsBool(enabled) || !cJSON_IsString(trigger) ||
        normalize_detail(trigger->valuestring, temp.trigger) != ModelError::ok ||
        !parse_trigger_spec(temp.trigger, parsed))
      return ModelError::invalid_detail;
    temp.enabled = cJSON_IsTrue(enabled);
  }
  if (kind != ModelKind::room) {
    const bool group = kind == ModelKind::group;
    const cJSON* list = cJSON_GetObjectItemCaseSensitive(object, group ? "device_ids" : "actions");
    if (!cJSON_IsArray(list))
      return ModelError::invalid_detail;
    const int array_size = cJSON_GetArraySize(list);
    if (array_size < 0)
      return ModelError::invalid_detail;
    const auto count = static_cast<std::size_t>(array_size);
    const std::size_t limit = group ? max_members : max_actions;
    if (count > limit)
      return group ? ModelError::too_many_members : ModelError::too_many_actions;
    for (std::size_t i = 0; i < count; ++i) {
      const cJSON* item = cJSON_GetArrayItem(list, static_cast<int>(i));
      char detail[max_detail + 1]{};
      if (!cJSON_IsString(item) || normalize_detail(item->valuestring, detail) != ModelError::ok)
        return ModelError::invalid_detail;
      bool duplicate = false;
      if (group) {
        for (size_t j = 0; j < temp.detail_count; ++j)
          if (std::strcmp(detail, temp.details[j]) == 0)
            duplicate = true;
      }
      if (*detail && !duplicate)
        copy(detail, temp.details[temp.detail_count++]);
    }
    if (!group) {
      if (!temp.detail_count)
        return ModelError::invalid_detail;
      for (size_t i = 0; i < temp.detail_count; ++i) {
        ActionSpec action{};
        if (!parse_action_spec(temp.details[i], action))
          return ModelError::invalid_detail;
      }
    }
  }
  result = temp;
  return ModelError::ok;
}
ModelError parse_model_delete(const cJSON* object, char (&id)[max_id + 1]) {
  const cJSON* field = cJSON_GetObjectItemCaseSensitive(object, "id");
  return cJSON_IsObject(object) && cJSON_IsString(field) ? normalize_id(field->valuestring, id)
                                                         : ModelError::invalid_id;
}
bool serialize_model(const ModelUpsert& model, ModelKind kind, char* output, size_t capacity) {
  if (!output || !capacity)
    return false;
  output[0] = '\0';
  if (model.detail_count > (kind == ModelKind::group ? max_members : max_actions) ||
      (model.has_id && (strnlen(model.id, sizeof model.id) > max_id || !*model.id)) ||
      strnlen(model.name, sizeof model.name) > max_name || !*model.name ||
      (kind == ModelKind::automation && strnlen(model.trigger, sizeof model.trigger) > max_detail))
    return false;
  for (size_t i = 0; i < model.detail_count; ++i)
    if (strnlen(model.details[i], sizeof model.details[i]) > max_detail)
      return false;
  cJSON* object = cJSON_CreateObject();
  if (!object)
    return false;
  bool ok = (!model.has_id || add_string(object, "id", model.id)) &&
            add_string(object, "name", model.name);
  if (kind == ModelKind::automation) {
    ok = ok && cJSON_AddBoolToObject(object, "enabled", model.enabled) &&
         add_string(object, "trigger", model.trigger);
  }
  if (kind != ModelKind::room) {
    cJSON* array =
        cJSON_AddArrayToObject(object, kind == ModelKind::group ? "device_ids" : "actions");
    ok =
        ok && array && model.detail_count <= (kind == ModelKind::group ? max_members : max_actions);
    for (size_t i = 0; ok && i < model.detail_count; ++i) {
      cJSON* item = cJSON_CreateString(model.details[i]);
      if (!item) {
        ok = false;
        break;
      }
      if (!cJSON_AddItemToArray(array, item)) {
        cJSON_Delete(item);
        ok = false;
      }
    }
  }
  ok = ok && serialize_json(object, output, capacity);
  cJSON_Delete(object);
  if (!ok)
    output[0] = '\0';
  return ok;
}

bool parse_trigger_spec(const char* value, TriggerSpec& output) {
  char parts[4][max_detail + 1]{};
  size_t n = 0;
  if (!split(value, parts, n))
    return false;
  TriggerSpec temp{};
  if (n == 1 && std::strcmp(parts[0], "manual") == 0) {
    output = temp;
    return true;
  }
  if (n != 4)
    return false;
  if (std::strcmp(parts[0], "once") == 0) {
    const char* d = parts[1];
    if (std::strlen(d) != 10 || d[4] != '-' || d[7] != '-')
      return false;
    for (int i = 0; i < 10; ++i)
      if (i != 4 && i != 7 && !digit(d[i]))
        return false;
    uint32_t y, m, day;
    char year[] = {d[0], d[1], d[2], d[3], 0};
    char month[] = {d[5], d[6], 0};
    char date[] = {d[8], d[9], 0};
    if (!unsigned_number(year, 65535, y) || !unsigned_number(month, 12, m) ||
        !unsigned_number(date, 31, day) || !y || !m || !day)
      return false;
    const bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    const uint8_t days[] = {
        0, 31, static_cast<uint8_t>(leap ? 29 : 28), 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (day > days[m])
      return false;
    temp.kind = TriggerSpec::Kind::once;
    temp.year = y;
    temp.month = m;
    temp.day = day;
  } else if (std::strcmp(parts[0], "weekly") == 0) {
    uint32_t mask;
    if (!unsigned_number(parts[1], 127, mask) || !mask)
      return false;
    temp.kind = TriggerSpec::Kind::weekly;
    temp.weekdays_mask = mask;
  } else
    return false;
  const char* time = parts[2];
  const char* colon = std::strchr(time, ':');
  if (!colon || std::strchr(colon + 1, ':') || colon == time)
    return false;
  char hour[6]{};
  if (static_cast<size_t>(colon - time) >= sizeof hour)
    return false;
  std::memcpy(hour, time, colon - time);
  uint32_t h, minute;
  if (!unsigned_number(hour, 23, h) || !unsigned_number(colon + 1, 59, minute))
    return false;
  temp.minute_of_day = h * 60 + minute;
  const char* offset = parts[3];
  if (*offset != '-' && *offset != '+')
    return false;
  uint32_t magnitude;
  if (!unsigned_number(offset + 1, *offset == '-' ? 720 : 840, magnitude))
    return false;
  // magnitude is bounded to 840 above, so it is representable as int16_t.
  temp.timezone_offset_minutes = *offset == '-' ? -static_cast<int16_t>(magnitude) // NOLINT
                                                : static_cast<int16_t>(magnitude); // NOLINT
  output = temp;
  return true;
}
bool parse_action_spec(const char* value, ActionSpec& output) {
  char parts[4][max_detail + 1]{};
  size_t n = 0;
  if (!split(value, parts, n))
    return false;
  ActionSpec temp{};
  if (n == 3 && std::strcmp(parts[0], "on") == 0 && action_id(parts[1]) &&
      (std::strcmp(parts[2], "0") == 0 || std::strcmp(parts[2], "1") == 0)) {
    temp.kind = ActionSpec::Kind::on;
    temp.value = parts[2][0] - '0';
  } else if (n == 3 && std::strcmp(parts[0], "level") == 0 && action_id(parts[1]) &&
             unsigned_number(parts[2], 100, temp.value)) {
    temp.kind = ActionSpec::Kind::level;
  } else if (n == 2 && std::strcmp(parts[0], "scene") == 0 && action_id(parts[1])) {
    temp.kind = ActionSpec::Kind::scene;
  } else if (n == 2 && std::strcmp(parts[0], "notify") == 0 && *parts[1] &&
             std::strlen(parts[1]) <= max_notification) {
    temp.kind = ActionSpec::Kind::notify;
  } else if (n == 2 && std::strcmp(parts[0], "delay") == 0 &&
             unsigned_number(parts[1], max_delay_ms, temp.value)) {
    temp.kind = ActionSpec::Kind::delay;
  } else
    return false;
  if (temp.kind != ActionSpec::Kind::delay)
    copy(parts[1], temp.target);
  output = temp;
  return true;
}
bool schedule_matches(const TriggerSpec& spec, DateTime local) {
  if (local.hour > 23 || local.minute > 59 || local.weekday > 6)
    return false;
  if (spec.minute_of_day != local.hour * 60 + local.minute)
    return false;
  if (spec.kind == TriggerSpec::Kind::once)
    return spec.year == local.year && spec.month == local.month && spec.day == local.day;
  if (spec.kind == TriggerSpec::Kind::weekly)
    return (spec.weekdays_mask & (1u << ((local.weekday + 6) % 7))) != 0;
  return false;
}

OtaError validate_ota_prepare(const OtaPrepare& request) {
  bool nonspace = false;
  const size_t v = strnlen(request.version, sizeof request.version);
  if (v == sizeof request.version)
    return OtaError::missing_version;
  for (size_t i = 0; i < v; ++i)
    if (!ascii_space(request.version[i]))
      nonspace = true;
  if (!nonspace)
    return OtaError::missing_version;
  if (!request.size_bytes || request.size_bytes > max_ota_image)
    return OtaError::image_too_large;
  if (strnlen(request.sha256_hex, sizeof request.sha256_hex) != 64)
    return OtaError::invalid_digest;
  for (char c : request.sha256_hex)
    if (c && !hex(c))
      return OtaError::invalid_digest;
  return OtaError::ok;
}
OtaError parse_ota_prepare(const cJSON* object, OtaPrepare& result) {
  if (!cJSON_IsObject(object))
    return OtaError::missing_version;
  const cJSON* version = cJSON_GetObjectItemCaseSensitive(object, "version");
  const cJSON* size = cJSON_GetObjectItemCaseSensitive(object, "size_bytes");
  const cJSON* digest = cJSON_GetObjectItemCaseSensitive(object, "sha256_hex");
  const cJSON* restart = cJSON_GetObjectItemCaseSensitive(object, "restart_after_update");
  OtaPrepare temp{};
  if (!cJSON_IsString(version) || !copy(version->valuestring, temp.version))
    return OtaError::missing_version;
  if (!json_u32(size, temp.size_bytes))
    return OtaError::image_too_large;
  if (!cJSON_IsString(digest) || !copy(digest->valuestring, temp.sha256_hex))
    return OtaError::invalid_digest;
  if (restart && !cJSON_IsBool(restart))
    return OtaError::not_prepared;
  temp.restart_after_update = cJSON_IsTrue(restart);
  const OtaError error = validate_ota_prepare(temp);
  if (error == OtaError::ok)
    result = temp;
  return error;
}
OtaError parse_ota_chunk(const cJSON* object, OtaChunk& result) {
  if (!cJSON_IsObject(object))
    return OtaError::not_prepared;
  const cJSON* offset = cJSON_GetObjectItemCaseSensitive(object, "offset");
  const cJSON* data = cJSON_GetObjectItemCaseSensitive(object, "data_base64");
  const cJSON* final = cJSON_GetObjectItemCaseSensitive(object, "final_chunk");
  OtaChunk temp{};
  if (!json_u32(offset, temp.offset))
    return OtaError::offset_mismatch;
  if (!cJSON_IsString(data) || !copy(data->valuestring, temp.data_base64))
    return OtaError::chunk_too_large;
  if (!cJSON_IsBool(final))
    return OtaError::not_prepared;
  temp.final_chunk = cJSON_IsTrue(final);
  result = temp;
  return OtaError::ok;
}
bool serialize_ota_prepare(const OtaPrepare& request, char* output, size_t capacity) {
  if (!output || !capacity)
    return false;
  output[0] = '\0';
  if (validate_ota_prepare(request) != OtaError::ok)
    return false;
  cJSON* object = cJSON_CreateObject();
  if (!object)
    return false;
  bool ok = add_string(object, "version", request.version) &&
            cJSON_AddNumberToObject(object, "size_bytes", request.size_bytes) &&
            add_string(object, "sha256_hex", request.sha256_hex) &&
            cJSON_AddBoolToObject(object, "restart_after_update", request.restart_after_update) &&
            serialize_json(object, output, capacity);
  cJSON_Delete(object);
  if (!ok)
    output[0] = '\0';
  return ok;
}
bool serialize_ota_chunk(const OtaChunk& chunk, char* output, size_t capacity) {
  if (!output || !capacity)
    return false;
  output[0] = '\0';
  OtaChunk canonical = chunk;
  canonical.offset = 0;
  canonical.final_chunk = false;
  size_t decoded = 0;
  if (validate_ota_chunk(canonical, 0, max_ota_chunk, decoded) != OtaError::ok)
    return false;
  cJSON* object = cJSON_CreateObject();
  if (!object)
    return false;
  bool ok = cJSON_AddNumberToObject(object, "offset", chunk.offset) &&
            add_string(object, "data_base64", chunk.data_base64) &&
            cJSON_AddBoolToObject(object, "final_chunk", chunk.final_chunk) &&
            serialize_json(object, output, capacity);
  cJSON_Delete(object);
  if (!ok)
    output[0] = '\0';
  return ok;
}
OtaError validate_ota_chunk(const OtaChunk& chunk, uint32_t written, uint32_t expected_size,
                            size_t& decoded_size) {
  decoded_size = 0;
  const size_t n = strnlen(chunk.data_base64, sizeof chunk.data_base64);
  if (n == sizeof chunk.data_base64 || n > max_ota_chunk_base64 || n % 4)
    return OtaError::chunk_too_large;
  if (chunk.offset != written)
    return OtaError::offset_mismatch;
  size_t pad = n && chunk.data_base64[n - 1] == '=' ? 1 : 0;
  if (n > 1 && chunk.data_base64[n - 2] == '=')
    ++pad;
  for (size_t i = 0; i < n - pad; ++i) {
    const char c = chunk.data_base64[i];
    if (!(digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '+' || c == '/'))
      return OtaError::chunk_too_large;
  }
  for (size_t i = n - pad; i < n; ++i)
    if (chunk.data_base64[i] != '=')
      return OtaError::chunk_too_large;
  // Canonical padding: unused low bits in the last sextet must be zero.
  if (pad) {
    const char c = chunk.data_base64[n - pad - 1];
    const int sextet = digit(c)               ? c - '0' + 52
                       : c >= 'a' && c <= 'z' ? c - 'a' + 26
                       : c >= 'A' && c <= 'Z' ? c - 'A'
                       : c == '+'             ? 62
                                              : 63;
    if (sextet & (pad == 2 ? 15 : 3))
      return OtaError::chunk_too_large;
  }
  const size_t length = n / 4 * 3 - pad;
  if (length > max_ota_chunk)
    return OtaError::chunk_too_large;
  if (written > expected_size || length > expected_size - written)
    return OtaError::image_too_large;
  if (chunk.final_chunk && length != expected_size - written)
    return OtaError::image_too_large;
  decoded_size = length;
  return OtaError::ok;
}
} // namespace core_domain
