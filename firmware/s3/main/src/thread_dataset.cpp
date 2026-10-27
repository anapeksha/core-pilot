#include "thread_dataset.hpp"

#include "lib/support/ThreadOperationalDataset.h"
#include "mbedtls/platform_util.h"

#include <cstring>

namespace core_pilot {
namespace {

constexpr char kPath[] = "/core-pilot/thread/dataset.enc";
constexpr std::uint16_t kSchema = 1;
constexpr std::size_t kEncryptedCapacity = 320;

int hexNibble(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  return -1;
}

core_security::RecordSpec recordSpec(std::uint64_t generation) {
  return {kPath,
          sizeof(kPath) - 1,
          core_security::Namespace::Thread,
          core_security::RecordType::ThreadActiveDataset,
          kSchema,
          generation};
}

} // namespace

void ThreadDataset::clear() {
  mbedtls_platform_zeroize(tlvs, sizeof(tlvs));
  length = 0;
  generation = 0;
}

bool validateThreadDataset(const std::uint8_t* tlvs, std::size_t length) {
  if (tlvs == nullptr || length == 0 || length > ThreadDataset::kMaxBytes)
    return false;
  chip::Thread::OperationalDataset dataset;
  return dataset.Init(chip::ByteSpan(tlvs, length)) == CHIP_NO_ERROR;
}

bool decodeThreadDatasetHex(const char* hex, ThreadDataset& dataset) {
  dataset.clear();
  if (hex == nullptr)
    return false;
  const std::size_t hex_length = std::strlen(hex);
  if (hex_length == 0 || hex_length > ThreadDataset::kMaxBytes * 2 || (hex_length % 2) != 0)
    return false;
  const std::size_t length = hex_length / 2;
  for (std::size_t index = 0; index < length; ++index) {
    const int high = hexNibble(hex[index * 2]);
    const int low = hexNibble(hex[index * 2 + 1]);
    if (high < 0 || low < 0) {
      dataset.clear();
      return false;
    }
    dataset.tlvs[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  if (!validateThreadDataset(dataset.tlvs, length)) {
    dataset.clear();
    return false;
  }
  dataset.length = static_cast<std::uint8_t>(length);
  return true;
}

core_security::Status loadThreadDataset(core_security::StorageService& storage,
                                        ThreadDataset& dataset) {
  dataset.clear();
  std::uint8_t encrypted[kEncryptedCapacity]{};
  std::size_t size = 0;
  core_security::Metadata metadata{};
  const auto result = storage.get(recordSpec(0), encrypted, sizeof(encrypted), dataset.tlvs,
                                  sizeof(dataset.tlvs), size, metadata);
  mbedtls_platform_zeroize(encrypted, sizeof(encrypted));
  if (result != core_security::Status::Ok)
    return result;
  if (metadata.schema_version != kSchema || !validateThreadDataset(dataset.tlvs, size)) {
    dataset.clear();
    return core_security::Status::Invalid;
  }
  dataset.length = static_cast<std::uint8_t>(size);
  dataset.generation = metadata.generation;
  return core_security::Status::Ok;
}

core_security::Status saveThreadDataset(core_security::StorageService& storage,
                                        core_security::RandomSource& random,
                                        ThreadDataset& dataset) {
  if (!validateThreadDataset(dataset.tlvs, dataset.length))
    return core_security::Status::Invalid;
  std::uint8_t encrypted[kEncryptedCapacity]{};
  const std::uint64_t generation = dataset.generation + 1;
  const auto result = storage.put(recordSpec(generation), dataset.tlvs, dataset.length, random,
                                  encrypted, sizeof(encrypted));
  mbedtls_platform_zeroize(encrypted, sizeof(encrypted));
  if (result == core_security::Status::Ok)
    dataset.generation = generation;
  return result;
}

} // namespace core_pilot
