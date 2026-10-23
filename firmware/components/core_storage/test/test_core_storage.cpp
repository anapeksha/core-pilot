#include "core_storage.hpp"
#include "matter_devices_codec.hpp"
#include "models_codec.hpp"
#include "unity.h"
#include "wifi_codec.hpp"

#include <cstring>

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

TEST_CASE("Matter device codec rejects malformed records", "[core_storage]") {
  MatterDevices source{};
  source.entries[0] = {0x0002000000000001ULL, 1, 0x00000001, true};
  source.entries[1] = {0x0002000000000002ULL, 3, 0x00000006, false};
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
  TEST_ASSERT_EQUAL(static_cast<int>(Status::UnsupportedFormat),
                    static_cast<int>(decode_matter_devices(3, encoded, written, decoded)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Invalid),
      static_cast<int>(decode_matter_devices(kMatterDevicesSchema, encoded, written - 1, decoded)));
  encoded[15] = 2;
  TEST_ASSERT_EQUAL(
      static_cast<int>(Status::Invalid),
      static_cast<int>(decode_matter_devices(kMatterDevicesSchema, encoded, written, decoded)));

  const uint8_t legacy[] = {1, 1, 0, 0, 0, 0, 0, 2, 0, 1, 0, 1};
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok),
                    static_cast<int>(decode_matter_devices(1, legacy, sizeof(legacy), decoded)));
  TEST_ASSERT_EQUAL_UINT32(0, decoded.entries[0].capabilities);
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

TEST_CASE("Wi-Fi schema 1 postcard codec validates lengths and option", "[core_storage]") {
  const uint8_t ssid[] = {'w', 'i', 'f', 'i'};
  const uint8_t password[] = {'1', '2', '3', '4', '5', '6', '7', '8'};
  const uint8_t hostname[] = {'c', 'o', 'r', 'e', 'p', 'i', 'l', 'o', 't'};
  WifiConfig config{ssid, sizeof(ssid), password, sizeof(password), hostname, sizeof(hostname)};
  uint8_t encoded[kMaxWifiPayload]{};
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
                    static_cast<int>(decode_wifi(2, encoded, written, decoded)));
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(decode_wifi(1, encoded, written - 1, decoded)));
  encoded[1 + sizeof(ssid) + 1 + sizeof(password)] = 2;
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(decode_wifi(1, encoded, written, decoded)));
  config.ssid_len = 33;
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(encode_wifi(config, encoded, sizeof(encoded), written)));
}
