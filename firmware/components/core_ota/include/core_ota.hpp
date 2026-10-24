#pragma once

#include "core_domain.hpp"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"

#include <cstddef>
#include <cstdint>

namespace core_pilot {

class CoreOta final {
public:
  static constexpr std::size_t kMaxChunkBytes = 2 * 1024;
  static constexpr std::uint32_t kMaxImageBytes = core_domain::max_ota_image;
  static constexpr std::size_t kSha256Bytes = 32;
  static constexpr std::size_t kMaxVersionBytes = 64;

  enum class State : std::uint8_t { idle, prepared, staging, activated, failed };

  // ESP-IDF verifies the publisher signature when the completed image is finalized.
  enum class Assurance : std::uint8_t { signed_update };

  enum class Error : std::uint8_t {
    ok,
    invalid_argument,
    invalid_state,
    no_update_partition,
    image_too_large,
    chunk_too_large,
    offset_mismatch,
    final_mismatch,
    digest_mismatch,
    version_mismatch,
    no_memory,
    ota_begin_failed,
    ota_write_failed,
    hash_failed,
    ota_end_failed,
    boot_partition_failed,
    abort_failed,
  };

  struct Prepare final {
    char version[kMaxVersionBytes + 1]{};
    std::uint32_t size_bytes{};
    char sha256_hex[kSha256Bytes * 2 + 1]{};
    bool restart_after_update{};
  };

  struct Chunk final {
    std::uint32_t offset{};
    const std::uint8_t* data{};
    std::size_t size{};
    bool final_chunk{};
  };

  struct Result final {
    Error error{Error::ok};
    esp_err_t esp_error{ESP_OK};

    explicit operator bool() const {
      return error == Error::ok;
    }
  };

  struct Status final {
    State state{State::idle};
    Assurance assurance{Assurance::signed_update};
    Error last_error{Error::ok};
    esp_err_t last_esp_error{ESP_OK};
    std::uint32_t size_bytes{};
    std::uint32_t written_bytes{};
    std::uint8_t progress_percent{};
    bool restart_after_update{};
    bool digest_verified{};
    char version[kMaxVersionBytes + 1]{};
  };

  CoreOta();
  ~CoreOta();
  CoreOta(const CoreOta&) = delete;
  CoreOta& operator=(const CoreOta&) = delete;

  Result prepare(const Prepare& request);
  Result write(const Chunk& chunk);
  Result abort();
  Status status() const;

private:
  Result fail_locked(Error error, esp_err_t esp_error = ESP_OK);
  void clear_upload_locked();
  static bool parse_digest(const char* hex, std::uint8_t (&digest)[kSha256Bytes]);
  static bool valid_version(const char* version);

  mutable StaticSemaphore_t mutex_storage_{};
  mutable SemaphoreHandle_t mutex_{};
  Status status_{};
  const esp_partition_t* partition_{};
  esp_ota_handle_t handle_{};
  bool handle_open_{};
  bool hash_active_{};
  mbedtls_sha256_context hash_{};
  std::uint8_t expected_digest_[kSha256Bytes]{};
};

} // namespace core_pilot
