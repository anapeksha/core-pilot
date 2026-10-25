#include "core_ota.hpp"

#include "esp_app_desc.h"
#include "esp_timer.h"

#include <cstring>

namespace core_pilot {
namespace {

class Lock final {
public:
  explicit Lock(SemaphoreHandle_t mutex)
      : mutex_(mutex), held_(mutex != nullptr && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {}
  ~Lock() {
    if (held_)
      xSemaphoreGive(mutex_);
  }
  bool held() const {
    return held_;
  }

private:
  SemaphoreHandle_t mutex_;
  bool held_;
};

std::uint8_t hex_value(char value) {
  if (value >= '0' && value <= '9')
    return static_cast<std::uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<std::uint8_t>(value - 'a' + 10);
  if (value >= 'A' && value <= 'F')
    return static_cast<std::uint8_t>(value - 'A' + 10);
  return 0xff;
}

struct Identifier final {
  const char* data{};
  std::size_t size{};
};

struct Version final {
  Identifier core[3]{};
  Identifier prerelease{};
};

bool numeric(Identifier id) {
  for (std::size_t i = 0; i < id.size; ++i) {
    if (id.data[i] < '0' || id.data[i] > '9')
      return false;
  }
  return id.size != 0;
}

bool parse_version(const char* text, Version& version) {
  if (text == nullptr)
    return false;
  const char* cursor = text;
  for (auto& part : version.core) {
    const char* start = cursor;
    while (*cursor >= '0' && *cursor <= '9')
      ++cursor;
    part = {start, static_cast<std::size_t>(cursor - start)};
    if (part.size == 0 || (part.size > 1 && *start == '0'))
      return false;
    if (&part != &version.core[2]) {
      if (*cursor++ != '.')
        return false;
    }
  }
  if (*cursor == '-') {
    const char* start = ++cursor;
    const char* identifier = cursor;
    while (*cursor != 0 && *cursor != '+') {
      const char c = *cursor;
      if (c == '.') {
        if (cursor == identifier ||
            (numeric({identifier, static_cast<std::size_t>(cursor - identifier)}) &&
             cursor - identifier > 1 && *identifier == '0'))
          return false;
        identifier = cursor + 1;
      } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   c == '-')) {
        return false;
      }
      ++cursor;
    }
    if (cursor == identifier ||
        (numeric({identifier, static_cast<std::size_t>(cursor - identifier)}) &&
         cursor - identifier > 1 && *identifier == '0'))
      return false;
    version.prerelease = {start, static_cast<std::size_t>(cursor - start)};
  }
  if (*cursor == '+') {
    ++cursor;
    const char* identifier = cursor;
    while (*cursor != 0) {
      const char c = *cursor;
      if (c == '.') {
        if (cursor == identifier)
          return false;
        identifier = cursor + 1;
      } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   c == '-')) {
        return false;
      }
      ++cursor;
    }
    if (cursor == identifier)
      return false;
  }
  return *cursor == 0;
}

int compare_identifier(Identifier a, Identifier b, bool prerelease = false) {
  const bool a_numeric = numeric(a);
  const bool b_numeric = numeric(b);
  if (prerelease && a_numeric != b_numeric)
    return a_numeric ? -1 : 1;
  if (!prerelease || (a_numeric && b_numeric)) {
    if (a.size != b.size)
      return a.size < b.size ? -1 : 1;
  }
  const std::size_t length = a.size < b.size ? a.size : b.size;
  const int comparison = std::memcmp(a.data, b.data, length);
  if (comparison != 0)
    return comparison < 0 ? -1 : 1;
  if (a.size != b.size)
    return a.size < b.size ? -1 : 1;
  return 0;
}

int compare_versions(const Version& candidate, const Version& running) {
  for (std::size_t i = 0; i < 3; ++i) {
    const int result = compare_identifier(candidate.core[i], running.core[i]);
    if (result != 0)
      return result;
  }
  if (candidate.prerelease.size == 0 || running.prerelease.size == 0) {
    if (candidate.prerelease.size == running.prerelease.size)
      return 0;
    return candidate.prerelease.size == 0 ? 1 : -1;
  }
  const char* a = candidate.prerelease.data;
  const char* b = running.prerelease.data;
  const char* a_end = a + candidate.prerelease.size;
  const char* b_end = b + running.prerelease.size;
  while (a < a_end && b < b_end) {
    const char* a_next = a;
    const char* b_next = b;
    while (a_next < a_end && *a_next != '.')
      ++a_next;
    while (b_next < b_end && *b_next != '.')
      ++b_next;
    const int result = compare_identifier({a, static_cast<std::size_t>(a_next - a)},
                                          {b, static_cast<std::size_t>(b_next - b)}, true);
    if (result != 0)
      return result;
    a = a_next == a_end ? a_end : a_next + 1;
    b = b_next == b_end ? b_end : b_next + 1;
  }
  if (a == a_end && b == b_end)
    return 0;
  return a == a_end ? -1 : 1;
}

bool newer_than_running(const char* candidate) {
  Version proposed{};
  Version running{};
  const esp_app_desc_t* app = esp_app_get_description();
  if (app == nullptr)
    return false;
  char running_version[sizeof app->version + 1]{};
  std::memcpy(running_version, app->version, sizeof app->version);
  return parse_version(candidate, proposed) && parse_version(running_version, running) &&
         compare_versions(proposed, running) > 0;
}

bool digest_equal(const std::uint8_t* left, const std::uint8_t* right, std::size_t size) {
  std::uint8_t difference = 0;
  for (std::size_t index = 0; index < size; ++index)
    difference |= static_cast<std::uint8_t>(left[index] ^ right[index]);
  return difference == 0;
}

} // namespace

CoreOta::CoreOta() : mutex_(xSemaphoreCreateMutexStatic(&mutex_storage_)) {
  mbedtls_sha256_init(&hash_);
  if (mutex_ == nullptr) {
    status_.state = State::failed;
    status_.last_error = Error::no_memory;
    status_.last_esp_error = ESP_ERR_NO_MEM;
  }
}

CoreOta::~CoreOta() {
  if (mutex_ != nullptr) {
    {
      Lock lock(mutex_);
      if (lock.held())
        clear_upload_locked();
    }
    vSemaphoreDelete(mutex_);
    mutex_ = nullptr;
  }
  mbedtls_sha256_free(&hash_);
}

bool CoreOta::valid_version(const char* version) {
  if (version == nullptr)
    return false;
  bool nonspace = false;
  for (std::size_t index = 0; index <= kMaxVersionBytes; ++index) {
    const unsigned char value = static_cast<unsigned char>(version[index]);
    if (value == 0)
      return index != 0 && nonspace;
    if (index == kMaxVersionBytes || value < 0x20 || value > 0x7e)
      return false;
    if (value != ' ')
      nonspace = true;
  }
  return false;
}

bool CoreOta::parse_digest(const char* hex, std::uint8_t (&digest)[kSha256Bytes]) {
  if (hex == nullptr)
    return false;
  for (std::size_t index = 0; index < kSha256Bytes; ++index) {
    const std::uint8_t high = hex_value(hex[index * 2]);
    const std::uint8_t low = hex_value(hex[index * 2 + 1]);
    if (high == 0xff || low == 0xff)
      return false;
    digest[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return hex[kSha256Bytes * 2] == '\0';
}

void CoreOta::clear_upload_locked() {
  if (handle_open_) {
    esp_ota_abort(handle_);
    handle_open_ = false;
    handle_ = 0;
  }
  if (hash_active_) {
    mbedtls_sha256_free(&hash_);
    mbedtls_sha256_init(&hash_);
    hash_active_ = false;
  }
  partition_ = nullptr;
  session_ = 0;
  last_activity_us_ = 0;
  std::memset(expected_digest_, 0, sizeof expected_digest_);
}

CoreOta::Result CoreOta::fail_locked(Error error, esp_err_t esp_error) {
  esp_err_t abort_error = ESP_OK;
  if (handle_open_) {
    abort_error = esp_ota_abort(handle_);
    handle_open_ = false;
    handle_ = 0;
  }
  if (hash_active_) {
    mbedtls_sha256_free(&hash_);
    mbedtls_sha256_init(&hash_);
    hash_active_ = false;
  }
  partition_ = nullptr;
  std::memset(expected_digest_, 0, sizeof expected_digest_);
  session_ = 0;
  last_activity_us_ = 0;
  status_.state = State::failed;
  status_.last_error = error;
  status_.last_esp_error = esp_error;
  if (abort_error != ESP_OK && esp_error == ESP_OK)
    status_.last_esp_error = abort_error;
  return {error, esp_error};
}

void CoreOta::expire_locked() {
  if (status_.state != State::prepared && status_.state != State::staging)
    return;
  const std::int64_t now = esp_timer_get_time();
  if (now >= last_activity_us_ && now - last_activity_us_ >= kUploadIdleTimeoutUs)
    fail_locked(Error::expired, ESP_ERR_TIMEOUT);
}

CoreOta::Result CoreOta::prepare(const Prepare& request) {
  return prepare_impl(request, nullptr);
}

CoreOta::Result CoreOta::prepare(const Prepare& request, SessionId& session) {
  session = 0;
  return prepare_impl(request, &session);
}

CoreOta::Result CoreOta::prepare_impl(const Prepare& request, SessionId* session) {
  Lock lock(mutex_);
  if (!lock.held())
    return {Error::no_memory, ESP_ERR_NO_MEM};
  expire_locked();
  if (status_.state == State::prepared || status_.state == State::staging ||
      status_.state == State::activated)
    return {Error::invalid_state, ESP_ERR_INVALID_STATE};

  std::uint8_t digest[kSha256Bytes]{};
  if (!valid_version(request.version) ||
      std::strlen(request.version) >= sizeof esp_app_desc_t::version ||
      !parse_digest(request.sha256_hex, digest) || request.size_bytes == 0)
    return fail_locked(Error::invalid_argument, ESP_ERR_INVALID_ARG);
  if (request.size_bytes > kMaxImageBytes)
    return fail_locked(Error::image_too_large, ESP_ERR_INVALID_SIZE);
  if (!newer_than_running(request.version))
    return fail_locked(Error::version_not_newer, ESP_ERR_INVALID_VERSION);

  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  if (partition == nullptr)
    return fail_locked(Error::no_update_partition, ESP_ERR_NOT_FOUND);
  if (request.size_bytes > partition->size)
    return fail_locked(Error::image_too_large, ESP_ERR_INVALID_SIZE);

  clear_upload_locked();
  esp_ota_handle_t handle = 0;
  const esp_err_t begin_error = esp_ota_begin(partition, request.size_bytes, &handle);
  if (begin_error != ESP_OK)
    return fail_locked(Error::ota_begin_failed, begin_error);

  partition_ = partition;
  handle_ = handle;
  handle_open_ = true;
  if (mbedtls_sha256_starts(&hash_, 0) != 0)
    return fail_locked(Error::hash_failed, ESP_FAIL);
  hash_active_ = true;
  std::memcpy(expected_digest_, digest, sizeof digest);

  status_ = {};
  status_.state = State::prepared;
  last_activity_us_ = esp_timer_get_time();
  if (session != nullptr) {
    if (++next_session_ == 0)
      ++next_session_;
    session_ = next_session_;
    *session = session_;
  }
  status_.assurance = Assurance::signed_update;
  status_.size_bytes = request.size_bytes;
  status_.restart_after_update = request.restart_after_update;
  std::memcpy(status_.version, request.version, std::strlen(request.version) + 1);
  return {};
}

CoreOta::Result CoreOta::write(const Chunk& chunk) {
  return write_impl(chunk, 0);
}

CoreOta::Result CoreOta::write(const Chunk& chunk, SessionId session) {
  return write_impl(chunk, session);
}

CoreOta::Result CoreOta::write_impl(const Chunk& chunk, SessionId session) {
  Lock lock(mutex_);
  if (!lock.held())
    return {Error::no_memory, ESP_ERR_NO_MEM};
  expire_locked();
  if ((status_.state != State::prepared && status_.state != State::staging) || session != session_)
    return {Error::invalid_state, ESP_ERR_INVALID_STATE};
  if (chunk.data == nullptr || chunk.size == 0)
    return fail_locked(Error::invalid_argument, ESP_ERR_INVALID_ARG);
  if (chunk.size > kMaxChunkBytes)
    return fail_locked(Error::chunk_too_large, ESP_ERR_INVALID_SIZE);
  if (chunk.offset != status_.written_bytes)
    return fail_locked(Error::offset_mismatch, ESP_ERR_INVALID_ARG);
  if (chunk.size > status_.size_bytes - status_.written_bytes)
    return fail_locked(Error::image_too_large, ESP_ERR_INVALID_SIZE);

  const std::uint32_t next = status_.written_bytes + static_cast<std::uint32_t>(chunk.size);
  if (chunk.final_chunk != (next == status_.size_bytes))
    return fail_locked(Error::final_mismatch, ESP_ERR_INVALID_SIZE);

  esp_err_t error = esp_ota_write(handle_, chunk.data, chunk.size);
  if (error != ESP_OK)
    return fail_locked(Error::ota_write_failed, error);
  if (mbedtls_sha256_update(&hash_, chunk.data, chunk.size) != 0)
    return fail_locked(Error::hash_failed, ESP_FAIL);

  status_.written_bytes = next;
  status_.progress_percent =
      static_cast<std::uint8_t>((static_cast<std::uint64_t>(next) * 100U) / status_.size_bytes);
  status_.state = State::staging;
  last_activity_us_ = esp_timer_get_time();
  if (!chunk.final_chunk)
    return {};

  std::uint8_t actual_digest[kSha256Bytes]{};
  if (mbedtls_sha256_finish(&hash_, actual_digest) != 0)
    return fail_locked(Error::hash_failed, ESP_FAIL);
  mbedtls_sha256_free(&hash_);
  mbedtls_sha256_init(&hash_);
  hash_active_ = false;
  if (!digest_equal(actual_digest, expected_digest_, sizeof actual_digest)) {
    std::memset(actual_digest, 0, sizeof actual_digest);
    return fail_locked(Error::digest_mismatch, ESP_ERR_INVALID_CRC);
  }
  std::memset(actual_digest, 0, sizeof actual_digest);

  error = esp_ota_end(handle_);
  handle_open_ = false;
  handle_ = 0;
  if (error != ESP_OK)
    return fail_locked(Error::ota_end_failed, error);

  esp_app_desc_t description{};
  error = esp_ota_get_partition_description(partition_, &description);
  if (error != ESP_OK)
    return fail_locked(Error::ota_end_failed, error);
  if (std::strncmp(description.version, status_.version, sizeof description.version) != 0 ||
      std::strlen(status_.version) >= sizeof description.version)
    return fail_locked(Error::version_mismatch, ESP_ERR_INVALID_VERSION);
  if (!newer_than_running(description.version))
    return fail_locked(Error::version_not_newer, ESP_ERR_INVALID_VERSION);

  error = esp_ota_set_boot_partition(partition_);
  if (error != ESP_OK)
    return fail_locked(Error::boot_partition_failed, error);

  partition_ = nullptr;
  session_ = 0;
  last_activity_us_ = 0;
  std::memset(expected_digest_, 0, sizeof expected_digest_);
  status_.state = State::activated;
  status_.digest_verified = true;
  status_.last_error = Error::ok;
  status_.last_esp_error = ESP_OK;
  return {};
}

CoreOta::Result CoreOta::abort() {
  return abort_impl(0);
}

CoreOta::Result CoreOta::abort(SessionId session) {
  if (session == 0)
    return {Error::invalid_argument, ESP_ERR_INVALID_ARG};
  return abort_impl(session);
}

CoreOta::Result CoreOta::abort_impl(SessionId session) {
  Lock lock(mutex_);
  if (!lock.held())
    return {Error::no_memory, ESP_ERR_NO_MEM};
  expire_locked();
  if (session != session_ ||
      (session != 0 && status_.state != State::prepared && status_.state != State::staging))
    return {Error::invalid_state, ESP_ERR_INVALID_STATE};
  esp_err_t error = ESP_OK;
  if (handle_open_)
    error = esp_ota_abort(handle_);
  handle_open_ = false;
  handle_ = 0;
  if (hash_active_) {
    mbedtls_sha256_free(&hash_);
    mbedtls_sha256_init(&hash_);
    hash_active_ = false;
  }
  partition_ = nullptr;
  session_ = 0;
  last_activity_us_ = 0;
  std::memset(expected_digest_, 0, sizeof expected_digest_);
  status_ = {};
  if (error != ESP_OK) {
    status_.state = State::failed;
    status_.last_error = Error::abort_failed;
    status_.last_esp_error = error;
    return {Error::abort_failed, error};
  }
  return {};
}

CoreOta::Status CoreOta::status() const {
  Lock lock(mutex_);
  if (!lock.held())
    return status_;
  const_cast<CoreOta*>(this)->expire_locked();
  return status_;
}

} // namespace core_pilot
