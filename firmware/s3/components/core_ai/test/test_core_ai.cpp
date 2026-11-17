#include "core_ai.hpp"

#include <cmath>
#include <cstring>
#include <unity.h>
#include <unity_test_runner.h>
#include <vector>

using namespace core_pilot::ai;

namespace {

ProviderConfig openai_config() {
  ProviderConfig config{};
  std::strcpy(config.base_url, "https://api.openai.com/v1");
  std::strcpy(config.model, "gpt-test");
  config.api_key_length = 1;
  config.api_key[0] = 'x';
  return config;
}

class MemoryStore final : public core_security::EncryptedStore {
public:
  std::vector<std::uint8_t> record;
  bool fail_write{};
  unsigned writes{};
  core_security::Status write_atomic(const char*, std::size_t, const std::uint8_t* bytes,
                                     std::size_t size) override {
    if (fail_write)
      return core_security::Status::IoError;
    record.assign(bytes, bytes + size);
    ++writes;
    return core_security::Status::Ok;
  }
  core_security::Status read(const char*, std::size_t, std::uint8_t* bytes, std::size_t capacity,
                             std::size_t& size) override {
    size = 0;
    if (record.empty())
      return core_security::Status::NotFound;
    if (record.size() > capacity)
      return core_security::Status::BufferTooSmall;
    size = record.size();
    std::memcpy(bytes, record.data(), size);
    return core_security::Status::Ok;
  }
};

} // namespace

TEST_CASE("AI memory identifiers enforce exact byte and alphabet bounds", "[core_ai]") {
  char exact[kMaxMemoryIdBytes + 1]{};
  std::memset(exact, 'a', kMaxMemoryIdBytes);
  TEST_ASSERT_TRUE(valid_memory_id(exact));
  char oversized[kMaxMemoryIdBytes + 2]{};
  std::memset(oversized, 'a', kMaxMemoryIdBytes + 1);
  TEST_ASSERT_FALSE(valid_memory_id(oversized));
  TEST_ASSERT_TRUE(valid_memory_id("room_1-history"));
  TEST_ASSERT_FALSE(valid_memory_id(""));
  TEST_ASSERT_FALSE(valid_memory_id(nullptr));
  TEST_ASSERT_FALSE(valid_memory_id("room/history"));
  TEST_ASSERT_FALSE(valid_memory_id("room name"));
}

TEST_CASE("AI memory removal persists absence without losing other sections", "[core_ai]") {
  core_security::DeviceSeed seed;
  std::uint8_t seed_bytes[core_security::kSeedBytes]{};
  seed.assign(seed_bytes);
  MemoryStore store;
  core_security::StorageService storage(seed, store);
  core_security::EspHardwareRandom random;
  MemoryRepository memory(storage, random);
  TEST_ASSERT_EQUAL(Status::NotReady, memory.remove("one"));
  TEST_ASSERT_EQUAL(Status::Ok, memory.start());
  TEST_ASSERT_EQUAL(Status::InvalidArgument, memory.remove("../bad"));
  TEST_ASSERT_EQUAL(Status::NotFound, memory.remove("one"));
  const std::uint8_t text[] = "private context";
  TEST_ASSERT_EQUAL(Status::Ok, memory.upsert("one", text, sizeof text - 1));
  TEST_ASSERT_EQUAL(Status::Ok, memory.upsert("two", text, sizeof text - 1));
  const auto original = store.record;
  store.fail_write = true;
  TEST_ASSERT_EQUAL(Status::StorageError, memory.remove("one"));
  TEST_ASSERT_TRUE(original == store.record);
  store.fail_write = false;
  TEST_ASSERT_EQUAL(Status::Ok, memory.remove("one"));
  MemoryRepository reopened(storage, random);
  TEST_ASSERT_EQUAL(Status::Ok, reopened.start());
  std::uint8_t output[64]{};
  std::size_t size{};
  TEST_ASSERT_EQUAL(Status::NotFound, reopened.get("one", output, sizeof output, size));
  TEST_ASSERT_EQUAL(Status::Ok, reopened.get("two", output, sizeof output, size));
  TEST_ASSERT_EQUAL_MEMORY(text, output, sizeof text - 1);
  const unsigned writes = store.writes;
  TEST_ASSERT_EQUAL(Status::NotFound, reopened.remove("one"));
  TEST_ASSERT_EQUAL_UINT(writes, store.writes);
  const auto intact = store.record;
  store.record.back() ^= 1;
  TEST_ASSERT_EQUAL(Status::CorruptData, reopened.remove("two"));
  TEST_ASSERT_EQUAL_UINT(writes, store.writes);
  store.record = intact;
  TEST_ASSERT_EQUAL(Status::Ok, reopened.remove("two"));
  unsigned visited = 0;
  TEST_ASSERT_EQUAL(Status::Ok, reopened.visit(
                                    [](const MemorySectionView&, void* context) {
                                      ++*static_cast<unsigned*>(context);
                                      return true;
                                    },
                                    &visited));
  TEST_ASSERT_EQUAL_UINT(0, visited);
  TEST_ASSERT_EQUAL(Status::NotFound, reopened.get("two", output, sizeof output, size));
  TEST_ASSERT_FALSE(store.record.empty());
}

TEST_CASE("AI prompt paths allow portable path syntax within byte bounds", "[core_ai]") {
  TEST_ASSERT_TRUE(valid_prompt_path("system/baseline_v1.md"));
  TEST_ASSERT_TRUE(valid_prompt_path("a.b-c_d/e"));
  TEST_ASSERT_FALSE(valid_prompt_path(""));
  TEST_ASSERT_FALSE(valid_prompt_path("../prompt:bad"));
  TEST_ASSERT_FALSE(valid_prompt_path("prompt name.md"));
  char oversized[kMaxPromptPathBytes + 2]{};
  std::memset(oversized, 'p', kMaxPromptPathBytes + 1);
  TEST_ASSERT_FALSE(valid_prompt_path(oversized));
}

TEST_CASE("AI provider usability binds supported HTTPS origins and auth", "[core_ai]") {
  ProviderConfig config = openai_config();
  TEST_ASSERT_TRUE(provider_config_usable(config));
  std::strcpy(config.base_url, "https://API.OPENAI.COM:443/v1");
  TEST_ASSERT_TRUE(provider_config_usable(config));
  std::strcpy(config.base_url, "http://api.openai.com/v1");
  TEST_ASSERT_FALSE(provider_config_usable(config));
  std::strcpy(config.base_url, "https://api.openai.com:444/v1");
  TEST_ASSERT_FALSE(provider_config_usable(config));
  std::strcpy(config.base_url, "https://user@api.openai.com/v1");
  TEST_ASSERT_FALSE(provider_config_usable(config));
  config = openai_config();
  config.auth_type = AuthType::ApiKey;
  TEST_ASSERT_FALSE(provider_config_usable(config));
  config = openai_config();
  config.api_key_length = 0;
  TEST_ASSERT_FALSE(provider_config_usable(config));
}

TEST_CASE("AI provider numeric and finite-value boundaries are enforced", "[core_ai]") {
  ProviderConfig config = openai_config();
  config.max_output_tokens = 1;
  config.timeout_ms = 1000;
  TEST_ASSERT_TRUE(provider_config_usable(config));
  config.max_output_tokens = 8192;
  config.timeout_ms = 120000;
  TEST_ASSERT_TRUE(provider_config_usable(config));
  config.max_output_tokens = 8193;
  TEST_ASSERT_FALSE(provider_config_usable(config));
  config = openai_config();
  config.timeout_ms = 999;
  TEST_ASSERT_FALSE(provider_config_usable(config));
  config = openai_config();
  config.temperature = NAN;
  TEST_ASSERT_FALSE(provider_config_usable(config));
  config.has_temperature = false;
  TEST_ASSERT_TRUE(provider_config_usable(config));
}

TEST_CASE("AI redacted credentials stay within the same provider scope", "[core_ai]") {
  ProviderConfig stored = openai_config();
  ProviderConfig incoming = stored;
  std::strcpy(incoming.base_url, "https://API.OPENAI.COM:443/other");
  std::strcpy(incoming.model, "another-model");
  TEST_ASSERT_TRUE(same_provider_credential_scope(incoming, stored));
  std::strcpy(incoming.base_url, "https://api.anthropic.com/v1");
  TEST_ASSERT_FALSE(same_provider_credential_scope(incoming, stored));
  incoming = stored;
  incoming.auth_type = AuthType::ApiKey;
  TEST_ASSERT_FALSE(same_provider_credential_scope(incoming, stored));
  incoming = stored;
  incoming.backend = ProviderBackend::AnthropicCompatible;
  TEST_ASSERT_FALSE(same_provider_credential_scope(incoming, stored));
}
