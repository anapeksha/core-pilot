#pragma once

#include "core_security.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace core_pilot {

class CoreMatter;

struct ThreadDataset final {
  static constexpr std::size_t kMaxBytes = 254;

  std::uint8_t tlvs[kMaxBytes]{};
  std::uint8_t length{0};
  std::uint64_t generation{0};

  void clear();
};

bool decodeThreadDatasetHex(const char* hex, ThreadDataset& dataset);
bool validateThreadDataset(const std::uint8_t* tlvs, std::size_t length);
core_security::Status loadThreadDataset(core_security::StorageService& storage,
                                        ThreadDataset& dataset);
core_security::Status saveThreadDataset(core_security::StorageService& storage,
                                        core_security::RandomSource& random,
                                        ThreadDataset& dataset);

class ThreadDatasetService final {
public:
  enum class State : std::uint8_t { idle, queued, persisting, submitting, complete, failed };

  struct Status final {
    State state{State::idle};
    std::uint32_t operation_id{};
    esp_err_t last_error{ESP_OK};
  };

  ThreadDatasetService(core_security::StorageService& storage, core_security::RandomSource& random,
                       CoreMatter& matter);
  esp_err_t start();
  esp_err_t submit(const ThreadDataset& dataset, std::uint32_t& operation_id);
  void reconcile();
  void reconcileMatterTerminal(std::uint32_t matter_operation_id, bool succeeded, esp_err_t error);
  Status status() const;

private:
  struct Request final {
    std::uint32_t operation_id{};
    std::uint8_t length{};
    std::uint8_t tlvs[ThreadDataset::kMaxBytes]{};
  };
  static_assert(std::is_trivially_copyable_v<Request>);

  void publish(State state, std::uint32_t operation_id, esp_err_t error);

  core_security::StorageService& storage_;
  core_security::RandomSource& random_;
  CoreMatter& matter_;
  StaticQueue_t queue_storage_{};
  std::uint8_t queue_buffer_[sizeof(Request)]{};
  QueueHandle_t queue_{nullptr};
  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  Status status_{};
  std::uint32_t next_operation_id_{1};
  std::uint32_t matter_operation_id_{};
};

const char* threadDatasetOperationState(ThreadDatasetService::State state);

} // namespace core_pilot
