#pragma once

#include "core_ai.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cstddef>
#include <cstdint>

namespace core_pilot {

struct HttpRuntime;

class AiChatService final {
public:
  static constexpr std::size_t kMaxMessageBytes = 4096;
  static constexpr std::size_t kMaxHistoryEntries = 16;
  static constexpr std::size_t kMaxHistoryContentBytes = 4096;
  static constexpr std::size_t kMaxResponseBytes = std::size_t{16} * 1024;
  static constexpr std::size_t kMaxActions = 8;
  static constexpr std::size_t kMaxErrorDetailBytes = 95;

  enum class Role : std::uint8_t { user, assistant, tool };
  enum class State : std::uint8_t { idle, queued, processing, complete, failed };
  enum class SubmitResult : std::uint8_t {
    queued,
    invalid,
    too_large,
    busy,
    not_ready,
    no_memory,
  };

  enum class Error : std::uint8_t {
    none,
    not_configured,
    invalid_provider,
    unsafe_url,
    actions_unsupported,
    json_encode,
    network,
    timeout,
    tls,
    http_status,
    response_too_large,
    invalid_response,
    internal,
  };

  struct HistoryEntry final {
    Role role{Role::user};
    const char* content{};
    std::size_t content_length{};
  };

  struct Request final {
    const char* message{};
    std::size_t message_length{};
    const HistoryEntry* history{};
    std::size_t history_count{};
  };

  struct Action final {
    char tool[65]{};
    char result_json[2049]{};
  };

  struct Snapshot final {
    State state{State::idle};
    std::uint64_t operation_id{};
    Error error{Error::none};
    char error_detail[kMaxErrorDetailBytes + 1]{};
    std::size_t response_length{};
    std::uint8_t action_count{};
    Action actions[kMaxActions]{};
  };

  explicit AiChatService(ai::AiStorageService& storage);
  ~AiChatService();
  AiChatService(const AiChatService&) = delete;
  AiChatService& operator=(const AiChatService&) = delete;

  // Creates synchronization primitives only. Large PSRAM slots and the internal-RAM owner
  // task stack are allocated lazily by the first valid submit.
  esp_err_t start();

  // Binds the service to the application runtime without taking ownership. The runtime must
  // outlive this service and must be bound before action-enabled requests are submitted.
  void bindRuntime(const HttpRuntime& runtime);

  // Never waits for the network or for a lock. The request is copied before this returns.
  SubmitResult submit(const Request& input, std::uint64_t& operation_id);

  // Never waits for the network. Returns false if the snapshot is momentarily busy or the
  // supplied response buffer cannot hold the completed response (including its trailing NUL).
  bool status(std::uint64_t operation_id, Snapshot& output, char* response,
              std::size_t response_capacity) const;

private:
  struct RequestSlot;
  struct Runtime;

  static constexpr UBaseType_t kQueueDepth = 1;
  static constexpr std::uint32_t kTaskStackBytes = 12288;

  static void taskEntry(void* context);
  bool ensureWorker();
  void taskLoop();
  void process(std::uint64_t operation_id);

  ai::AiStorageService& storage_;
  const HttpRuntime* http_runtime_{};
  mutable StaticSemaphore_t mutex_storage_{};
  mutable SemaphoreHandle_t mutex_{};
  StaticQueue_t queue_storage_{};
  std::uint8_t queue_buffer_[kQueueDepth * sizeof(std::uint64_t)]{};
  QueueHandle_t queue_{};
  StaticTask_t task_storage_{};
  StackType_t* task_stack_{};
  TaskHandle_t task_{};
  RequestSlot* request_{};
  Runtime* runtime_{};
  std::uint64_t next_operation_id_{1};
  bool started_{};
};

} // namespace core_pilot
