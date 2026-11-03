#include "core_ota.hpp"

#include "core_ota_policy.hpp"
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

bool newer_than_running(const char* candidate) {

  const esp_app_desc_t* app = esp_app_get_description();
  if (app == nullptr)
    return false;
  char running_version[sizeof app->version + 1]{};
  std::memcpy(running_version, app->version, sizeof app->version);
  return ota_policy::newer_version(candidate, running_version);
}

} // namespace

CoreOta::CoreOta() : mutex_(xSemaphoreCreateMutexStatic(&mutex_storage_)) {
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
  psa_hash_abort(&hash_);
}

bool CoreOta::valid_version(const char* version) {
  return ota_policy::valid_text_version(version, kMaxVersionBytes);
}

bool CoreOta::parse_digest(const char* hex, std::uint8_t (&digest)[kSha256Bytes]) {
  return ota_policy::parse_digest(hex, digest, kSha256Bytes);
}

void CoreOta::clear_upload_locked() {
  if (handle_open_) {
    esp_ota_abort(handle_);
    handle_open_ = false;
    handle_ = 0;
  }
  if (hash_active_) {
    psa_hash_abort(&hash_);
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
    psa_hash_abort(&hash_);
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
  if (psa_crypto_init() != PSA_SUCCESS || psa_hash_setup(&hash_, PSA_ALG_SHA_256) != PSA_SUCCESS)
    return fail_locked(Error::hash_failed, ESP_FAIL);
  hash_active_ = true;
  std::memcpy(expected_digest_, digest, sizeof digest);

  status_ = {};
  status_.state = State::prepared;
  last_activity_us_ = esp_timer_get_time();
  if (session != nullptr) {
    next_session_ = ota_policy::next_session(next_session_);
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
  if ((status_.state != State::prepared && status_.state != State::staging) ||
      !ota_policy::session_matches(session, session_))
    return {Error::invalid_state, ESP_ERR_INVALID_STATE};
  const auto chunk_error =
      ota_policy::validate_chunk(chunk.data, chunk.size, kMaxChunkBytes, chunk.offset,
                                 status_.written_bytes, status_.size_bytes, chunk.final_chunk);
  switch (chunk_error) {
  case ota_policy::ChunkError::invalid_argument:
    return fail_locked(Error::invalid_argument, ESP_ERR_INVALID_ARG);
  case ota_policy::ChunkError::chunk_too_large:
    return fail_locked(Error::chunk_too_large, ESP_ERR_INVALID_SIZE);
  case ota_policy::ChunkError::offset_mismatch:
    return fail_locked(Error::offset_mismatch, ESP_ERR_INVALID_ARG);
  case ota_policy::ChunkError::image_too_large:
    return fail_locked(Error::image_too_large, ESP_ERR_INVALID_SIZE);
  case ota_policy::ChunkError::final_mismatch:
    return fail_locked(Error::final_mismatch, ESP_ERR_INVALID_SIZE);
  case ota_policy::ChunkError::ok:
    break;
  }

  const std::uint32_t next = status_.written_bytes + static_cast<std::uint32_t>(chunk.size);

  esp_err_t error = esp_ota_write(handle_, chunk.data, chunk.size);
  if (error != ESP_OK)
    return fail_locked(Error::ota_write_failed, error);
  if (psa_hash_update(&hash_, chunk.data, chunk.size) != PSA_SUCCESS)
    return fail_locked(Error::hash_failed, ESP_FAIL);

  status_.written_bytes = next;
  status_.progress_percent = ota_policy::progress_percent(next, status_.size_bytes);
  status_.state = State::staging;
  last_activity_us_ = esp_timer_get_time();
  if (!chunk.final_chunk)
    return {};

  std::uint8_t actual_digest[kSha256Bytes]{};
  std::size_t actual_digest_size = 0;
  if (psa_hash_finish(&hash_, actual_digest, sizeof actual_digest, &actual_digest_size) !=
          PSA_SUCCESS ||
      actual_digest_size != sizeof actual_digest)
    return fail_locked(Error::hash_failed, ESP_FAIL);
  hash_active_ = false;
  if (!ota_policy::digest_equal(actual_digest, expected_digest_, sizeof actual_digest)) {
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
    psa_hash_abort(&hash_);
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
