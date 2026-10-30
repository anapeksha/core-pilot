#include "core_matter.hpp"
#include "core_security.hpp"
#include "thread_dataset.hpp"
#include "unity.h"

#include <cstdint>
#include <cstring>

using namespace core_pilot;

namespace {
class TestRandom final : public core_security::RandomSource {
public:
  core_security::Status fill(std::uint8_t* output, std::size_t size) override {
    std::memset(output, 0x5a, size);
    return core_security::Status::Ok;
  }
};

class CountingStore final : public core_security::EncryptedStore {
public:
  core_security::Status write_atomic(const char*, std::size_t, const std::uint8_t*,
                                     std::size_t) override {
    ++writes;
    return core_security::Status::Ok;
  }

  core_security::Status read(const char*, std::size_t, std::uint8_t*, std::size_t,
                             std::size_t&) override {
    return core_security::Status::NotFound;
  }

  std::uint32_t writes{};
};
} // namespace

TEST_CASE("rejected Thread dataset submission performs no durable mutation", "[thread_dataset]") {
  static core_security::DeviceSeed seed;
  static CountingStore store;
  static TestRandom random;
  static core_security::StorageService storage(seed, store);
  static CoreMatter matter;
  static ThreadDatasetService service(storage, random, matter);

  std::uint8_t seed_bytes[core_security::kSeedBytes]{};
  for (std::size_t index = 0; index < sizeof(seed_bytes); ++index)
    seed_bytes[index] = static_cast<std::uint8_t>(index + 1U);
  seed.assign(seed_bytes);
  TEST_ASSERT_EQUAL(ESP_OK, service.start());

  ThreadDataset dataset{};
  TEST_ASSERT_TRUE(decodeThreadDatasetHex("0e080000000000010000", dataset));
  std::uint32_t accepted_operation = 0;
  TEST_ASSERT_EQUAL(ESP_OK, service.submit(dataset, accepted_operation));
  TEST_ASSERT_NOT_EQUAL(0, accepted_operation);

  std::uint32_t rejected_operation = 0;
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, service.submit(dataset, rejected_operation));
  TEST_ASSERT_EQUAL_UINT32(0, rejected_operation);
  TEST_ASSERT_EQUAL_UINT32(0, store.writes);
  TEST_ASSERT_EQUAL(static_cast<int>(ThreadDatasetService::State::queued),
                    static_cast<int>(service.status().state));
  dataset.clear();
}
