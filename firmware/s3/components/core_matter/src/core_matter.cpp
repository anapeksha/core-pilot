#include "core_matter.hpp"

#include "app-common/zap-generated/ids/Clusters.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_matter.h"
#include "esp_matter_controller_client.h"
#include "esp_matter_controller_read_command.h"
#include "esp_matter_controller_subscribe_command.h"
#include "esp_matter_controller_utils.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_types.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/platform_util.h"
#include "mdns.h"
#include "openthread/dataset.h"
#include "openthread/ip6.h"
#include "openthread/thread.h"
#include "platform/ESP32/OpenthreadLauncher.h"
#include "setup_payload/ManualSetupPayloadParser.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

namespace core_pilot {

CoreMatter* CoreMatter::callback_owner_ = nullptr;

namespace {

constexpr char kTag[] = "core_matter";
constexpr std::uint16_t kControllerPort = 0;
constexpr std::uint64_t kFabricId = 0x434F524550494C4FULL;
constexpr std::uint16_t kMaxTransitionTimeDs = 0xfffe;
constexpr std::uint16_t kMinColorTemperatureMireds = 1;
constexpr std::uint16_t kMaxColorTemperatureMireds = 0xfeff;

std::atomic_bool border_router_initialized{false};
std::atomic_bool controller_initializing{false};
std::atomic<CoreMatter::CommissioningState> commissioning_state{
    CoreMatter::CommissioningState::idle};
std::atomic<std::uint64_t> commissioned_node_id{0};
std::atomic<esp_err_t> commissioning_error{ESP_OK};
std::atomic<std::int64_t> commissioning_started_us{0};
std::atomic<std::uint8_t> cancellation_reason{0};
std::atomic_bool on_off_subscription_active{false};
std::atomic<std::uint64_t> reported_on_off_node_id{0};
std::atomic<std::uint16_t> reported_on_off_endpoint_id{0};
std::atomic_bool reported_on_off_known{false};
std::atomic_bool reported_on_off_value{false};
constexpr std::int64_t kCommissioningTimeoutUs = 120LL * 1000 * 1000;
constexpr std::int64_t kDiscoveryTimeoutUs = 30LL * 1000 * 1000;

void onPaseComplete(CHIP_ERROR error) {
  if (error != CHIP_NO_ERROR) {
    const auto reason = cancellation_reason.exchange(0);
    commissioning_error.store(reason == 1 ? ESP_OK : reason == 2 ? ESP_ERR_TIMEOUT : ESP_FAIL);
    commissioning_state.store(reason == 1 ? CoreMatter::CommissioningState::cancelled
                                          : CoreMatter::CommissioningState::failed);
  }
}

void onCommissioningSuccess(chip::ScopedNodeId peer_id) {
  cancellation_reason.store(0);
  commissioned_node_id.store(peer_id.GetNodeId());
  commissioning_error.store(ESP_OK);
  commissioning_state.store(CoreMatter::CommissioningState::complete);
  ESP_LOGI(kTag, "Matter commissioning completed for node 0x%016llx",
           static_cast<unsigned long long>(peer_id.GetNodeId()));
}

void onOnOffReport(std::uint64_t node_id, const chip::app::ConcreteDataAttributePath& path,
                   chip::TLV::TLVReader* data, const chip::app::StatusIB& status) {
  bool value = false;
  if (data == nullptr || !status.IsSuccess() || data->Get(value) != CHIP_NO_ERROR)
    return;
  reported_on_off_node_id.store(node_id);
  reported_on_off_endpoint_id.store(path.mEndpointId);
  reported_on_off_value.store(value);
  reported_on_off_known.store(true);
}

void onSubscriptionEstablished(std::uint64_t, std::uint32_t) {
  on_off_subscription_active.store(true);
}

void onSubscriptionTerminated(std::uint64_t node_id, std::uint32_t) {
  on_off_subscription_active.store(false);
  if (reported_on_off_node_id.load() == node_id)
    reported_on_off_known.store(false);
}

void onCommissioningFailure(chip::NodeId node_id, CHIP_ERROR error,
                            chip::Controller::CommissioningStage stage,
                            std::optional<chip::Credentials::AttestationVerificationResult>) {
  const auto reason = cancellation_reason.exchange(0);
  commissioning_error.store(reason == 1 ? ESP_OK : reason == 2 ? ESP_ERR_TIMEOUT : ESP_FAIL);
  commissioning_state.store(reason == 1 ? CoreMatter::CommissioningState::cancelled
                                        : CoreMatter::CommissioningState::failed);
  ESP_LOGE(kTag, "Matter commissioning failed for node 0x%016llx at stage %u: %s",
           static_cast<unsigned long long>(node_id), static_cast<unsigned>(stage),
           chip::ErrorStr(error));
}

esp_openthread_platform_config_t threadPlatformConfig() {
  esp_openthread_platform_config_t config{};
  config.radio_config.radio_mode = RADIO_MODE_UART_RCP;
  config.radio_config.radio_uart_config.port = UART_NUM_1;
  config.radio_config.radio_uart_config.uart_config = {
      .baud_rate = 460800,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 0,
      .source_clk = UART_SCLK_DEFAULT,
  };
  config.radio_config.radio_uart_config.rx_pin = GPIO_NUM_10;
  config.radio_config.radio_uart_config.tx_pin = GPIO_NUM_17;
  config.host_config.host_connection_mode = HOST_CONNECTION_MODE_NONE;
  config.port_config.storage_partition_name = "nvs";
  config.port_config.netif_queue_size = 10;
  config.port_config.task_queue_size = 10;
  return config;
}

std::uint32_t capabilityForCluster(std::uint32_t cluster_id) {
  switch (cluster_id) {
  case 0x0006:
    return CoreMatter::Capability::on_off;
  case 0x0008:
    return CoreMatter::Capability::level;
  case 0x0300:
    return CoreMatter::Capability::color;
  case 0x0201:
    return CoreMatter::Capability::thermostat;
  case 0x0202:
    return CoreMatter::Capability::fan;
  case 0x0101:
    return CoreMatter::Capability::lock;
  case 0x0102:
    return CoreMatter::Capability::window_covering;
  case 0x0406:
    return CoreMatter::Capability::occupancy;
  case 0x0402:
    return CoreMatter::Capability::temperature;
  case 0x0405:
    return CoreMatter::Capability::humidity;
  case 0x0090:
    return CoreMatter::Capability::electrical_power;
  case 0x0091:
    return CoreMatter::Capability::electrical_energy;
  default:
    return 0;
  }
}

class InvokeRequest final {
public:
  using SuccessCallback =
      esp_matter::client::interaction::invoke::custom_command_callback::on_success_callback_t;
  using ErrorCallback =
      esp_matter::client::interaction::invoke::custom_command_callback::on_error_callback_t;
  using ConnectFailureCallback = esp_matter::controller::on_connect_failure_cb_t;

  InvokeRequest(std::uint64_t node_id, std::uint16_t endpoint_id, std::uint32_t cluster_id,
                std::uint32_t command_id, const char* command_data, void* callback_context,
                SuccessCallback success, ErrorCallback error,
                ConnectFailureCallback connect_failure)
      : node_id_(node_id), endpoint_id_(endpoint_id), cluster_id_(cluster_id),
        command_id_(command_id),
        command_data_(
            command_data,
            esp_matter::client::interaction::custom_encodable_type::interaction_type::k_invoke_cmd),
        callback_context_(callback_context), success_(std::move(success)), error_(std::move(error)),
        connect_failure_(std::move(connect_failure)), connected_callback_(onConnected, this),
        connection_failure_callback_(onConnectionFailure, this) {}

  esp_err_t start() {
    auto* commissioner =
        esp_matter::controller::matter_controller_client::get_instance().get_commissioner();
    const CHIP_ERROR result = commissioner->GetConnectedDevice(node_id_, &connected_callback_,
                                                               &connection_failure_callback_);
    if (result == CHIP_NO_ERROR)
      return ESP_OK;
    chip::Platform::Delete(this);
    return result == CHIP_ERROR_NO_MEMORY ? ESP_ERR_NO_MEM : ESP_FAIL;
  }

private:
  static void onConnected(void* context, chip::Messaging::ExchangeManager& exchange_manager,
                          const chip::SessionHandle& session) {
    auto* request = static_cast<InvokeRequest*>(context);
    chip::OperationalDeviceProxy device(&exchange_manager, session);
    const chip::app::CommandPathParams path = {request->endpoint_id_, 0, request->cluster_id_,
                                               request->command_id_,
                                               chip::app::CommandPathFlags::kEndpointIdValid};
    const esp_err_t result = esp_matter::client::interaction::invoke::send_request(
        request->callback_context_, &device, path, request->command_data_, request->success_,
        request->error_, chip::NullOptional);
    if (result != ESP_OK && request->error_ != nullptr)
      request->error_(request->callback_context_, CHIP_ERROR_INTERNAL);
    chip::Platform::Delete(request);
  }

  static void onConnectionFailure(void* context, const chip::ScopedNodeId& peer_id,
                                  CHIP_ERROR error) {
    auto* request = static_cast<InvokeRequest*>(context);
    if (request->connect_failure_ != nullptr)
      request->connect_failure_(request->callback_context_, peer_id, error);
    chip::Platform::Delete(request);
  }

  std::uint64_t node_id_;
  std::uint16_t endpoint_id_;
  std::uint32_t cluster_id_;
  std::uint32_t command_id_;
  esp_matter::client::interaction::custom_encodable_type command_data_;
  void* callback_context_;
  SuccessCallback success_;
  ErrorCallback error_;
  ConnectFailureCallback connect_failure_;
  chip::Callback::Callback<chip::OnDeviceConnected> connected_callback_;
  chip::Callback::Callback<chip::OnDeviceConnectionFailure> connection_failure_callback_;
};

esp_err_t sendInvokeRequest(std::uint64_t node_id, std::uint16_t endpoint_id,
                            std::uint32_t cluster_id, std::uint32_t command_id,
                            const char* command_data, void* callback_context,
                            const InvokeRequest::SuccessCallback& success,
                            const InvokeRequest::ErrorCallback& error,
                            const InvokeRequest::ConnectFailureCallback& connect_failure) {
  auto* request =
      chip::Platform::New<InvokeRequest>(node_id, endpoint_id, cluster_id, command_id, command_data,
                                         callback_context, success, error, connect_failure);
  return request != nullptr ? request->start() : ESP_ERR_NO_MEM;
}

bool txtNumber(const mdns_result_t& result, const char* key, std::uint32_t& value) {
  for (std::size_t index = 0; index < result.txt_count; ++index) {
    if (result.txt[index].key == nullptr || result.txt[index].value == nullptr ||
        std::strcmp(result.txt[index].key, key) != 0) {
      continue;
    }
    const std::size_t length = result.txt_value_len[index];
    if (length == 0 || length > 10) {
      return false;
    }
    std::uint32_t parsed = 0;
    for (std::size_t digit = 0; digit < length; ++digit) {
      const char character = result.txt[index].value[digit];
      if (character < '0' || character > '9') {
        return false;
      }
      parsed = parsed * 10U + static_cast<std::uint32_t>(character - '0');
    }
    value = parsed;
    return true;
  }
  return false;
}

class PairingDelegate final : public chip::Controller::DevicePairingDelegate {
public:
  void OnPairingComplete(CHIP_ERROR error) override {
    if (error != CHIP_NO_ERROR) {
      commissioner()->RegisterPairingDelegate(nullptr);
    }
    onPaseComplete(error);
  }

  void OnCommissioningSuccess(chip::PeerId peer_id) override {
    auto* device_commissioner = commissioner();
    device_commissioner->RegisterPairingDelegate(nullptr);
    const auto* fabric = device_commissioner->GetFabricTable()->FindFabricWithCompressedId(
        peer_id.GetCompressedFabricId());
    if (fabric != nullptr) {
      onCommissioningSuccess(chip::ScopedNodeId(peer_id.GetNodeId(), fabric->GetFabricIndex()));
    }
  }

  void OnCommissioningFailure(chip::PeerId peer_id, CHIP_ERROR error,
                              chip::Controller::CommissioningStage stage,
                              chip::Optional<chip::Credentials::AttestationVerificationResult>
                                  attestation_result) override {
    auto* device_commissioner = commissioner();
    device_commissioner->RegisterPairingDelegate(nullptr);
    onCommissioningFailure(peer_id.GetNodeId(), error, stage,
                           attestation_result.HasValue()
                               ? std::make_optional(attestation_result.Value())
                               : std::nullopt);
  }

  static PairingDelegate& instance() {
    static PairingDelegate delegate;
    return delegate;
  }

private:
  static chip::Controller::DeviceCommissioner* commissioner() {
    return esp_matter::controller::matter_controller_client::get_instance().get_commissioner();
  }
};

esp_err_t findCommissionablePeer(const chip::SetupPayload& payload, std::uint8_t address_type,
                                 chip::Inet::IPAddress& peer_ip, std::uint16_t& peer_port) {
  mdns_result_t* results = nullptr;
  const esp_err_t query_result = mdns_query_ptr("_matterc", "_udp", 5000, 16, &results);
  if (query_result != ESP_OK)
    return query_result;

  bool found = false;
  for (const mdns_result_t* result = results; result != nullptr && !found; result = result->next) {
    std::uint32_t discriminator = 0;
    std::uint32_t commissioning_mode = 0;
    if (result->port == 0 || !txtNumber(*result, "D", discriminator) ||
        !txtNumber(*result, "CM", commissioning_mode) || commissioning_mode == 0 ||
        (discriminator >> 8U) != payload.discriminator.GetShortValue()) {
      continue;
    }
    for (const mdns_ip_addr_t* address = result->addr; address != nullptr;
         address = address->next) {
      if (address->addr.type != address_type)
        continue;
      char text[40]{};
      if (address_type == ESP_IPADDR_TYPE_V4) {
        if (esp_ip4addr_ntoa(&address->addr.u_addr.ip4, text, sizeof(text)) == nullptr)
          continue;
      } else {
        if (address->addr.u_addr.ip6.zone != 0)
          continue;
        std::snprintf(text, sizeof(text), IPV6STR, IPV62STR(address->addr.u_addr.ip6));
      }
      if (chip::Inet::IPAddress::FromString(text, peer_ip)) {
        peer_port = result->port;
        found = true;
        break;
      }
    }
  }
  mdns_query_results_free(results);
  return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t pairThreadOnNetwork(chip::NodeId node_id, const char* setup_payload) {
  chip::SetupPayload payload;
  if (chip::ManualSetupPayloadParser(setup_payload).populatePayload(payload) != CHIP_NO_ERROR)
    return ESP_ERR_INVALID_ARG;

  chip::Inet::IPAddress peer_ip;
  std::uint16_t peer_port = 0;
  const esp_err_t discovery =
      findCommissionablePeer(payload, ESP_IPADDR_TYPE_V6, peer_ip, peer_port);
  if (discovery != ESP_OK)
    return discovery;

  chip::RendezvousParameters rendezvous =
      chip::RendezvousParameters()
          .SetSetupPINCode(payload.setUpPINCode)
          .SetPeerAddress(chip::Transport::PeerAddress::UDP(peer_ip, peer_port));
  chip::Controller::CommissioningParameters parameters;

  esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
  auto* commissioner =
      esp_matter::controller::matter_controller_client::get_instance().get_commissioner();
  commissioner->RegisterPairingDelegate(&PairingDelegate::instance());
  const CHIP_ERROR error = commissioner->PairDevice(node_id, rendezvous, parameters);
  if (error != CHIP_NO_ERROR) {
    commissioner->RegisterPairingDelegate(nullptr);
    return error == CHIP_ERROR_NO_MEMORY ? ESP_ERR_NO_MEM : ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t pairThreadOverBle(chip::NodeId node_id, const char* setup_payload, std::uint8_t* dataset,
                            std::uint8_t dataset_length) {
  chip::SetupPayload payload;
  if (chip::ManualSetupPayloadParser(setup_payload).populatePayload(payload) != CHIP_NO_ERROR) {
    return ESP_ERR_INVALID_ARG;
  }

  chip::RendezvousParameters rendezvous = chip::RendezvousParameters()
                                              .SetSetupPINCode(payload.setUpPINCode)
                                              .SetSetupDiscriminator(payload.discriminator)
                                              .SetPeerAddress(chip::Transport::PeerAddress::BLE());
  chip::Controller::CommissioningParameters parameters =
      chip::Controller::CommissioningParameters().SetThreadOperationalDataset(
          chip::ByteSpan(dataset, dataset_length));

  esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
  auto* commissioner =
      esp_matter::controller::matter_controller_client::get_instance().get_commissioner();
  commissioner->RegisterPairingDelegate(&PairingDelegate::instance());
  const CHIP_ERROR error = commissioner->PairDevice(node_id, rendezvous, parameters);
  if (error != CHIP_NO_ERROR) {
    commissioner->RegisterPairingDelegate(nullptr);
    return error == CHIP_ERROR_NO_MEMORY ? ESP_ERR_NO_MEM : ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t pairWifiOnNetwork(chip::NodeId node_id, const char* ssid, const char* password,
                            const char* setup_payload) {
  chip::SetupPayload payload;
  if (chip::ManualSetupPayloadParser(setup_payload).populatePayload(payload) != CHIP_NO_ERROR) {
    return ESP_ERR_INVALID_ARG;
  }

  chip::Inet::IPAddress peer_ip;
  std::uint16_t peer_port = 0;
  const esp_err_t discovery =
      findCommissionablePeer(payload, ESP_IPADDR_TYPE_V4, peer_ip, peer_port);
  if (discovery != ESP_OK)
    return discovery;

  chip::ByteSpan ssid_span(reinterpret_cast<const std::uint8_t*>(ssid), std::strlen(ssid));
  chip::ByteSpan password_span(reinterpret_cast<const std::uint8_t*>(password),
                               std::strlen(password));
  chip::Controller::CommissioningParameters parameters =
      chip::Controller::CommissioningParameters().SetWiFiCredentials(
          chip::Controller::WiFiCredentials(ssid_span, password_span));
  chip::RendezvousParameters rendezvous =
      chip::RendezvousParameters()
          .SetSetupPINCode(payload.setUpPINCode)
          .SetPeerAddress(chip::Transport::PeerAddress::UDP(peer_ip, peer_port));

  esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
  auto* commissioner =
      esp_matter::controller::matter_controller_client::get_instance().get_commissioner();
  commissioner->RegisterPairingDelegate(&PairingDelegate::instance());
  const CHIP_ERROR error = commissioner->PairDevice(node_id, rendezvous, parameters);
  if (error != CHIP_NO_ERROR) {
    commissioner->RegisterPairingDelegate(nullptr);
    return error == CHIP_ERROR_NO_MEMORY ? ESP_ERR_NO_MEM : ESP_FAIL;
  }
  return ESP_OK;
}

std::uint64_t controllerNodeId() {
  std::uint8_t mac[6]{};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    return 0;
  }
  std::uint64_t node_id = 0;
  for (const auto octet : mac) {
    node_id = (node_id << 8U) | octet;
  }
  return 0x0001000000000000ULL | node_id;
}

esp_err_t initializeBorderRouter() {
  if (border_router_initialized.load()) {
    return ESP_OK;
  }
  esp_netif_t* backbone = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (backbone == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_netif_ip_info_t ip_info{};
  if (esp_netif_get_ip_info(backbone, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_openthread_set_backbone_netif(backbone);
  if (!esp_openthread_lock_acquire(pdMS_TO_TICKS(2000))) {
    return ESP_ERR_TIMEOUT;
  }
  const esp_err_t result = esp_openthread_border_router_init();
  esp_openthread_lock_release();
  if (result == ESP_OK) {
    border_router_initialized.store(true);
    ESP_LOGI(kTag, "Thread border router initialized");
  }
  return result;
}

void matterEvent(const ChipDeviceEvent* event, intptr_t) {
  if (event == nullptr || border_router_initialized.load() || controller_initializing.load() ||
      event->Type != chip::DeviceLayer::DeviceEventType::kESPSystemEvent ||
      event->Platform.ESPSystemEvent.Base != IP_EVENT ||
      event->Platform.ESPSystemEvent.Id != IP_EVENT_STA_GOT_IP) {
    return;
  }

  const esp_err_t result = initializeBorderRouter();
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "Thread border router init failed: %s", esp_err_to_name(result));
  }
}

} // namespace

CoreMatter::~CoreMatter() {
  clearSecrets();
  mbedtls_platform_zeroize(command_slots_, sizeof(command_slots_));
  mbedtls_platform_zeroize(command_slot_used_, sizeof(command_slot_used_));
  mbedtls_platform_zeroize(queue_buffer_, sizeof(queue_buffer_));
}

void CoreMatter::clearSecrets() {
  mbedtls_platform_zeroize(wifi_ssid_, sizeof(wifi_ssid_));
  mbedtls_platform_zeroize(wifi_password_, sizeof(wifi_password_));
  mbedtls_platform_zeroize(thread_dataset_, sizeof(thread_dataset_));
  wifi_ssid_length_ = 0;
  wifi_password_length_ = 0;
  thread_dataset_length_ = 0;
}

esp_err_t CoreMatter::enqueueCommand(const Command& command) {
  if (queue_ == nullptr)
    return ESP_ERR_INVALID_STATE;
  std::size_t slot = kQueueDepth;
  taskENTER_CRITICAL(&status_lock_);
  for (std::size_t index = 0; index < kQueueDepth; ++index) {
    if (!command_slot_used_[index]) {
      command_slots_[index] = command;
      command_slot_used_[index] = true;
      slot = index;
      break;
    }
  }
  taskEXIT_CRITICAL(&status_lock_);
  if (slot == kQueueDepth)
    return ESP_ERR_TIMEOUT;
  const QueueMessage message{static_cast<std::uint8_t>(slot)};
  if (xQueueSend(queue_, &message, 0) == pdTRUE)
    return ESP_OK;
  taskENTER_CRITICAL(&status_lock_);
  mbedtls_platform_zeroize(&command_slots_[slot], sizeof(command_slots_[slot]));
  command_slot_used_[slot] = false;
  taskEXIT_CRITICAL(&status_lock_);
  return ESP_ERR_TIMEOUT;
}

bool CoreMatter::receiveCommand(Command& command, TickType_t timeout) {
  QueueMessage message{};
  if (xQueueReceive(queue_, &message, timeout) != pdTRUE || message.slot >= kQueueDepth)
    return false;
  taskENTER_CRITICAL(&status_lock_);
  if (!command_slot_used_[message.slot]) {
    taskEXIT_CRITICAL(&status_lock_);
    return false;
  }
  command = command_slots_[message.slot];
  mbedtls_platform_zeroize(&command_slots_[message.slot], sizeof(command_slots_[message.slot]));
  command_slot_used_[message.slot] = false;
  taskEXIT_CRITICAL(&status_lock_);
  return true;
}

esp_err_t CoreMatter::begin(const char* wifi_ssid, std::size_t wifi_ssid_length,
                            const char* wifi_password, std::size_t wifi_password_length,
                            const std::uint8_t* thread_dataset, std::size_t thread_dataset_length) {
  if (task_ != nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  if (wifi_ssid == nullptr || wifi_password == nullptr || wifi_ssid_length == 0 ||
      wifi_ssid_length >= sizeof(wifi_ssid_) || wifi_password_length >= sizeof(wifi_password_) ||
      thread_dataset_length > sizeof(thread_dataset_) ||
      (thread_dataset_length != 0 && thread_dataset == nullptr)) {
    return ESP_ERR_INVALID_ARG;
  }
  std::memcpy(wifi_ssid_, wifi_ssid, wifi_ssid_length);
  std::memcpy(wifi_password_, wifi_password, wifi_password_length);
  wifi_ssid_length_ = wifi_ssid_length;
  wifi_password_length_ = wifi_password_length;
  if (thread_dataset_length != 0) {
    std::memcpy(thread_dataset_, thread_dataset, thread_dataset_length);
    thread_dataset_length_ = static_cast<std::uint8_t>(thread_dataset_length);
  }
  ESP_LOGI(kTag, "heap before Matter: internal free=%u largest=%u, DMA free=%u largest=%u",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(
               heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));

  terminal_signal_ =
      xSemaphoreCreateCountingStatic(kTerminalCommandDepth, 0, &terminal_signal_storage_);
  queue_ = xQueueCreateStatic(kQueueDepth, sizeof(QueueMessage), queue_buffer_, &queue_storage_);
  if (terminal_signal_ == nullptr || queue_ == nullptr) {
    clearSecrets();
    return ESP_ERR_NO_MEM;
  }
  taskENTER_CRITICAL(&status_lock_);
  status_.state = State::starting;
  taskEXIT_CRITICAL(&status_lock_);
  if (xTaskCreatePinnedToCore(taskEntry, "matter_owner", 8192, this, 5, &task_, 1) != pdPASS) {
    taskENTER_CRITICAL(&status_lock_);
    status_.state = State::failed;
    status_.last_error = ESP_ERR_NO_MEM;
    taskEXIT_CRITICAL(&status_lock_);
    clearSecrets();
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

esp_err_t CoreMatter::commission(CommissioningTransport transport, const char* setup_payload,
                                 std::uint32_t& operation_id) {
  operation_id = 0;
  if (setup_payload == nullptr)
    return ESP_ERR_INVALID_ARG;
  const std::size_t length = std::strlen(setup_payload);
  if (length < 11 || length >= sizeof(Command{}.setup_payload)) {
    return ESP_ERR_INVALID_ARG;
  }
  for (std::size_t index = 0; index < length; ++index) {
    if (setup_payload[index] < '0' || setup_payload[index] > '9') {
      return ESP_ERR_INVALID_ARG;
    }
  }

  Command command{.type = CommandType::commission, .transport = transport};
  taskENTER_CRITICAL(&status_lock_);
  if (transport == CommissioningTransport::thread) {
    if (thread_dataset_length_ == 0) {
      taskEXIT_CRITICAL(&status_lock_);
      mbedtls_platform_zeroize(&command, sizeof(command));
      return ESP_ERR_INVALID_ARG;
    }
    command.dataset_length = thread_dataset_length_;
    std::memcpy(command.dataset, thread_dataset_, command.dataset_length);
  }
  const bool ready = status_.state == State::ready;
  const auto current_commissioning_state = commissioning_state.load();
  const bool busy = status_.commissioning_state == CommissioningState::queued ||
                    current_commissioning_state == CommissioningState::pase ||
                    current_commissioning_state == CommissioningState::cancelling;
  if (!ready || busy) {
    taskEXIT_CRITICAL(&status_lock_);
    mbedtls_platform_zeroize(&command, sizeof(command));
    return ESP_ERR_INVALID_STATE;
  }
  operation_id = next_operation_id_++;
  if (next_operation_id_ == 0) {
    next_operation_id_ = 1;
  }
  status_.operation_id = operation_id;
  status_.commissioning_transport = transport;
  status_.commissioning_state = CommissioningState::queued;
  status_.last_error = ESP_OK;
  command.operation_id = operation_id;
  taskEXIT_CRITICAL(&status_lock_);

  std::memcpy(command.setup_payload, setup_payload, length + 1);
  if (enqueueCommand(command) != ESP_OK) {
    taskENTER_CRITICAL(&status_lock_);
    status_.commissioning_state = CommissioningState::failed;
    status_.last_error = ESP_ERR_TIMEOUT;
    taskEXIT_CRITICAL(&status_lock_);
    mbedtls_platform_zeroize(&command, sizeof(command));
    return ESP_ERR_TIMEOUT;
  }
  mbedtls_platform_zeroize(&command, sizeof(command));
  return ESP_OK;
}

esp_err_t CoreMatter::applyThreadDataset(const std::uint8_t* dataset, std::size_t length) {
  if (dataset == nullptr || length == 0 || length > sizeof(thread_dataset_) || queue_ == nullptr)
    return ESP_ERR_INVALID_ARG;
  Command command{.type = CommandType::apply_thread_dataset,
                  .dataset_length = static_cast<std::uint8_t>(length)};
  std::memcpy(command.dataset, dataset, length);
  const esp_err_t result = enqueueCommand(command);
  mbedtls_platform_zeroize(&command, sizeof(command));
  return result;
}

esp_err_t CoreMatter::cancelCommissioning(std::uint32_t operation_id) {
  if (operation_id == 0 || queue_ == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  taskENTER_CRITICAL(&status_lock_);
  const bool matches = status_.operation_id == operation_id;
  const auto state = commissioning_state.load();
  const bool active = status_.commissioning_state == CommissioningState::queued ||
                      state == CommissioningState::pase || state == CommissioningState::cancelling;
  taskEXIT_CRITICAL(&status_lock_);
  if (!matches || !active) {
    return ESP_ERR_INVALID_STATE;
  }
  const Command command{.type = CommandType::cancel_commissioning, .operation_id = operation_id};
  return enqueueCommand(command);
}

esp_err_t CoreMatter::enqueueDeviceCommand(Command command, std::uint32_t& operation_id,
                                           CommandRejection* rejection) {
  operation_id = 0;
  if (rejection != nullptr)
    *rejection = CommandRejection::none;
  taskENTER_CRITICAL(&status_lock_);
  const bool ready = status_.state == State::ready;
  const auto current_commissioning_state = commissioning_state.load();
  const bool commissioning_busy = status_.commissioning_state == CommissioningState::queued ||
                                  current_commissioning_state == CommissioningState::pase ||
                                  current_commissioning_state == CommissioningState::cancelling;
  const bool command_busy = command_status_.state == CommandState::queued ||
                            command_status_.state == CommandState::invoking;
  if (!ready || commissioning_busy || command_busy) {
    if (rejection != nullptr) {
      *rejection = !ready               ? CommandRejection::matter_not_ready
                   : commissioning_busy ? CommandRejection::commissioning_busy
                                        : CommandRejection::command_busy;
    }
    taskEXIT_CRITICAL(&status_lock_);
    return ESP_ERR_INVALID_STATE;
  }
  operation_id = next_command_operation_id_++;
  if (next_command_operation_id_ == 0) {
    next_command_operation_id_ = 1;
  }
  command.operation_id = operation_id;
  CommandKind kind = CommandKind::none;
  switch (command.type) {
  case CommandType::set_on_off:
    kind = CommandKind::set_on_off;
    break;
  case CommandType::set_level:
    kind = CommandKind::set_level;
    break;
  case CommandType::set_color_temperature:
    kind = CommandKind::set_color_temperature;
    break;
  case CommandType::unpair_device:
    kind = CommandKind::unpair_device;
    break;
  default:
    break;
  }
  command_status_ = {
      kind,  CommandState::queued, operation_id, command.node_id, command.endpoint_id, command.on,
      ESP_OK};
  taskEXIT_CRITICAL(&status_lock_);

  if (enqueueCommand(command) != ESP_OK) {
    finishCommand(operation_id, CommandState::failed, ESP_ERR_TIMEOUT);
    operation_id = 0;
    if (rejection != nullptr)
      *rejection = CommandRejection::queue_full;
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

esp_err_t CoreMatter::setOnOff(std::uint64_t node_id, std::uint16_t endpoint_id, bool on,
                               std::uint32_t& operation_id, CommandRejection* rejection) {
  if (node_id == 0 || endpoint_id == 0 || queue_ == nullptr) {
    operation_id = 0;
    return ESP_ERR_INVALID_ARG;
  }
  return enqueueDeviceCommand(Command{.type = CommandType::set_on_off,
                                      .node_id = node_id,
                                      .endpoint_id = endpoint_id,
                                      .on = on},
                              operation_id, rejection);
}

esp_err_t CoreMatter::setLevel(std::uint64_t node_id, std::uint16_t endpoint_id, std::uint8_t level,
                               std::uint16_t transition_time_ds, std::uint32_t& operation_id,
                               CommandRejection* rejection) {
  if (node_id == 0 || endpoint_id == 0 || queue_ == nullptr || level > 254 ||
      transition_time_ds > kMaxTransitionTimeDs) {
    operation_id = 0;
    return ESP_ERR_INVALID_ARG;
  }
  return enqueueDeviceCommand(Command{.type = CommandType::set_level,
                                      .node_id = node_id,
                                      .endpoint_id = endpoint_id,
                                      .value = level,
                                      .transition_time_ds = transition_time_ds,
                                      .on = level != 0},
                              operation_id, rejection);
}

esp_err_t CoreMatter::setColorTemperature(std::uint64_t node_id, std::uint16_t endpoint_id,
                                          std::uint16_t color_temperature_mireds,
                                          std::uint16_t transition_time_ds,
                                          std::uint32_t& operation_id,
                                          CommandRejection* rejection) {
  if (node_id == 0 || endpoint_id == 0 || queue_ == nullptr ||
      color_temperature_mireds < kMinColorTemperatureMireds ||
      color_temperature_mireds > kMaxColorTemperatureMireds ||
      transition_time_ds > kMaxTransitionTimeDs) {
    operation_id = 0;
    return ESP_ERR_INVALID_ARG;
  }
  return enqueueDeviceCommand(Command{.type = CommandType::set_color_temperature,
                                      .node_id = node_id,
                                      .endpoint_id = endpoint_id,
                                      .value = color_temperature_mireds,
                                      .transition_time_ds = transition_time_ds},
                              operation_id, rejection);
}

esp_err_t CoreMatter::unpairDevice(std::uint64_t node_id, std::uint32_t& operation_id,
                                   CommandRejection* rejection) {
  if (node_id == 0 || queue_ == nullptr) {
    operation_id = 0;
    if (rejection != nullptr)
      *rejection =
          node_id == 0 ? CommandRejection::invalid_request : CommandRejection::matter_not_ready;
    return node_id == 0 ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE;
  }
  return enqueueDeviceCommand(Command{.type = CommandType::unpair_device, .node_id = node_id},
                              operation_id, rejection);
}

esp_err_t CoreMatter::discoverEndpoints(std::uint64_t node_id) {
  if (node_id == 0 || queue_ == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  taskENTER_CRITICAL(&status_lock_);
  if (status_.state != State::ready || discovery_status_.active) {
    taskEXIT_CRITICAL(&status_lock_);
    return ESP_ERR_INVALID_STATE;
  }
  discovery_status_ = {};
  discovery_status_.active = true;
  discovery_status_.operation_id = next_discovery_operation_id_++;
  discovery_status_.last_error = ESP_OK;
  const std::uint32_t operation_id = discovery_status_.operation_id;
  discovery_started_us_ = esp_timer_get_time();
  taskEXIT_CRITICAL(&status_lock_);
  const Command command{
      .type = CommandType::discover_endpoints, .operation_id = operation_id, .node_id = node_id};
  if (enqueueCommand(command) != ESP_OK) {
    taskENTER_CRITICAL(&status_lock_);
    discovery_status_.active = false;
    discovery_status_.last_error = ESP_ERR_TIMEOUT;
    taskEXIT_CRITICAL(&status_lock_);
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

esp_err_t CoreMatter::subscribeOnOff(std::uint64_t node_id) {
  if (node_id == 0 || queue_ == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  const Command command{.type = CommandType::subscribe_on_off, .node_id = node_id};
  return enqueueCommand(command);
}

CoreMatter::DiscoveryStatus CoreMatter::discoveryStatus() const {
  taskENTER_CRITICAL(const_cast<portMUX_TYPE*>(&status_lock_));
  const DiscoveryStatus snapshot = discovery_status_;
  taskEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&status_lock_));
  return snapshot;
}

CoreMatter::OnOffState CoreMatter::onOffState() const {
  const bool known = reported_on_off_known.load();
  return {.node_id = known ? reported_on_off_node_id.load() : 0,
          .endpoint_id = static_cast<std::uint16_t>(known ? reported_on_off_endpoint_id.load() : 0),
          .known = known,
          .on = known && reported_on_off_value.load()};
}

CoreMatter::CommandStatus CoreMatter::commandStatus() const {
  taskENTER_CRITICAL(const_cast<portMUX_TYPE*>(&status_lock_));
  const CommandStatus snapshot = command_status_;
  taskEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&status_lock_));
  return snapshot;
}

bool CoreMatter::terminalCommand(std::uint32_t operation_id, TerminalCommand& terminal) const {
  taskENTER_CRITICAL(const_cast<portMUX_TYPE*>(&status_lock_));
  const bool found = terminal_commands_.find(operation_id, terminal);
  taskEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&status_lock_));
  return found;
}

std::size_t CoreMatter::terminalCommandsAfter(std::uint64_t after_sequence,
                                              TerminalCommand* terminals,
                                              std::size_t capacity) const {
  taskENTER_CRITICAL(const_cast<portMUX_TYPE*>(&status_lock_));
  const std::size_t count = terminal_commands_.after(after_sequence, terminals, capacity);
  taskEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&status_lock_));
  return count;
}

esp_err_t CoreMatter::waitForTerminalCommand(std::uint32_t operation_id, TickType_t timeout,
                                             TerminalCommand& terminal) {
  if (operation_id == 0 || terminal_signal_ == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  const TickType_t started = xTaskGetTickCount();
  while (!terminalCommand(operation_id, terminal)) {
    TickType_t remaining = portMAX_DELAY;
    if (timeout != portMAX_DELAY) {
      const TickType_t elapsed = xTaskGetTickCount() - started;
      if (elapsed >= timeout) {
        return ESP_ERR_TIMEOUT;
      }
      remaining = timeout - elapsed;
    }
    if (xSemaphoreTake(terminal_signal_, remaining) != pdTRUE) {
      return ESP_ERR_TIMEOUT;
    }
  }
  return ESP_OK;
}

bool CoreMatter::finishCommand(std::uint32_t operation_id, CommandState state, esp_err_t error) {
  bool appended = false;
  taskENTER_CRITICAL(&status_lock_);
  if (operation_id != 0 && command_status_.operation_id == operation_id &&
      (command_status_.state == CommandState::queued ||
       command_status_.state == CommandState::invoking)) {
    command_status_.state = state;
    command_status_.last_error = error;
    const TerminalCommand terminal{next_terminal_sequence_, command_status_};
    appended = terminal_commands_.append(operation_id, terminal);
    if (appended) {
      ++next_terminal_sequence_;
      if (next_terminal_sequence_ == 0) {
        next_terminal_sequence_ = 1;
      }
    }
    if (invoke_callback_context_.operation_id == operation_id) {
      invoke_callback_context_.operation_id = 0;
    }
    if (active_unpair_operation_id_ == operation_id) {
      active_unpair_operation_id_ = 0;
    }
  }
  taskEXIT_CRITICAL(&status_lock_);
  if (appended && terminal_signal_ != nullptr) {
    static_cast<void>(xSemaphoreGive(terminal_signal_));
  }
  return appended;
}

CoreMatter::Status CoreMatter::status() const {
  taskENTER_CRITICAL(const_cast<portMUX_TYPE*>(&status_lock_));
  Status snapshot = status_;
  snapshot.border_router_ready = border_router_initialized.load();
  taskEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&status_lock_));
  snapshot.subscriptions_active = on_off_subscription_active.load() ? 1 : 0;
  snapshot.case_ready = snapshot.subscriptions_active != 0;
  const auto callback_state = commissioning_state.load();
  if (callback_state != CommissioningState::idle) {
    snapshot.commissioning_state = callback_state;
    snapshot.commissioned_node_id = commissioned_node_id.load();
    snapshot.last_error = commissioning_error.load();
  }
  return snapshot;
}

void CoreMatter::taskEntry(void* context) {
  static_cast<CoreMatter*>(context)->run();
}

void CoreMatter::onUnpairComplete(std::uint64_t node_id, CHIP_ERROR error) {
  CoreMatter* matter = callback_owner_;
  if (matter == nullptr) {
    return;
  }
  taskENTER_CRITICAL(&matter->status_lock_);
  const std::uint32_t operation_id = matter->active_unpair_operation_id_;
  const bool current = operation_id != 0 && matter->command_status_.operation_id == operation_id &&
                       matter->command_status_.kind == CommandKind::unpair_device &&
                       matter->command_status_.state == CommandState::invoking &&
                       matter->command_status_.node_id == node_id;
  taskEXIT_CRITICAL(&matter->status_lock_);
  if (current) {
    matter->finishCommand(operation_id,
                          error == CHIP_NO_ERROR ? CommandState::complete : CommandState::failed,
                          error == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL);
  }
}

esp_err_t CoreMatter::applyThreadDatasetOnOwner(const std::uint8_t* dataset, std::size_t length) {
  if (dataset == nullptr || length == 0 || length > sizeof(thread_dataset_))
    return ESP_ERR_INVALID_ARG;
  if (!esp_openthread_lock_acquire(pdMS_TO_TICKS(2000)))
    return ESP_ERR_TIMEOUT;
  otInstance* instance = esp_openthread_get_instance();
  if (instance == nullptr) {
    esp_openthread_lock_release();
    return ESP_ERR_INVALID_STATE;
  }

  const bool thread_enabled = otThreadGetDeviceRole(instance) != OT_DEVICE_ROLE_DISABLED;
  const bool ip6_enabled = otIp6IsEnabled(instance);
  otError error = thread_enabled ? otThreadSetEnabled(instance, false) : OT_ERROR_NONE;
  otOperationalDatasetTlvs active{};
  if (error == OT_ERROR_NONE) {
    active.mLength = static_cast<std::uint8_t>(length);
    std::memcpy(active.mTlvs, dataset, length);
    error = otDatasetSetActiveTlvs(instance, &active);
  }
  if (error == OT_ERROR_NONE && !ip6_enabled)
    error = otIp6SetEnabled(instance, true);
  if (error == OT_ERROR_NONE)
    error = otThreadSetEnabled(instance, true);
  if (error != OT_ERROR_NONE && thread_enabled)
    static_cast<void>(otThreadSetEnabled(instance, true));
  esp_openthread_lock_release();
  mbedtls_platform_zeroize(&active, sizeof(active));

  const esp_err_t result = error == OT_ERROR_NONE      ? ESP_OK
                           : error == OT_ERROR_NO_BUFS ? ESP_ERR_NO_MEM
                           : error == OT_ERROR_INVALID_ARGS || error == OT_ERROR_PARSE
                               ? ESP_ERR_INVALID_ARG
                           : error == OT_ERROR_INVALID_STATE ? ESP_ERR_INVALID_STATE
                                                             : ESP_FAIL;
  taskENTER_CRITICAL(&status_lock_);
  status_.thread_dataset_configured = result == ESP_OK;
  status_.last_error = result;
  if (result == ESP_OK) {
    std::memcpy(thread_dataset_, dataset, length);
    thread_dataset_length_ = static_cast<std::uint8_t>(length);
  }
  taskEXIT_CRITICAL(&status_lock_);
  return result;
}

void CoreMatter::refreshThreadStatus() {
  if (!esp_openthread_lock_acquire(0))
    return;
  otInstance* instance = esp_openthread_get_instance();
  const otDeviceRole role =
      instance == nullptr ? OT_DEVICE_ROLE_DISABLED : otThreadGetDeviceRole(instance);
  esp_openthread_lock_release();
  const bool attached = role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER ||
                        role == OT_DEVICE_ROLE_LEADER;
  taskENTER_CRITICAL(&status_lock_);
  status_.thread_attached = attached;
  taskEXIT_CRITICAL(&status_lock_);
}

void CoreMatter::run() {
  const esp_err_t startup_result = startController();
  taskENTER_CRITICAL(&status_lock_);
  status_.state = startup_result == ESP_OK ? State::ready : State::failed;
  status_.commissioner_ready = startup_result == ESP_OK;
  status_.border_router_ready = border_router_initialized.load();
  status_.last_error = startup_result;
  taskEXIT_CRITICAL(&status_lock_);
  if (startup_result != ESP_OK) {
    ESP_LOGE(kTag, "Matter owner startup failed: %s", esp_err_to_name(startup_result));
    clearSecrets();
    mbedtls_platform_zeroize(command_slots_, sizeof(command_slots_));
    mbedtls_platform_zeroize(command_slot_used_, sizeof(command_slot_used_));
    task_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  Command command{};
  while (true) {
    mbedtls_platform_zeroize(&command, sizeof(command));
    if (!receiveCommand(command, pdMS_TO_TICKS(250))) {
      refreshThreadStatus();
      if (commissioning_state.load() == CommissioningState::pase && active_node_id_ != 0 &&
          esp_timer_get_time() - commissioning_started_us.load() > kCommissioningTimeoutUs) {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        auto* commissioner =
            esp_matter::controller::matter_controller_client::get_instance().get_commissioner();
        cancellation_reason.store(2);
        const CHIP_ERROR result = commissioner->StopPairing(active_node_id_);
        if (result != CHIP_NO_ERROR) {
          cancellation_reason.store(0);
          commissioner->RegisterPairingDelegate(nullptr);
          commissioner->RegisterDeviceDiscoveryDelegate(nullptr);
          commissioning_error.store(ESP_FAIL);
          commissioning_state.store(CommissioningState::failed);
          active_node_id_ = 0;
        }
      }
      taskENTER_CRITICAL(&status_lock_);
      if (discovery_status_.active &&
          esp_timer_get_time() - discovery_started_us_ > kDiscoveryTimeoutUs) {
        discovery_status_.active = false;
        discovery_status_.complete = false;
        discovery_status_.last_error = ESP_ERR_TIMEOUT;
      }
      taskEXIT_CRITICAL(&status_lock_);
      continue;
    }
    if (command.type == CommandType::stop) {
      break;
    }
    if (command.type == CommandType::cancel_commissioning) {
      taskENTER_CRITICAL(&status_lock_);
      const bool matches = command.operation_id == status_.operation_id;
      taskEXIT_CRITICAL(&status_lock_);
      if (!matches || active_node_id_ == 0) {
        continue;
      }
      esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
      auto* commissioner =
          esp_matter::controller::matter_controller_client::get_instance().get_commissioner();
      cancellation_reason.store(1);
      const CHIP_ERROR result = commissioner->StopPairing(active_node_id_);
      if (result == CHIP_NO_ERROR) {
        commissioning_state.store(CommissioningState::cancelling);
      } else {
        cancellation_reason.store(0);
        commissioner->RegisterPairingDelegate(nullptr);
        commissioner->RegisterDeviceDiscoveryDelegate(nullptr);
        commissioning_error.store(ESP_FAIL);
        commissioning_state.store(CommissioningState::failed);
        active_node_id_ = 0;
      }
      continue;
    }
    if (command.type == CommandType::apply_thread_dataset) {
      const esp_err_t result = applyThreadDatasetOnOwner(command.dataset, command.dataset_length);
      mbedtls_platform_zeroize(&command, sizeof(command));
      if (result != ESP_OK)
        ESP_LOGE(kTag, "failed to apply active Thread dataset: %s", esp_err_to_name(result));
      continue;
    }
    if (command.type == CommandType::discover_endpoints) {
      const std::uint32_t operation_id = command.operation_id;
      const auto attribute_callback =
          [this,
           operation_id](std::uint64_t node_id, const chip::app::ConcreteDataAttributePath& path,
                         chip::TLV::TLVReader* data, const chip::app::StatusIB& response_status) {
            if (response_status.ToChipError() != CHIP_NO_ERROR || data == nullptr ||
                path.mEndpointId == 0) {
              return;
            }
            chip::TLV::TLVReader reader = *data;
            std::uint32_t capabilities = 0;
            if (reader.GetType() == chip::TLV::kTLVType_Array) {
              chip::TLV::TLVType container;
              if (reader.EnterContainer(container) != CHIP_NO_ERROR) {
                return;
              }
              while (reader.Next() == CHIP_NO_ERROR) {
                std::uint32_t cluster_id = 0;
                if (reader.Get(cluster_id) == CHIP_NO_ERROR) {
                  capabilities |= capabilityForCluster(cluster_id);
                }
              }
            } else {
              std::uint32_t cluster_id = 0;
              if (reader.Get(cluster_id) != CHIP_NO_ERROR) {
                return;
              }
              capabilities = capabilityForCluster(cluster_id);
            }
            taskENTER_CRITICAL(&status_lock_);
            if (!discovery_status_.active || discovery_status_.operation_id != operation_id) {
              taskEXIT_CRITICAL(&status_lock_);
              return;
            }
            for (std::size_t index = 0; index < discovery_status_.count; ++index) {
              auto& endpoint = discovery_status_.endpoints[index];
              if (endpoint.node_id == node_id && endpoint.endpoint_id == path.mEndpointId) {
                endpoint.capabilities |= capabilities;
                taskEXIT_CRITICAL(&status_lock_);
                return;
              }
            }
            if (discovery_status_.count < std::size(discovery_status_.endpoints)) {
              discovery_status_.endpoints[discovery_status_.count++] = {node_id, path.mEndpointId,
                                                                        capabilities};
            }
            taskEXIT_CRITICAL(&status_lock_);
          };
      const auto done_callback =
          [this, operation_id](
              std::uint64_t,
              const chip::Platform::ScopedMemoryBufferWithSize<chip::app::AttributePathParams>&,
              const chip::Platform::ScopedMemoryBufferWithSize<chip::app::EventPathParams>&) {
            taskENTER_CRITICAL(&status_lock_);
            if (discovery_status_.operation_id == operation_id) {
              discovery_status_.active = false;
              discovery_status_.complete = true;
              discovery_status_.last_error = ESP_OK;
            }
            taskEXIT_CRITICAL(&status_lock_);
          };
      const auto error_callback = [this, operation_id](std::uint64_t, CHIP_ERROR error) {
        taskENTER_CRITICAL(&status_lock_);
        if (discovery_status_.operation_id == operation_id) {
          discovery_status_.active = false;
          discovery_status_.complete = false;
          discovery_status_.last_error = error == CHIP_ERROR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
        }
        taskEXIT_CRITICAL(&status_lock_);
      };
      const auto connect_failure = [this, operation_id](void*, const chip::ScopedNodeId&,
                                                        CHIP_ERROR error) {
        taskENTER_CRITICAL(&status_lock_);
        if (discovery_status_.operation_id == operation_id) {
          discovery_status_.active = false;
          discovery_status_.complete = false;
          discovery_status_.last_error = error == CHIP_ERROR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
        }
        taskEXIT_CRITICAL(&status_lock_);
      };
      esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
      auto* request = chip::Platform::New<esp_matter::controller::read_command>(
          command.node_id, 0xffff, 0x001d, 0x0001, esp_matter::controller::READ_ATTRIBUTE,
          attribute_callback, done_callback, nullptr, connect_failure, error_callback);
      const esp_err_t result = request == nullptr ? ESP_ERR_NO_MEM : request->send_command();
      if (result != ESP_OK) {
        taskENTER_CRITICAL(&status_lock_);
        discovery_status_.active = false;
        discovery_status_.last_error = result;
        taskEXIT_CRITICAL(&status_lock_);
      }
      continue;
    }
    if (command.type == CommandType::subscribe_on_off) {
      reported_on_off_known.store(false);
      esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
      auto* subscription = chip::Platform::New<esp_matter::controller::subscribe_command>(
          command.node_id, 0xffff, 0x0006, 0x0000, esp_matter::controller::SUBSCRIBE_ATTRIBUTE, 1,
          60, true, onOnOffReport, nullptr, onSubscriptionEstablished, onSubscriptionTerminated,
          nullptr, true);
      const esp_err_t result =
          subscription == nullptr ? ESP_ERR_NO_MEM : subscription->send_command();
      if (result != ESP_OK) {
        on_off_subscription_active.store(false);
        ESP_LOGE(kTag, "failed to subscribe to Matter OnOff state: %s", esp_err_to_name(result));
      }
      continue;
    }
    if (command.type == CommandType::unpair_device) {
      taskENTER_CRITICAL(&status_lock_);
      const bool current = command.operation_id == command_status_.operation_id &&
                           command_status_.kind == CommandKind::unpair_device;
      if (current) {
        command_status_.state = CommandState::invoking;
        active_unpair_operation_id_ = command.operation_id;
      }
      taskEXIT_CRITICAL(&status_lock_);
      if (!current) {
        continue;
      }
      esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
      const esp_err_t result =
          esp_matter::controller::matter_controller_client::get_instance().unpair(command.node_id,
                                                                                  onUnpairComplete);
      if (result != ESP_OK) {
        finishCommand(command.operation_id, CommandState::failed, result);
        ESP_LOGE(kTag, "failed to unpair Matter device: %s", esp_err_to_name(result));
      }
      continue;
    }
    if (command.type == CommandType::set_on_off || command.type == CommandType::set_level ||
        command.type == CommandType::set_color_temperature) {
      taskENTER_CRITICAL(&status_lock_);
      const bool current = command.operation_id == command_status_.operation_id &&
                           command_status_.kind != CommandKind::unpair_device;
      if (current) {
        command_status_.state = CommandState::invoking;
        invoke_callback_context_ = {this, command.operation_id};
      }
      taskEXIT_CRITICAL(&status_lock_);
      if (!current) {
        continue;
      }

      const auto on_success = [](void* context, const chip::app::ConcreteCommandPath&,
                                 const chip::app::StatusIB& response_status,
                                 chip::TLV::TLVReader*) {
        const auto* callback = static_cast<const InvokeCallbackContext*>(context);
        const CHIP_ERROR chip_error = response_status.ToChipError();
        callback->owner->finishCommand(
            callback->operation_id,
            chip_error == CHIP_NO_ERROR ? CommandState::complete : CommandState::failed,
            chip_error == CHIP_NO_ERROR ? ESP_OK : ESP_ERR_INVALID_RESPONSE);
      };
      const auto on_error = [](void* context, CHIP_ERROR error) {
        const auto* callback = static_cast<const InvokeCallbackContext*>(context);
        callback->owner->finishCommand(callback->operation_id, CommandState::failed,
                                       error == CHIP_ERROR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL);
      };
      const auto on_connect_failure = [](void* context, const chip::ScopedNodeId&,
                                         CHIP_ERROR error) {
        const auto* callback = static_cast<const InvokeCallbackContext*>(context);
        callback->owner->finishCommand(callback->operation_id, CommandState::failed,
                                       error == CHIP_ERROR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL);
      };

      std::uint32_t cluster_id = chip::app::Clusters::OnOff::Id;
      std::uint32_t command_id = command.on ? chip::app::Clusters::OnOff::Commands::On::Id
                                            : chip::app::Clusters::OnOff::Commands::Off::Id;
      const char* command_data = nullptr;
      char command_json[72]{};
      const char* command_name = "OnOff";
      if (command.type == CommandType::set_level) {
        cluster_id = chip::app::Clusters::LevelControl::Id;
        command_id = chip::app::Clusters::LevelControl::Commands::MoveToLevelWithOnOff::Id;
        std::snprintf(command_json, sizeof(command_json),
                      "{\"0:U8\":%u,\"1:U16\":%u,\"2:U8\":0,\"3:U8\":0}",
                      static_cast<unsigned>(command.value),
                      static_cast<unsigned>(command.transition_time_ds));
        command_data = command_json;
        command_name = "LevelControl";
      } else if (command.type == CommandType::set_color_temperature) {
        cluster_id = chip::app::Clusters::ColorControl::Id;
        command_id = chip::app::Clusters::ColorControl::Commands::MoveToColorTemperature::Id;
        std::snprintf(command_json, sizeof(command_json),
                      "{\"0:U16\":%u,\"1:U16\":%u,\"2:U8\":0,\"3:U8\":0}",
                      static_cast<unsigned>(command.value),
                      static_cast<unsigned>(command.transition_time_ds));
        command_data = command_json;
        command_name = "ColorControl";
      }

      esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
      const esp_err_t result = sendInvokeRequest(
          command.node_id, command.endpoint_id, cluster_id, command_id, command_data,
          &invoke_callback_context_, on_success, on_error, on_connect_failure);
      if (result != ESP_OK) {
        finishCommand(command.operation_id, CommandState::failed, result);
        ESP_LOGE(kTag, "failed to queue Matter %s command: %s", command_name,
                 esp_err_to_name(result));
      }
      continue;
    }
    if (command.type == CommandType::commission) {
      {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        auto* commissioner =
            esp_matter::controller::matter_controller_client::get_instance().get_commissioner();
        commissioner->RegisterPairingDelegate(nullptr);
        commissioner->RegisterDeviceDiscoveryDelegate(nullptr);
      }
      taskENTER_CRITICAL(&status_lock_);
      status_.commissioning_state = CommissioningState::pase;
      taskEXIT_CRITICAL(&status_lock_);
      commissioning_state.store(CommissioningState::pase);
      commissioning_error.store(ESP_OK);
      commissioning_started_us.store(esp_timer_get_time());
      std::uint64_t random_node_id = 0;
      do {
        random_node_id = (static_cast<std::uint64_t>(esp_random()) << 32U) | esp_random();
      } while (random_node_id == 0);
      const chip::NodeId node_id = 0x0002000000000000ULL | (random_node_id & 0x0000FFFFFFFFFFFFULL);
      active_node_id_ = node_id;
      esp_err_t result = ESP_OK;
      if (command.transport == CommissioningTransport::thread) {
        result = pairThreadOnNetwork(node_id, command.setup_payload);
        if (result == ESP_ERR_NOT_FOUND) {
          result = pairThreadOverBle(node_id, command.setup_payload, command.dataset,
                                     command.dataset_length);
        }
      } else {
        result =
            pairWifiOnNetwork(node_id, reinterpret_cast<const char*>(wifi_ssid_),
                              reinterpret_cast<const char*>(wifi_password_), command.setup_payload);
      }
      if (result != ESP_OK) {
        commissioning_error.store(result);
        commissioning_state.store(CommissioningState::failed);
        active_node_id_ = 0;
      }
      mbedtls_platform_zeroize(&command, sizeof(command));
    }
  }
  mbedtls_platform_zeroize(&command, sizeof(command));
  clearSecrets();
  task_ = nullptr;
  vTaskDelete(nullptr);
}

esp_err_t CoreMatter::startController() {
  auto thread_config = threadPlatformConfig();
  ESP_RETURN_ON_ERROR(set_openthread_platform_config(&thread_config), kTag,
                      "configure external RCP");
  controller_initializing.store(true);
  const esp_err_t start_result = esp_matter::start(matterEvent);
  if (start_result != ESP_OK) {
    controller_initializing.store(false);
    ESP_LOGE(kTag, "start Matter: %s", esp_err_to_name(start_result));
    return start_result;
  }

  const std::uint64_t node_id = controllerNodeId();
  if (node_id == 0) {
    controller_initializing.store(false);
    return ESP_FAIL;
  }
  esp_err_t controller_result = ESP_OK;
  {
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    auto& controller = esp_matter::controller::matter_controller_client::get_instance();
    controller_result = controller.init(node_id, kFabricId, kControllerPort);
    if (controller_result == ESP_OK)
      controller_result = controller.setup_commissioner();
    if (controller_result == ESP_OK) {
      const auto* fabric_table = controller.get_commissioner()->GetFabricTable();
      taskENTER_CRITICAL(&status_lock_);
      status_.fabric_configured = fabric_table != nullptr && fabric_table->FabricCount() != 0;
      taskEXIT_CRITICAL(&status_lock_);
      callback_owner_ = this;
    }
  }
  controller_initializing.store(false);
  ESP_RETURN_ON_ERROR(controller_result, kTag, "initialize commissioner");
  if (thread_dataset_length_ != 0) {
    ESP_RETURN_ON_ERROR(applyThreadDatasetOnOwner(thread_dataset_, thread_dataset_length_), kTag,
                        "apply active Thread dataset");
  }
  const esp_err_t border_router_result = initializeBorderRouter();
  if (border_router_result != ESP_OK && border_router_result != ESP_ERR_INVALID_STATE)
    return border_router_result;
  return ESP_OK;
}

} // namespace core_pilot
