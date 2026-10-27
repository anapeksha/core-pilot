#include "mcp_permissions.hpp"
#include "model_repository.hpp"
#include "unity.h"

#include <cstdint>
#include <cstring>

using namespace core_pilot;

namespace {
class TestRandom final : public core_security::RandomSource {
public:
  core_security::Status fill(std::uint8_t* output, std::size_t size) override {
    for (std::size_t index = 0; index < size; ++index)
      output[index] = static_cast<std::uint8_t>(index + 1U);
    return core_security::Status::Ok;
  }
};

class MemoryStore final : public core_security::EncryptedStore {
public:
  core_security::Status write_atomic(const char*, std::size_t, const std::uint8_t* record,
                                     std::size_t size) override {
    if (size > sizeof(record_))
      return core_security::Status::TooLarge;
    std::memcpy(record_, record, size);
    size_ = size;
    return core_security::Status::Ok;
  }

  core_security::Status read(const char*, std::size_t, std::uint8_t* record, std::size_t capacity,
                             std::size_t& size) override {
    if (size_ == 0)
      return core_security::Status::NotFound;
    if (capacity < size_)
      return core_security::Status::BufferTooSmall;
    std::memcpy(record, record_, size_);
    size = size_;
    return core_security::Status::Ok;
  }

private:
  std::uint8_t record_[core_security::kMaxRecordBytes]{};
  std::size_t size_{0};
};
} // namespace

TEST_CASE("MCP permission v2 path is distinct and v1 bits migrate by stable ID", "[mcp]") {
  TEST_ASSERT_EQUAL_STRING("/core-pilot/ai/tool-permissions.enc", mcp_permissions::v1_path.data());
  TEST_ASSERT_EQUAL_STRING("/core-pilot/ai/tool-permissions-v2.enc",
                           mcp_permissions::v2_path.data());
  TEST_ASSERT_TRUE(mcp_permissions::v1_enabled(1U << 7U, "scenes.list"));
  TEST_ASSERT_TRUE(mcp_permissions::v1_enabled(1U << 14U, "matter.remove_device"));
  TEST_ASSERT_FALSE(mcp_permissions::v1_enabled(1U << 7U, "rooms.create"));
  TEST_ASSERT_FALSE(mcp_permissions::v1_enabled(1U << 11U, "automations.create"));
}

TEST_CASE("model repository owner distinguishes create update and enable-only mutations",
          "[models]") {
  static core_security::DeviceSeed seed;
  static MemoryStore store;
  static TestRandom random;
  static core_security::StorageService storage(seed, store);
  static ModelRepository repository(storage, random);
  std::uint8_t seed_bytes[core_security::kSeedBytes]{};
  for (std::size_t index = 0; index < sizeof(seed_bytes); ++index)
    seed_bytes[index] = static_cast<std::uint8_t>(index + 1U);
  seed.assign(seed_bytes);
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::Status::Ok),
                    static_cast<int>(repository.start()));

  core_domain::ModelUpsert missing{};
  missing.has_id = true;
  std::strcpy(missing.id, "missing");
  std::strcpy(missing.name, "Missing");
  ModelRepository::Result result{};
  TEST_ASSERT_TRUE(repository.submit(ModelRepository::Mutation::update,
                                     core_domain::ModelKind::room, missing, result));
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::Status::NotFound),
                    static_cast<int>(result.status));

  core_domain::ModelUpsert automation{};
  std::strcpy(automation.name, "Morning");
  std::strcpy(automation.trigger, "manual");
  std::strcpy(automation.details[0], "delay|1");
  automation.detail_count = 1;
  TEST_ASSERT_TRUE(repository.submit(ModelRepository::Mutation::create,
                                     core_domain::ModelKind::automation, automation, result));
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::Status::Ok), static_cast<int>(result.status));

  core_domain::ModelUpsert enable{};
  enable.has_id = true;
  enable.enabled = true;
  std::memcpy(enable.id, result.id, sizeof(enable.id));
  TEST_ASSERT_TRUE(repository.submit(ModelRepository::Mutation::set_automation_enabled,
                                     core_domain::ModelKind::automation, enable, result));
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::Status::Ok), static_cast<int>(result.status));

  core_domain::ModelUpsert stored{};
  TEST_ASSERT_TRUE(repository.find(core_domain::ModelKind::automation, result.id, stored));
  TEST_ASSERT_TRUE(stored.enabled);
  TEST_ASSERT_EQUAL_STRING("Morning", stored.name);
  TEST_ASSERT_EQUAL_STRING("manual", stored.trigger);
  TEST_ASSERT_EQUAL_STRING("delay|1", stored.details[0]);
}
