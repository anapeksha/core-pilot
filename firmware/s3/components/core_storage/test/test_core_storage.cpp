#include "automation_schedule_codec.hpp"
#include "core_storage.hpp"
#include "matter_devices_codec.hpp"
#include "models_codec.hpp"
#include "wifi_codec.hpp"

#include <cstring>
#include <unity.h>

using core_security::Status;
using namespace core_storage;
namespace {
struct Expected {
  const char* path;
  const char* file;
  const char* temp;
};
constexpr Expected kExpected[] = {
    {"/core-pilot/config/wifi.enc", "WIFI.ENC", "WIFI.TMP"},
    {"/core-pilot/config/ai_provider.enc", "AIPROV.ENC", "AIPROV.TMP"},
    {"/core-pilot/ai/memory.enc", "AIMEM.ENC", "AIMEM.TMP"},
    {"/core-pilot/ai/memory-v2.enc", "AIMEM2.ENC", "AIMEM2.TMP"},
    {"/core-pilot/ai/prompts.enc", "AIPROM.ENC", "AIPROM.TMP"},
    {"/core-pilot/ai/tool-permissions.enc", "AITOOL.ENC", "AITOOL.TMP"},
    {"/core-pilot/ai/tool-permissions-v2.enc", "AITOOL2.ENC", "AITOOL2.TMP"},
    {"/core-pilot/config/time.enc", "TIME.ENC", "TIME.TMP"},
    {"/core-pilot/config/smtp.enc", "SMTP.ENC", "SMTP.TMP"},
    {"/core-pilot/matter/fabric.enc", "FABRIC.ENC", "FABRIC.TMP"},
    {"/core-pilot/matter/devices.enc", "MATDEV.ENC", "MATDEV.TMP"},
    {"/core-pilot/matter/devices-legacy.enc", "MATOLD.ENC", "MATOLD.TMP"},
    {"/core-pilot/matter/kv-image.enc", "MATKV.ENC", "MATKV.TMP"},
    {"/core-pilot/matter/commissioning-boot.enc", "MATBOOT.ENC", "MATBOOT.TMP"},
    {"/core-pilot/matter/controller-attestation.enc", "MATATT.ENC", "MATATT.TMP"},
    {"/core-pilot/matter/paa-anchors.enc", "PAA.ENC", "PAA.TMP"},
    {"/core-pilot/matter/trusted-products.enc", "MATPROD.ENC", "MATPROD.TMP"},
    {"/core-pilot/thread/dataset.enc", "THREAD.ENC", "THREAD.TMP"},
    {"/core-pilot/thread/settings.enc", "OTSET.ENC", "OTSET.TMP"},
    {"/core-pilot/models/database.enc", "MODELS.ENC", "MODELS.TMP"},
    {"/core-pilot/models/database-unreadable.enc", "MODBAD.ENC", "MODBAD.TMP"},
    {"/core-pilot/automations/schedule-checkpoints.enc", "AUTOSCH.ENC", "AUTOSCH.TMP"},
    {"/core-pilot/automations/schedule-checkpoints-unreadable.enc", "AUTBAD.ENC", "AUTBAD.TMP"},
    {"/core-pilot/diagnostics/activity-history.enc", "ACTIVITY.ENC", "ACTIVITY.TMP"},
    {"/core-pilot/config/system.enc", "SYSTEM.ENC", "SYSTEM.TMP"},
    {"/core-pilot/config/ui.enc", "UI.ENC", "UI.TMP"},
    {"/core-pilot/diagnostics/storage-smoke.enc", "SMOKE.ENC", "SMOKE.TMP"},
};
} // namespace

TEST_CASE("FAT allowlist matches stable storage paths exactly", "[core_storage]") {
  for (const auto& item : kExpected) {
    const auto* names = fat_names(item.path, std::strlen(item.path));
    TEST_ASSERT_NOT_NULL(names);
    TEST_ASSERT_EQUAL_STRING(item.file, names->record);
    TEST_ASSERT_EQUAL_STRING(item.temp, names->temporary);
    TEST_ASSERT_EQUAL_STRING_LEN(item.file, names->backup, std::strlen(item.file) - 3);
  }
  const char* bad[] = {"/core-pilot/config/wifi.enc/../ui.enc", "/core-pilot/config/WIFI.enc",
                       "/core-pilot/ai/tool_permissions.enc", "/core-pilot/config/wifi.tmp",
                       "/core-pilot/config/wifi.enc/"};
  for (auto path : bad)
    TEST_ASSERT_NULL(fat_names(path, std::strlen(path)));
  TEST_ASSERT_NULL(fat_names(nullptr, 1));
  TEST_ASSERT_NULL(fat_names(kExpected[0].path, core_security::kMaxPathBytes + 1));
  TEST_ASSERT_NULL(fat_names(kExpected[0].path, 1));
}

TEST_CASE("encrypted SD backend rejects plaintext and oversize before I/O", "[core_storage]") {
  SdEncryptedStore store;
  const char path[] = "/core-pilot/config/wifi.enc";
  const uint8_t plain[] = {'p', 'a', 's', 's'};
  const uint8_t magic[] = {'C', 'P', 'E', 'R'};
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Invalid),
      static_cast<int>(store.write_atomic(path, sizeof(path) - 1, plain, sizeof(plain))));
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::TooLarge),
      static_cast<int>(store.write_atomic(path, sizeof(path) - 1, magic, kMaxStoredBytes + 1)));
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(store.write_atomic("/bad", 4, magic, sizeof(magic))));
}

TEST_CASE("tracked missing flash record cannot be imported from SD", "[core_storage]") {
  TEST_ASSERT_EQUAL(static_cast<int>(Status::NotFound),
                    static_cast<int>(missing_flash_status(false)));
  TEST_ASSERT_EQUAL(static_cast<int>(Status::AuthenticationFailed),
                    static_cast<int>(missing_flash_status(true)));
}

TEST_CASE("Matter device codec round trips names and rejects malformed records", "[core_storage]") {
  MatterDevices source{};
  source.entries[0] = {0x0002000000000001ULL, 1, 0x00000001, true, {}};
  source.entries[1] = {0x0002000000000002ULL, 3, 0x00000006, false, {}};
  std::strcpy(source.entries[0].name, "Kitchen light");
  std::memset(source.entries[1].name, 'n', kMaxMatterDeviceNameBytes);
  source.entries[1].name[kMaxMatterDeviceNameBytes] = '\0';
  source.count = 2;
  uint8_t encoded[kMaxMatterDevicesPayload]{};
  size_t written = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(Status::BufferTooSmall),
                    static_cast<int>(encode_matter_devices(source, encoded, 1, written)));
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok), static_cast<int>(encode_matter_devices(
                                                      source, encoded, sizeof(encoded), written)));
  MatterDevices decoded{};
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Ok),
      static_cast<int>(decode_matter_devices(kMatterDevicesSchema, encoded, written, decoded)));
  TEST_ASSERT_EQUAL_UINT32(2, decoded.count);
  TEST_ASSERT_EQUAL_UINT64(source.entries[0].node_id, decoded.entries[0].node_id);
  TEST_ASSERT_EQUAL_UINT16(3, decoded.entries[1].endpoint_id);
  TEST_ASSERT_EQUAL_HEX32(0x00000006, decoded.entries[1].capabilities);
  TEST_ASSERT_FALSE(decoded.entries[1].reachable);
  TEST_ASSERT_EQUAL_STRING("Kitchen light", decoded.entries[0].name);
  TEST_ASSERT_EQUAL_UINT32(kMaxMatterDeviceNameBytes, std::strlen(decoded.entries[1].name));
  TEST_ASSERT_EQUAL(static_cast<int>(Status::UnsupportedFormat),
                    static_cast<int>(decode_matter_devices(5, encoded, written, decoded)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Invalid),
      static_cast<int>(decode_matter_devices(kMatterDevicesSchema, encoded, written - 1, decoded)));
  encoded[15] = 2;
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Invalid),
      static_cast<int>(decode_matter_devices(kMatterDevicesSchema, encoded, written, decoded)));
  encoded[15] = 1;
  encoded[16] = 65;
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Invalid),
      static_cast<int>(decode_matter_devices(kMatterDevicesSchema, encoded, written, decoded)));
  encoded[16] = 1;
  encoded[17] = 0;
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Invalid),
      static_cast<int>(decode_matter_devices(kMatterDevicesSchema, encoded, written, decoded)));

  std::memset(source.entries[0].name, 'x', sizeof source.entries[0].name);
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Invalid),
      static_cast<int>(encode_matter_devices(source, encoded, sizeof(encoded), written)));

  const uint8_t legacy_v1[] = {1, 1, 0, 0, 0, 0, 0, 2, 0, 1, 0, 1};
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok), static_cast<int>(decode_matter_devices(
                                                      1, legacy_v1, sizeof(legacy_v1), decoded)));
  TEST_ASSERT_EQUAL_UINT32(0, decoded.entries[0].capabilities);
  TEST_ASSERT_EQUAL_STRING("", decoded.entries[0].name);

  const uint8_t legacy_v2[] = {1, 1, 0, 0, 0, 0, 0, 2, 0, 1, 0, 6, 0, 0, 0, 1};
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok), static_cast<int>(decode_matter_devices(
                                                      2, legacy_v2, sizeof(legacy_v2), decoded)));
  TEST_ASSERT_EQUAL_HEX32(6, decoded.entries[0].capabilities);
  TEST_ASSERT_EQUAL_STRING("", decoded.entries[0].name);
}

TEST_CASE("Matter device codec preserves pending endpoint discovery", "[core_storage]") {
  MatterDevices source{};
  source.entries[0] = {0x0002000000000001ULL, 0, 0, false, {}};
  source.count = 1;
  uint8_t encoded[kMaxMatterDevicesPayload]{};
  size_t written = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok), static_cast<int>(encode_matter_devices(
                                                      source, encoded, sizeof(encoded), written)));
  MatterDevices decoded{};
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Ok),
      static_cast<int>(decode_matter_devices(kMatterDevicesSchema, encoded, written, decoded)));
  TEST_ASSERT_EQUAL_UINT16(0, decoded.entries[0].endpoint_id);
  TEST_ASSERT_FALSE(decoded.entries[0].reachable);

  source.entries[0].reachable = true;
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Invalid),
      static_cast<int>(encode_matter_devices(source, encoded, sizeof(encoded), written)));
}

TEST_CASE("models codec round trips and rejects malformed records", "[core_storage]") {
  static ModelsDatabase source{};
  auto& room = source.collections[model_kind_index(core_domain::ModelKind::room)];
  room.count = 1;
  room.entries[0].has_id = true;
  std::strcpy(room.entries[0].id, "room-1");
  std::strcpy(room.entries[0].name, "Kitchen");
  auto& automation = source.collections[model_kind_index(core_domain::ModelKind::automation)];
  automation.count = 1;
  automation.entries[0].has_id = true;
  automation.entries[0].enabled = true;
  automation.entries[0].detail_count = 1;
  std::strcpy(automation.entries[0].id, "automation-1");
  std::strcpy(automation.entries[0].name, "Morning");
  std::strcpy(automation.entries[0].trigger, "manual");
  std::strcpy(automation.entries[0].details[0], "on|lamp|1");
  static uint8_t encoded[kMaxModelsPayload]{};
  size_t written = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(Status::BufferTooSmall),
                    static_cast<int>(encode_models(source, encoded, 4, written)));
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok),
                    static_cast<int>(encode_models(source, encoded, sizeof(encoded), written)));
  static ModelsDatabase decoded{};
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok),
                    static_cast<int>(decode_models(kModelsSchema, encoded, written, decoded)));
  TEST_ASSERT_EQUAL_STRING("Kitchen", decoded.collections[0].entries[0].name);
  TEST_ASSERT_TRUE(decoded.collections[3].entries[0].enabled);
  TEST_ASSERT_EQUAL_STRING("on|lamp|1", decoded.collections[3].entries[0].details[0]);
  TEST_ASSERT_EQUAL(static_cast<int>(Status::UnsupportedFormat),
                    static_cast<int>(decode_models(2, encoded, written, decoded)));
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(decode_models(kModelsSchema, encoded, written - 1, decoded)));
  encoded[0] = 'X';
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(decode_models(kModelsSchema, encoded, written, decoded)));
}

TEST_CASE("automation schedule checkpoint codec strictly validates records", "[core_storage]") {
  AutomationScheduleCheckpoints source{};
  source.count = 2;
  std::strcpy(source.entries[0].automation_id, "morning-lights");
  std::strcpy(source.entries[0].exact_trigger, "weekly|62|07:30|-300");
  source.entries[0].last_utc_occurrence_minute = 0x0102030405060708ULL;
  std::strcpy(source.entries[1].automation_id, "one-time");
  std::strcpy(source.entries[1].exact_trigger, "once|2028-02-29|23:59|+840");
  source.entries[1].last_utc_occurrence_minute = UINT64_MAX;

  uint8_t encoded[kMaxAutomationScheduleCheckpointsPayload]{};
  size_t written = 0;
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::BufferTooSmall),
      static_cast<int>(encode_automation_schedule_checkpoints(source, encoded, 1, written)));
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok),
                    static_cast<int>(encode_automation_schedule_checkpoints(
                        source, encoded, sizeof(encoded), written)));
  AutomationScheduleCheckpoints decoded{};
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok),
                    static_cast<int>(decode_automation_schedule_checkpoints(
                        kAutomationScheduleCheckpointsSchema, encoded, written, decoded)));
  TEST_ASSERT_EQUAL_UINT32(2, decoded.count);
  TEST_ASSERT_EQUAL_STRING(source.entries[0].automation_id, decoded.entries[0].automation_id);
  TEST_ASSERT_EQUAL_STRING(source.entries[1].exact_trigger, decoded.entries[1].exact_trigger);
  TEST_ASSERT_EQUAL_UINT64(source.entries[0].last_utc_occurrence_minute,
                           decoded.entries[0].last_utc_occurrence_minute);
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, decoded.entries[1].last_utc_occurrence_minute);
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::UnsupportedFormat),
      static_cast<int>(decode_automation_schedule_checkpoints(2, encoded, written, decoded)));
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(decode_automation_schedule_checkpoints(
                        kAutomationScheduleCheckpointsSchema, encoded, written - 1, decoded)));
  encoded[written] = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(decode_automation_schedule_checkpoints(
                        kAutomationScheduleCheckpointsSchema, encoded, written + 1, decoded)));

  std::strcpy(source.entries[1].automation_id, source.entries[0].automation_id);
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(encode_automation_schedule_checkpoints(
                        source, encoded, sizeof(encoded), written)));
  std::strcpy(source.entries[1].automation_id, "one-time");
  std::strcpy(source.entries[1].exact_trigger, "manual");
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(encode_automation_schedule_checkpoints(
                        source, encoded, sizeof(encoded), written)));
  std::strcpy(source.entries[1].exact_trigger, "weekly|128|07:30|+0");
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(encode_automation_schedule_checkpoints(
                        source, encoded, sizeof(encoded), written)));

  const uint8_t duplicate[] = {2,   1,   'a', 17,  'w', 'e', 'e', 'k', 'l', 'y', '|', '1', '|', '0',
                               '0', ':', '0', '0', '|', '+', '0', 0,   0,   0,   0,   0,   0,   0,
                               1,   'a', 17,  'w', 'e', 'e', 'k', 'l', 'y', '|', '2', '|', '0', '0',
                               ':', '0', '0', '|', '+', '0', 1,   0,   0,   0,   0,   0,   0,   0};
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Invalid),
      static_cast<int>(decode_automation_schedule_checkpoints(
          kAutomationScheduleCheckpointsSchema, duplicate, sizeof(duplicate), decoded)));
}

TEST_CASE("Wi-Fi schema 1 postcard codec validates lengths and option", "[core_storage]") {
  const uint8_t ssid[] = {'w', 'i', 'f', 'i'};
  const uint8_t password[] = {'1', '2', '3', '4', '5', '6', '7', '8'};
  const uint8_t hostname[] = {'c', 'o', 'r', 'e', 'p', 'i', 'l', 'o', 't'};
  WifiConfig config{ssid, sizeof(ssid), password, sizeof(password), hostname, sizeof(hostname)};
  uint8_t encoded[kMaxWifiSchema1Payload]{};
  size_t written = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(Status::BufferTooSmall),
                    static_cast<int>(encode_wifi(config, encoded, 1, written)));
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok),
                    static_cast<int>(encode_wifi(config, encoded, sizeof(encoded), written)));
  TEST_ASSERT_EQUAL_HEX8(sizeof(ssid), encoded[0]);
  TEST_ASSERT_EQUAL_MEMORY(ssid, encoded + 1, sizeof(ssid));
  WifiConfig decoded{};
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok),
                    static_cast<int>(decode_wifi(1, encoded, written, decoded)));
  TEST_ASSERT_EQUAL_MEMORY(password, decoded.password, sizeof(password));
  TEST_ASSERT_EQUAL_MEMORY(hostname, decoded.hostname, sizeof(hostname));
  TEST_ASSERT_EQUAL(static_cast<int>(Status::UnsupportedFormat),
                    static_cast<int>(decode_wifi(kWifiSchema, encoded, written, decoded)));
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(decode_wifi(1, encoded, written - 1, decoded)));
  encoded[1 + sizeof(ssid) + 1 + sizeof(password)] = 2;
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(decode_wifi(1, encoded, written, decoded)));
  config.ssid_len = 33;
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(encode_wifi(config, encoded, sizeof(encoded), written)));
}

TEST_CASE("Wi-Fi schema 2 codec bounds, orders, and deduplicates saved networks",
          "[core_storage]") {
  const uint8_t first_ssid[] = {'h', 'o', 'm', 'e'};
  const uint8_t second_ssid[] = {'w', 'o', 'r', 'k'};
  const uint8_t password[] = {'1', '2', '3', '4', '5', '6', '7', '8'};
  WifiNetworks networks{};
  networks.count = 2;
  networks.networks[0] = {first_ssid, sizeof(first_ssid), password, sizeof(password)};
  networks.networks[1] = {second_ssid, sizeof(second_ssid), nullptr, 0};
  uint8_t encoded[kMaxWifiPayload]{};
  size_t written = 0;
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Ok),
      static_cast<int>(encode_wifi_networks(networks, encoded, sizeof(encoded), written)));
  WifiNetworks decoded{};
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok),
                    static_cast<int>(decode_wifi_networks(kWifiSchema, encoded, written, decoded)));
  TEST_ASSERT_EQUAL_UINT32(2, decoded.count);
  TEST_ASSERT_EQUAL_MEMORY(first_ssid, decoded.networks[0].ssid, sizeof(first_ssid));
  TEST_ASSERT_EQUAL_MEMORY(second_ssid, decoded.networks[1].ssid, sizeof(second_ssid));
  networks.networks[1] = networks.networks[0];
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Invalid),
      static_cast<int>(encode_wifi_networks(networks, encoded, sizeof(encoded), written)));
  networks.count = kMaxSavedWifiNetworks + 1;
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Invalid),
      static_cast<int>(encode_wifi_networks(networks, encoded, sizeof(encoded), written)));
}
