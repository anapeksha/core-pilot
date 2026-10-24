#include "core_ota.hpp"

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
  status_.state = State::failed;
  status_.last_error = error;
  status_.last_esp_error = esp_error;
  if (abort_error != ESP_OK && esp_error == ESP_OK)
    status_.last_esp_error = abort_error;
  return {error, esp_error};
}

CoreOta::Result CoreOta::prepare(const Prepare& request) {
  Lock lock(mutex_);
  if (!lock.held())
    return {Error::no_memory, ESP_ERR_NO_MEM};
  if (status_.state == State::prepared || status_.state == State::staging ||
      status_.state == State::activated)
    return {Error::invalid_state, ESP_ERR_INVALID_STATE};

  std::uint8_t digest[kSha256Bytes]{};
  if (!valid_version(request.version) || !parse_digest(request.sha256_hex, digest) ||
      request.size_bytes == 0)
    return fail_locked(Error::invalid_argument, ESP_ERR_INVALID_ARG);
  if (request.size_bytes > kMaxImageBytes)
    return fail_locked(Error::image_too_large, ESP_ERR_INVALID_SIZE);

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
  status_.assurance = Assurance::signed_update;
  status_.size_bytes = request.size_bytes;
  status_.restart_after_update = request.restart_after_update;
  std::memcpy(status_.version, request.version, std::strlen(request.version) + 1);
  return {};
}

CoreOta::Result CoreOta::write(const Chunk& chunk) {
  Lock lock(mutex_);
  if (!lock.held())
    return {Error::no_memory, ESP_ERR_NO_MEM};
  if (status_.state != State::prepared && status_.state != State::staging)
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
  if (std::strcmp(description.version, status_.version) != 0)
    return fail_locked(Error::version_mismatch, ESP_ERR_INVALID_VERSION);

  error = esp_ota_set_boot_partition(partition_);
  if (error != ESP_OK)
    return fail_locked(Error::boot_partition_failed, error);

  partition_ = nullptr;
  std::memset(expected_digest_, 0, sizeof expected_digest_);
  status_.state = State::activated;
  status_.digest_verified = true;
  status_.last_error = Error::ok;
  status_.last_esp_error = ESP_OK;
  return {};
}

CoreOta::Result CoreOta::abort() {
  Lock lock(mutex_);
  if (!lock.held())
    return {Error::no_memory, ESP_ERR_NO_MEM};
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
  return status_;
}

} // namespace core_pilot
