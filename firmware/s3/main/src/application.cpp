#include "application.hpp"

#include "ai_chat_service.hpp"
#include "automation_service.hpp"
#include "bootloader_random.h"
#include "core_activity.hpp"
#include "core_ai.hpp"
#include "core_board.hpp"
#include "core_display.hpp"
#include "core_gateway.hpp"
#include "core_matter.hpp"
#include "core_network.hpp"
#include "core_security.hpp"
#include "core_storage.hpp"
#include "device_registry.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "http_access.hpp"
#include "http_server.hpp"
#include "lwip/ip4_addr.h"
#include "mbedtls/platform_util.h"
#include "mcp_service.hpp"
#include "model_repository.hpp"
#include "nvs_flash.h"
#include "ota_service.hpp"
#include "smtp_service.hpp"
#include "thread_dataset.hpp"
#include "wifi_profile.hpp"
#include "wifi_provision_service.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace core_pilot {
namespace {

constexpr char kTag[] = "core_pilot";

class EarlyEntropy final {
public:
  EarlyEntropy() {
    bootloader_random_enable();
  }
  ~EarlyEntropy() {
    disable();
  }
  void disable() {
    if (active_) {
      bootloader_random_disable();
      active_ = false;
    }
  }
  EarlyEntropy(const EarlyEntropy&) = delete;
  EarlyEntropy& operator=(const EarlyEntropy&) = delete;

private:
  bool active_{true};
};

bool generateHexCredential(core_security::RandomSource& random, char (&credential)[17]) {
  std::uint8_t bytes[8]{};
  if (random.fill(bytes, sizeof(bytes)) != core_security::Status::Ok)
    return false;
  static constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < sizeof(bytes); ++i) {
    credential[i * 2] = hex[bytes[i] >> 4U];
    credential[i * 2 + 1] = hex[bytes[i] & 0x0fU];
  }
  mbedtls_platform_zeroize(bytes, sizeof(bytes));
  return true;
}

[[noreturn]] void stopAtFatal(CoreDisplay& display, const CoreDisplay::FatalContent& content) {
  display.fatal(content);
  while (true) {
    display.poll();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void showProgress(CoreDisplay& display, std::uint8_t percent, const char* message) {
  display.bootProgress(percent, message);
  // Let the LCD transfer finish and make each transition perceptible.
  vTaskDelay(pdMS_TO_TICKS(40));
}

void networkBootProgress(void* context, const char* message) {
  showProgress(*static_cast<CoreDisplay*>(context), 50, message);
}

void showHome(CoreDisplay& display, const CoreNetwork& network, const char* setup_password) {
  const auto status = network.status();
  if (status.mode == CoreNetwork::Mode::setup_ap) {
    char ap_line[48]{};
    char password_line[48]{};
    std::snprintf(ap_line, sizeof(ap_line), "Setup AP: %s", status.setup_ssid);
    std::snprintf(password_line, sizeof(password_line), "Password: %s  192.168.4.1",
                  setup_password);
    display.home(ap_line, password_line, "");
    return;
  }

  char network_line[64]{};
  char address[IP4ADDR_STRLEN_MAX]{};
  char web_address[40]{};
  std::snprintf(network_line, sizeof(network_line), "Joined Wi-Fi: %s", status.station_ssid);
  if (status.has_ipv4 && esp_ip4addr_ntoa(&status.ipv4, address, sizeof(address)) != nullptr) {
    std::snprintf(web_address, sizeof(web_address), "http://%s/", address);
  } else {
    std::snprintf(web_address, sizeof(web_address), "%s", "Waiting for station IPv4");
  }
  display.home(network_line, web_address, "");
}

void showAccessState(CoreDisplay& display, const HttpAccessSnapshot& snapshot) {
  switch (snapshot.state) {
  case core_security::BootstrapState::open:
    display.accessCode(snapshot.code);
    break;
  case core_security::BootstrapState::consumed:
    display.accessState(CoreDisplay::AccessState::granted);
    break;
  case core_security::BootstrapState::expired:
    display.accessState(CoreDisplay::AccessState::expired);
    break;
  case core_security::BootstrapState::attempts_exhausted:
    display.accessState(CoreDisplay::AccessState::attempts_exhausted);
    break;
  case core_security::BootstrapState::unavailable:
    display.accessState(CoreDisplay::AccessState::unavailable);
    break;
  }
}

} // namespace

void Application::run() {
  ESP_ERROR_CHECK(nvs_flash_init());

  static core_security::EspSeedPartition seed_partition;
  static core_security::EspHardwareRandom random;
  static core_security::DeviceSeed device_seed;
  core_security::SeedOrigin seed_origin{};
  core_security::Status seed_result = core_security::Status::IoError;
  {
    // The display may use ADC-backed peripherals, so finish early entropy collection first.
    EarlyEntropy entropy;
    seed_result = core_security::load_or_generate(seed_partition, random, device_seed, seed_origin);
  }

  static CoreDisplay display;
  ESP_ERROR_CHECK(display.begin());
  EarlyEntropy startup_entropy;
  display.bootSplash();
  showProgress(display, 5, "Starting secure services");
  if (seed_result != core_security::Status::Ok) {
    stopAtFatal(display,
                {"SECURE STORAGE", "Device Seed unavailable",
                 "Internal key material is missing or corrupt.", "Service internal flash"});
  }
  showProgress(display, 15, "Device identity ready");

  showProgress(display, 20, "Mounting encrypted SD storage");
  const esp_err_t sd_result = board::mount_sd();
  if (sd_result != ESP_OK) {
    ESP_LOGE(kTag, "required SD storage unavailable: %s", esp_err_to_name(sd_result));
    stopAtFatal(display, {"SD CARD REQUIRED", "Storage unavailable",
                          "Insert a working SD card to continue.", "Restart Core Pilot"});
  }

  static core_storage::SdEncryptedStore sd_store;
  static core_storage::FlashEncryptedStore flash_store;
  if (flash_store.begin() != core_security::Status::Ok) {
    stopAtFatal(display, {"SECURE STORAGE", "Internal journal unavailable",
                          "Rollback protection could not start.", "Service internal flash"});
  }
  static core_storage::ReplicatedEncryptedStore replicated_store(flash_store, &sd_store);
  static core_security::StorageService storage(device_seed, replicated_store);
  constexpr char kSmokePath[] = "/core-pilot/diagnostics/storage-smoke.enc";
  constexpr std::uint8_t kSmokeData[] = {'S', 'D', 'C', 'H', 'E', 'C', 'K', '1'};
  const core_security::RecordSpec smoke_spec{kSmokePath,
                                             sizeof(kSmokePath) - 1,
                                             core_security::Namespace::Diagnostics,
                                             core_security::RecordType::Diagnostics,
                                             1,
                                             1};
  std::uint8_t smoke_record[128]{};
  std::uint8_t smoke_plaintext[sizeof(kSmokeData)]{};
  std::size_t smoke_size = 0;
  core_security::Metadata smoke_metadata{};
  auto smoke_result = storage.put(smoke_spec, kSmokeData, sizeof(kSmokeData), random, smoke_record,
                                  sizeof(smoke_record));
  if (smoke_result == core_security::Status::Ok) {
    smoke_result = storage.get(smoke_spec, smoke_record, sizeof(smoke_record), smoke_plaintext,
                               sizeof(smoke_plaintext), smoke_size, smoke_metadata);
  }
  if (smoke_result != core_security::Status::Ok || smoke_size != sizeof(kSmokeData) ||
      !std::equal(std::begin(kSmokeData), std::end(kSmokeData), std::begin(smoke_plaintext))) {
    ESP_LOGE(kTag, "required SD storage self-test failed: %d", static_cast<int>(smoke_result));
    stopAtFatal(display,
                {"SD CARD ERROR", "Storage self-test failed",
                 "Encrypted records cannot be read and written safely.", "Replace the SD card"});
  }
  ESP_LOGI(kTag, "required encrypted SD storage verified");
  showProgress(display, 30, "Encrypted SD storage ready");

  static ActivityService activity(storage, random);
  const auto activity_result = activity.start();
  if (activity_result != core_security::Status::Ok) {
    ESP_LOGE(kTag, "activity history unavailable: %d", static_cast<int>(activity_result));
  } else {
    activity.append("system_started", "Core Pilot secure services started.");
  }

  static DeviceRegistry devices(storage, random);
  const auto devices_result = devices.load();
  if (devices_result != core_security::Status::Ok) {
    ESP_LOGE(kTag, "Matter device registry unavailable: %d; existing record preserved",
             static_cast<int>(devices_result));
  }
  static ModelRepository models(storage, random);
  const auto models_result = models.start();
  if (models_result != core_security::Status::Ok) {
    ESP_LOGE(kTag, "Model database unavailable: %d; existing record preserved read-only",
             static_cast<int>(models_result));
  }
  SavedWifiNetworks saved_wifi;
  const auto wifi_result = loadWifi(storage, random, saved_wifi);
  ESP_LOGI(kTag, "encrypted Wi-Fi profile state=%d", static_cast<int>(wifi_result));
  if (wifi_result != core_security::Status::Ok && wifi_result != core_security::Status::NotFound) {
    stopAtFatal(display, {"ENCRYPTED STORAGE REQUIRED", "Wi-Fi record invalid",
                          "Saved network data failed authentication or decoding.",
                          "Restore a valid encrypted profile"});
  }
  showProgress(display, 40, "Network profile loaded");

  network_policy::Profile profiles[core_storage::kMaxSavedWifiNetworks]{};
  const std::size_t profile_count = wifi_result == core_security::Status::Ok ? saved_wifi.count : 0;
  for (std::size_t i = 0; i < profile_count; ++i)
    profiles[i] = saved_wifi.profile(i);
  char setup_password[17]{};
  if (!generateHexCredential(random, setup_password)) {
    stopAtFatal(display,
                {"NETWORK SECURITY", "Setup credential unavailable",
                 "Hardware randomness could not protect setup mode.", "Restart Core Pilot"});
  }
  esp_log_level_set("wifi", ESP_LOG_WARN);
  static CoreNetwork network;
  showProgress(display, 50, "Joining local network");
  const esp_err_t network_result =
      network.begin(profiles, profile_count, setup_password, networkBootProgress, &display);
  startup_entropy.disable();
  ESP_LOGI(kTag, "network startup result=%s", esp_err_to_name(network_result));
  if (network_result != ESP_OK) {
    stopAtFatal(display,
                {"NETWORK SERVICE", "Wi-Fi unavailable",
                 "Neither station nor local setup networking could start.", "Restart Core Pilot"});
  }
  showProgress(display, 65, "Local network initialized");
  activity.append(
      network.status().mode == CoreNetwork::Mode::station ? "wifi_connected" : "wifi_setup_active",
      network.status().mode == CoreNetwork::Mode::station ? "Station network is ready."
                                                          : "Setup access point is ready.");

  ThreadDataset thread_dataset;
  const auto thread_dataset_result = loadThreadDataset(storage, thread_dataset);
  if (thread_dataset_result != core_security::Status::Ok &&
      thread_dataset_result != core_security::Status::NotFound) {
    ESP_LOGE(kTag, "active Thread dataset is invalid: %d", static_cast<int>(thread_dataset_result));
  }

  const auto network_status = network.status();
  const int active_index =
      saved_wifi.find(network_status.station_ssid, std::strlen(network_status.station_ssid));
  const auto active_profile = active_index >= 0
                                  ? saved_wifi.profile(static_cast<std::size_t>(active_index))
                                  : network_policy::Profile{};

  static CoreMatter matter;
  static WifiProvisionService wifi_provision(network, storage, random, activity, saved_wifi);
  const esp_err_t wifi_provision_result = wifi_provision.start();
  if (wifi_provision_result != ESP_OK)
    ESP_LOGE(kTag, "Wi-Fi provisioning startup failed: %s", esp_err_to_name(wifi_provision_result));

  static AutomationService automations(models, devices, matter, activity);
  const esp_err_t automation_result = automations.start();
  if (automation_result != ESP_OK)
    ESP_LOGE(kTag, "automation service startup failed: %s", esp_err_to_name(automation_result));

  static SmtpService smtp(storage, random, activity);
  const esp_err_t smtp_result = smtp.start();
  if (smtp_result != ESP_OK)
    ESP_LOGE(kTag, "SMTP service startup failed: %s", esp_err_to_name(smtp_result));

  static ai::AiStorageService ai_storage(storage, random);
  const auto ai_storage_result = ai_storage.start();
  if (ai_storage_result != ai::Status::Ok)
    ESP_LOGE(kTag, "AI encrypted storage unavailable: %d", static_cast<int>(ai_storage_result));

  static AiChatService ai_chat(ai_storage);
  const esp_err_t ai_chat_result = ai_chat.start();
  if (ai_chat_result != ESP_OK)
    ESP_LOGE(kTag, "AI chat service startup failed: %s", esp_err_to_name(ai_chat_result));

  static OtaService ota;
  const esp_err_t ota_result = ota.start();
  if (ota_result != ESP_OK)
    ESP_LOGE(kTag, "OTA service startup failed: %s", esp_err_to_name(ota_result));

  ESP_ERROR_CHECK(initializeHttpAccess(random, network));
  static const HttpRuntime http_runtime{network, activity,       automations, matter,  devices,
                                        models,  wifi_provision, ai_storage,  ai_chat, ota,
                                        smtp,    storage,        random};
  ai_chat.bindRuntime(http_runtime);
  const esp_err_t mcp_result = McpService::initialize(http_runtime);
  if (mcp_result != ESP_OK) {
    ESP_LOGE(kTag, "MCP permission storage unavailable: %s", esp_err_to_name(mcp_result));
  }
  publishRtcReady(display.rtcReady());
  const esp_err_t time_result = initializeTimeService(http_runtime);
  if (time_result != ESP_OK) {
    ESP_LOGE(kTag, "time service startup failed: %s", esp_err_to_name(time_result));
  }
  showProgress(display, 90, "Starting local dashboard");
  const httpd_handle_t server = startHttpServer(http_runtime);
  if (server == nullptr) {
    stopAtFatal(display, {"NETWORK SERVICE", "HTTP unavailable",
                          "The local dashboard server could not start.", "Restart Core Pilot"});
  }

  showProgress(display, 95, "Checking Gateway H2 radio");
  static CoreGateway gateway;
  const esp_err_t gateway_result = gateway.ensureReady();
  if (gateway_result != ESP_OK)
    ESP_LOGE(kTag, "Gateway H2 RCP unavailable: %s", esp_err_to_name(gateway_result));

  showProgress(display, 97, "Starting Matter and Thread");
  const esp_err_t matter_result =
      active_index >= 0 && gateway_result == ESP_OK
          ? matter.begin(
                active_profile.ssid, active_profile.ssid_length, active_profile.password,
                active_profile.password_length,
                thread_dataset_result == core_security::Status::Ok ? thread_dataset.tlvs : nullptr,
                thread_dataset_result == core_security::Status::Ok ? thread_dataset.length : 0)
          : ESP_ERR_INVALID_STATE;
  thread_dataset.clear();

  const bool matter_owner_started = matter_result == ESP_OK;
  if (!matter_owner_started)
    ESP_LOGE(kTag, "Matter owner startup failed: %s", esp_err_to_name(matter_result));
  activity.append(matter_owner_started ? "matter_starting" : "matter_start_failed",
                  matter_owner_started ? "Matter and Thread services are starting."
                                       : "Matter and Thread services require attention.");

  showProgress(display, 100,
               matter_owner_started ? "Core Pilot ready" : "Ready; Matter requires service");
  showHome(display, network, setup_password);
  mbedtls_platform_zeroize(setup_password, sizeof(setup_password));
  HttpAccessSnapshot access_snapshot{};
  core_security::BootstrapState displayed_access_state = core_security::BootstrapState::unavailable;
  if (snapshotHttpAccess(access_snapshot)) {
    showAccessState(display, access_snapshot);
    displayed_access_state = access_snapshot.state;
  }
  mbedtls_platform_zeroize(&access_snapshot, sizeof(access_snapshot));
  TickType_t last_access_check = xTaskGetTickCount();

  std::uint64_t discovery_nodes[core_storage::kMaxMatterDevices]{};
  std::size_t discovery_node_count = 0;
  const auto registered_devices = devices.snapshot();
  for (std::size_t index = 0; index < registered_devices.count; ++index) {
    const auto node_id = registered_devices.entries[index].node_id;
    bool known = false;
    for (std::size_t node_index = 0; node_index < discovery_node_count; ++node_index) {
      known = known || discovery_nodes[node_index] == node_id;
    }
    if (!known) {
      discovery_nodes[discovery_node_count++] = node_id;
    }
  }

  std::uint64_t recorded_node_id = 0;
  std::size_t discovery_node_index = 0;
  bool discovery_in_flight = false;
  bool matter_ready_reported = false;
  bool ota_update_shown = false;
  std::uint64_t reconciled_terminal_sequence = 0;
  std::uint64_t last_removed_node_id = 0;
  while (true) {
    servicePendingTimeNetworkEvent();
    std::time_t synchronized_time = 0;
    if (consumePendingTimeSync(synchronized_time)) {
      const bool updated = display.setRtcFromUnix(synchronized_time);
      publishRtcReady(updated);
      if (!updated)
        ESP_LOGW(kTag, "failed to update hardware RTC from synchronized network time");
    }

    const auto matter_status = matter.status();
    CoreMatter::TerminalCommand terminals[8]{};
    const std::size_t terminal_count =
        matter.terminalCommandsAfter(reconciled_terminal_sequence, terminals, std::size(terminals));
    for (std::size_t terminal_index = 0; terminal_index < terminal_count; ++terminal_index) {
      const auto& terminal = terminals[terminal_index];
      reconciled_terminal_sequence = terminal.sequence;
      const auto& command_status = terminal.status;
      if (command_status.kind != CoreMatter::CommandKind::unpair_device)
        continue;
      if (command_status.state == CoreMatter::CommandState::complete) {
        const auto removed = devices.removeNode(command_status.node_id);
        if (removed == core_security::Status::Ok) {
          last_removed_node_id = command_status.node_id;
          activity.append("matter_device_removed",
                          "A Matter device was removed from the local encrypted registry.");
          std::size_t write = 0;
          for (std::size_t read = 0; read < discovery_node_count; ++read) {
            if (discovery_nodes[read] != command_status.node_id)
              discovery_nodes[write++] = discovery_nodes[read];
          }
          discovery_node_count = write;
          if (discovery_node_index > discovery_node_count)
            discovery_node_index = discovery_node_count;
        } else {
          activity.append("matter_device_remove_failed",
                          "Matter unpair succeeded but local encrypted "
                          "registry update failed.");
        }
      } else {
        activity.append("matter_device_remove_failed", "Matter device removal failed.");
      }
    }
    if (!matter_ready_reported && matter_status.state == CoreMatter::State::ready) {
      matter_ready_reported = true;
      activity.append("matter_started", "Matter and Thread services started.");
    }
    const auto ota_status = ota.status();
    if (!ota_update_shown && ota_status.state == CoreOta::State::activated) {
      ota_update_shown = true;
      display.upgrade({CoreDisplay::UpgradePhase::activated, ota_status.written_bytes,
                       ota_status.size_bytes, ota_status.restart_after_update});
    }
    if (matter_status.commissioning_state == CoreMatter::CommissioningState::complete &&
        matter_status.commissioned_node_id != 0 &&
        matter_status.commissioned_node_id != recorded_node_id) {
      recorded_node_id = matter_status.commissioned_node_id;
      bool known = false;
      for (std::size_t index = 0; index < discovery_node_count; ++index) {
        known = known || discovery_nodes[index] == recorded_node_id;
      }
      if (!known && discovery_node_count < core_storage::kMaxMatterDevices) {
        discovery_nodes[discovery_node_count++] = recorded_node_id;
      }
    }

    if (matter_owner_started && matter_status.state == CoreMatter::State::ready &&
        discovery_node_index < discovery_node_count) {
      if (!discovery_in_flight) {
        discovery_in_flight =
            matter.discoverEndpoints(discovery_nodes[discovery_node_index]) == ESP_OK;
      } else {
        const auto discovery = matter.discoveryStatus();
        if (!discovery.active) {
          if (discovery.complete && discovery_nodes[discovery_node_index] != last_removed_node_id) {
            core_storage::MatterDevice endpoints[core_storage::kMaxMatterDevices]{};
            const auto count = std::min(discovery.count,
                                        static_cast<std::size_t>(core_storage::kMaxMatterDevices));
            for (std::size_t index = 0; index < count; ++index) {
              endpoints[index] = {discovery.endpoints[index].node_id,
                                  discovery.endpoints[index].endpoint_id,
                                  discovery.endpoints[index].capabilities, true};
            }
            const auto result =
                devices.replaceNode(discovery_nodes[discovery_node_index], endpoints, count);
            if (result != core_security::Status::Ok) {
              ESP_LOGE(kTag, "failed to persist Matter endpoint discovery: %d",
                       static_cast<int>(result));
            } else if (matter.subscribeOnOff(discovery_nodes[discovery_node_index]) != ESP_OK) {
              ESP_LOGW(kTag, "failed to queue Matter OnOff subscription");
            }
          } else {
            ESP_LOGW(kTag, "Matter endpoint discovery failed for node %016llx: %s",
                     static_cast<unsigned long long>(discovery_nodes[discovery_node_index]),
                     esp_err_to_name(discovery.last_error));
          }
          ++discovery_node_index;
          discovery_in_flight = false;
        }
      }
    }

    const auto display_action = display.poll();
    if (display_action == CoreDisplay::Action::generate_access_code) {
      display.accessState(CoreDisplay::AccessState::unavailable);
      if (regenerateHttpAccessFromPhysicalPresence() != ESP_OK)
        ESP_LOGE(kTag, "physical access-code regeneration failed");
      last_access_check = 0;
    }

    const TickType_t access_now = xTaskGetTickCount();
    if (last_access_check == 0 || access_now - last_access_check >= pdMS_TO_TICKS(250)) {
      last_access_check = access_now;
      HttpAccessSnapshot next{};
      if (snapshotHttpAccess(next) && next.state != displayed_access_state) {
        displayed_access_state = next.state;
        showAccessState(display, next);
      }
      mbedtls_platform_zeroize(&next, sizeof(next));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

} // namespace core_pilot
