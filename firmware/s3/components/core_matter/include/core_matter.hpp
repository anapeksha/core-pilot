#pragma once

#include "core_matter_terminal_history.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lib/core/CHIPError.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace core_pilot {

class CoreMatter final {
public:
  enum class State : std::uint8_t {
    stopped,
    starting,
    ready,
    failed,
  };

  enum class CommissioningTransport : std::uint8_t {
    wifi,
    thread,
  };

  enum class CommissioningState : std::uint8_t {
    idle,
    queued,
    pase,
    cancelling,
    complete,
    cancelled,
    failed,
  };

  enum class CommandKind : std::uint8_t {
    none,
    set_on_off,
    set_level,
    set_color_temperature,
    unpair_device,
  };

  enum class CommandState : std::uint8_t {
    idle,
    queued,
    invoking,
    complete,
    failed,
  };

  struct CommandStatus final {
    CommandKind kind;
    CommandState state;
    std::uint32_t operation_id;
    std::uint64_t node_id;
    std::uint16_t endpoint_id;
    bool requested_on;
    esp_err_t last_error;
  };

  struct TerminalCommand final {
    std::uint64_t sequence;
    CommandStatus status;
  };
  static_assert(std::is_trivially_copyable_v<TerminalCommand>);

  enum Capability : std::uint32_t {
    on_off = 1U << 0,
    level = 1U << 1,
    color = 1U << 2,
    thermostat = 1U << 3,
    fan = 1U << 4,
    lock = 1U << 5,
    window_covering = 1U << 6,
    occupancy = 1U << 7,
    temperature = 1U << 8,
    humidity = 1U << 9,
    electrical_power = 1U << 10,
    electrical_energy = 1U << 11,
  };

  struct DiscoveredEndpoint final {
    std::uint64_t node_id;
    std::uint16_t endpoint_id;
    std::uint32_t capabilities;
  };

  struct DiscoveryStatus final {
    bool active;
    bool complete;
    std::uint32_t operation_id;
    esp_err_t last_error;
    std::size_t count;
    DiscoveredEndpoint endpoints[16];
  };

  struct Status final {
    State state;
    bool commissioner_ready;
    bool fabric_configured;
    bool border_router_ready;
    bool thread_dataset_configured;
    bool thread_attached;
    CommissioningTransport commissioning_transport;
    CommissioningState commissioning_state;
    std::uint32_t operation_id;
    std::uint64_t commissioned_node_id;
    std::uint16_t subscriptions_active;
    bool case_ready;
    esp_err_t last_error;
  };

  static constexpr std::size_t kMaxThreadDatasetBytes = 254;

  ~CoreMatter();
  CoreMatter() = default;
  CoreMatter(const CoreMatter&) = delete;
  CoreMatter& operator=(const CoreMatter&) = delete;

  esp_err_t begin(const char* wifi_ssid, std::size_t wifi_ssid_length, const char* wifi_password,
                  std::size_t wifi_password_length, const std::uint8_t* thread_dataset = nullptr,
                  std::size_t thread_dataset_length = 0);
  esp_err_t commission(CommissioningTransport transport, const char* setup_payload,
                       std::uint32_t& operation_id);
  esp_err_t applyThreadDataset(const std::uint8_t* dataset, std::size_t length);
  esp_err_t cancelCommissioning(std::uint32_t operation_id);
  esp_err_t setOnOff(std::uint64_t node_id, std::uint16_t endpoint_id, bool on,
                     std::uint32_t& operation_id);
  esp_err_t setLevel(std::uint64_t node_id, std::uint16_t endpoint_id, std::uint8_t level,
                     std::uint16_t transition_time_ds, std::uint32_t& operation_id);
  esp_err_t setColorTemperature(std::uint64_t node_id, std::uint16_t endpoint_id,
                                std::uint16_t color_temperature_mireds,
                                std::uint16_t transition_time_ds, std::uint32_t& operation_id);
  esp_err_t unpairDevice(std::uint64_t node_id, std::uint32_t& operation_id);
  esp_err_t discoverEndpoints(std::uint64_t node_id);
  esp_err_t subscribeOnOff(std::uint64_t node_id);
  Status status() const;
  CommandStatus commandStatus() const;
  bool terminalCommand(std::uint32_t operation_id, TerminalCommand& terminal) const;
  std::size_t terminalCommandsAfter(std::uint64_t after_sequence, TerminalCommand* terminals,
                                    std::size_t capacity) const;
  esp_err_t waitForTerminalCommand(std::uint32_t operation_id, TickType_t timeout,
                                   TerminalCommand& terminal);
  DiscoveryStatus discoveryStatus() const;

private:
  enum class CommandType : std::uint8_t {
    commission,
    apply_thread_dataset,
    cancel_commissioning,
    set_on_off,
    set_level,
    set_color_temperature,
    unpair_device,
    discover_endpoints,
    subscribe_on_off,
    stop
  };
  /** Bounded value copied across cores by the FreeRTOS request queue. */
  struct Command final {
    CommandType type;
    CommissioningTransport transport;
    std::uint8_t dataset_length;
    std::uint32_t operation_id;
    std::uint64_t node_id;
    std::uint16_t endpoint_id;
    std::uint16_t value;
    std::uint16_t transition_time_ds;
    bool on;
    char setup_payload[22];
    std::uint8_t dataset[kMaxThreadDatasetBytes];
  };
  static_assert(std::is_trivially_copyable_v<Command>);
  struct QueueMessage final {
    std::uint8_t slot;
  };
  static_assert(std::is_trivially_copyable_v<QueueMessage>);

  static void taskEntry(void* context);
  static void onUnpairComplete(std::uint64_t node_id, CHIP_ERROR error);
  void run();
  esp_err_t startController();
  esp_err_t applyThreadDatasetOnOwner(const std::uint8_t* dataset, std::size_t length);
  void refreshThreadStatus();
  esp_err_t enqueueCommand(const Command& command);
  bool receiveCommand(Command& command, TickType_t timeout);
  void clearSecrets();
  esp_err_t enqueueDeviceCommand(Command command, std::uint32_t& operation_id);
  bool finishCommand(std::uint32_t operation_id, CommandState state, esp_err_t error);

  struct InvokeCallbackContext final {
    CoreMatter* owner;
    std::uint32_t operation_id;
  };
  static_assert(std::is_trivially_copyable_v<InvokeCallbackContext>);

  static constexpr UBaseType_t kQueueDepth = 8;
  static constexpr std::size_t kTerminalCommandDepth = 8;
  StaticQueue_t queue_storage_{};
  std::uint8_t queue_buffer_[kQueueDepth * sizeof(QueueMessage)]{};
  QueueHandle_t queue_{nullptr};
  Command command_slots_[kQueueDepth]{};
  bool command_slot_used_[kQueueDepth]{};
  TaskHandle_t task_{nullptr};
  portMUX_TYPE status_lock_ = portMUX_INITIALIZER_UNLOCKED;
  Status status_{
      State::stopped,           false, false, false, false, false, CommissioningTransport::wifi,
      CommissioningState::idle, 0,     0,     0,     false, ESP_OK};
  CommandStatus command_status_{CommandKind::none, CommandState::idle, 0, 0, 0, false, ESP_OK};
  detail::TerminalHistory<TerminalCommand, kTerminalCommandDepth> terminal_commands_{};
  std::uint64_t next_terminal_sequence_{1};
  StaticSemaphore_t terminal_signal_storage_{};
  SemaphoreHandle_t terminal_signal_{nullptr};
  InvokeCallbackContext invoke_callback_context_{};
  std::uint32_t active_unpair_operation_id_{0};
  DiscoveryStatus discovery_status_{};
  std::uint32_t next_operation_id_{1};
  std::uint32_t next_command_operation_id_{1};
  std::uint32_t next_discovery_operation_id_{1};
  std::int64_t discovery_started_us_{0};
  std::uint64_t active_node_id_{0};
  static CoreMatter* callback_owner_;
  std::uint8_t wifi_ssid_[33]{};
  std::size_t wifi_ssid_length_{0};
  std::uint8_t wifi_password_[65]{};
  std::size_t wifi_password_length_{0};
  std::uint8_t thread_dataset_[kMaxThreadDatasetBytes]{};
  std::uint8_t thread_dataset_length_{0};
};

} // namespace core_pilot
