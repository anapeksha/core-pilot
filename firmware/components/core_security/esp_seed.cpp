#include "core_security.hpp"
#include "esp_partition.h"
#include "esp_random.h"
#include "mbedtls/platform_util.h"

#include <cstring>

namespace core_security {
namespace {
const esp_partition_t* seed_partition() {
  const esp_partition_t* p =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "seed");
  if (!p || p->address != 0x920000 || p->size != 0x10000)
    return nullptr;
  return p;
}
} // namespace

Status EspSeedPartition::read(uint8_t (&record)[kSeedRecordBytes]) {
  const esp_partition_t* p = seed_partition();
  if (!p || esp_partition_read_raw(p, 0, record, sizeof(record)) != ESP_OK)
    return Status::IoError;
  bool erased = true;
  for (uint8_t byte : record)
    erased &= byte == 0xff;
  if (erased)
    return Status::NotFound;
  // An encrypted flash partition decrypts erased 0xff into arbitrary bytes.
  return esp_partition_read(p, 0, record, sizeof(record)) == ESP_OK ? Status::Ok : Status::IoError;
}
Status EspSeedPartition::write(const uint8_t (&record)[kSeedRecordBytes]) {
  const esp_partition_t* p = seed_partition();
  if (!p)
    return Status::IoError;
  uint8_t existing[kSeedRecordBytes]{};
  const Status state = read(existing);
  mbedtls_platform_zeroize(existing, sizeof(existing));
  if (state != Status::NotFound)
    return state == Status::Ok ? Status::Invalid : state;
  return esp_partition_write(p, 0, record, sizeof(record)) == ESP_OK ? Status::Ok : Status::IoError;
}
Status EspSeedPartition::destroy() {
  const esp_partition_t* p = seed_partition();
  if (!p || esp_partition_erase_range(p, 0, p->size) != ESP_OK)
    return Status::IoError;
  uint8_t block[256]{};
  for (size_t offset = 0; offset < p->size; offset += sizeof(block)) {
    if (esp_partition_read_raw(p, offset, block, sizeof(block)) != ESP_OK) {
      mbedtls_platform_zeroize(block, sizeof(block));
      return Status::IoError;
    }
    for (uint8_t byte : block) {
      if (byte != 0xff) {
        mbedtls_platform_zeroize(block, sizeof(block));
        return Status::IoError;
      }
    }
  }
  mbedtls_platform_zeroize(block, sizeof(block));
  return Status::Ok;
}
Status EspHardwareRandom::fill(uint8_t* out, size_t size) {
  if (!out && size)
    return Status::Invalid;
  // Caller must ensure ESP-IDF's entropy source is enabled (Wi-Fi/Bluetooth or
  // bootloader RNG entropy source) before generating a persistent Device Seed.
  if (size)
    esp_fill_random(out, size);
  return Status::Ok;
}
} // namespace core_security
