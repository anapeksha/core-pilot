#include "core_security.hpp"
#include "unity.h"
#include "unity_test_runner.h"

#include <array>
#include <cstring>

using namespace core_security;
namespace {
struct FixedRandom final : RandomSource {
  uint8_t next = 0;
  Status fill(uint8_t* out, size_t size) override {
    for (size_t i = 0; i < size; ++i)
      out[i] = next++;
    return Status::Ok;
  }
};
struct MemorySeed final : InternalSeedStore {
  uint8_t bytes[kSeedRecordBytes];
  MemorySeed() {
    std::memset(bytes, 0xff, sizeof(bytes));
  }
  Status read(uint8_t (&record)[kSeedRecordBytes]) override {
    std::memcpy(record, bytes, sizeof(bytes));
    for (uint8_t b : bytes)
      if (b != 0xff)
        return Status::Ok;
    return Status::NotFound;
  }
  Status write(const uint8_t (&record)[kSeedRecordBytes]) override {
    std::memcpy(bytes, record, sizeof(bytes));
    return Status::Ok;
  }
  Status destroy() override {
    std::memset(bytes, 0xff, sizeof(bytes));
    return Status::Ok;
  }
};
struct MemoryStore final : EncryptedStore {
  std::array<uint8_t, 256> bytes{};
  size_t len = 0;
  Status write_atomic(const char*, size_t, const uint8_t* record, size_t size) override {
    if (size > bytes.size())
      return Status::BufferTooSmall;
    std::memcpy(bytes.data(), record, size);
    len = size;
    return Status::Ok;
  }
  Status read(const char*, size_t, uint8_t* record, size_t capacity, size_t& size) override {
    if (!len)
      return Status::NotFound;
    if (capacity < len)
      return Status::BufferTooSmall;
    std::memcpy(record, bytes.data(), len);
    size = len;
    return Status::Ok;
  }
};
constexpr char kPath[] = "/core-pilot/config/wifi.enc";
RecordSpec spec() {
  return {kPath, sizeof(kPath) - 1, Namespace::Configuration, RecordType::WifiConfig, 2, 300};
}
} // namespace

TEST_CASE("seed partition preserves CPSEED01 layout and refuses corruption", "[core_security]") {
  MemorySeed flash;
  FixedRandom rng;
  DeviceSeed seed;
  SeedOrigin origin = SeedOrigin::Existing;
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok),
                    static_cast<int>(load_or_generate(flash, rng, seed, origin)));
  TEST_ASSERT_EQUAL(static_cast<int>(SeedOrigin::Generated), static_cast<int>(origin));
  TEST_ASSERT_EQUAL_MEMORY("CPSEED01", flash.bytes, 8);
  TEST_ASSERT_EQUAL_MEMORY(seed.data(), flash.bytes + 8, kSeedBytes);
  TEST_ASSERT_EQUAL_HEX8(0xff, flash.bytes[63]);
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok),
                    static_cast<int>(load_or_generate(flash, rng, seed, origin)));
  TEST_ASSERT_EQUAL(static_cast<int>(SeedOrigin::Existing), static_cast<int>(origin));
  flash.bytes[40] ^= 1;
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(load_or_generate(flash, rng, seed, origin)));
  for (size_t i = 0; i < kSeedBytes; ++i)
    TEST_ASSERT_EQUAL_HEX8(0, seed.data()[i]);
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok), static_cast<int>(destroy_seed(flash, seed)));
}

TEST_CASE("CPER postcard header and AAD bind identity path and tag", "[core_security]") {
  uint8_t key[kSeedBytes];
  std::memset(key, 7, sizeof(key));
  DeviceSeed seed;
  seed.assign(key);
  FixedRandom rng;
  auto identity = spec();
  const uint8_t message[] = {'s', 'e', 'c', 'r', 'e', 't'};
  uint8_t record[128]{};
  size_t length = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok),
                    static_cast<int>(encrypt(seed, identity, message, sizeof(message), rng, record,
                                             sizeof(record), length)));
  // Serde enum discriminants are unsigned postcard varints; generation 300 is
  // ac 02.
  const uint8_t prefix[] = {'C', 'P', 'E', 'R', 1, 1, 4, 0xac, 0x02, 2,  0, 1,
                            2,   3,   4,   5,   6, 7, 8, 9,    10,   11, 22};
  TEST_ASSERT_EQUAL_MEMORY(prefix, record, sizeof(prefix));
  TEST_ASSERT_EQUAL(sizeof(prefix) + sizeof(message) + 16, length);
  uint8_t plain[32]{};
  size_t size = 0;
  Metadata metadata{};
  auto check = [&](Status expected) {
    return decrypt(seed, identity, record, length, plain, sizeof(plain), size, metadata) ==
           expected;
  };
  TEST_ASSERT_TRUE(check(Status::Ok));
  TEST_ASSERT_EQUAL_MEMORY(message, plain, sizeof(message));
  TEST_ASSERT_EQUAL_UINT64(300, metadata.generation);
  TEST_ASSERT_EQUAL_UINT16(2, metadata.schema_version);
  identity.path = "/other";
  identity.path_len = 6;
  TEST_ASSERT_TRUE(check(Status::AuthenticationFailed));
  identity = spec();
  identity.name_space = Namespace::Ai;
  TEST_ASSERT_TRUE(check(Status::AuthenticationFailed));
  identity = spec();
  uint8_t different[kSeedBytes]{};
  DeviceSeed wrong_seed;
  wrong_seed.assign(different);
  TEST_ASSERT_EQUAL(static_cast<int>(Status::AuthenticationFailed),
                    static_cast<int>(decrypt(wrong_seed, identity, record, length, plain,
                                             sizeof(plain), size, metadata)));
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Invalid),
                    static_cast<int>(decrypt(seed, identity, record, length - 1, plain,
                                             sizeof(plain), size, metadata)));
  record[8] ^= 1;
  TEST_ASSERT_TRUE(check(Status::AuthenticationFailed));
  record[8] ^= 1;
  record[length - 1] ^= 1;
  TEST_ASSERT_TRUE(check(Status::AuthenticationFailed));
  record[length - 1] ^= 1;
  record[7] = 0x80;
  record[8] = 0x00; // overlong postcard varint
  TEST_ASSERT_TRUE(check(Status::Invalid));
}

TEST_CASE("bounded storage never writes plaintext and rejects moved records", "[core_security]") {
  uint8_t key[kSeedBytes]{};
  DeviceSeed seed;
  seed.assign(key);
  MemoryStore store;
  StorageService service(seed, store);
  FixedRandom rng;
  uint8_t scratch[256]{};
  const uint8_t message[] = {'w', 'i', 'f', 'i'};
  auto identity = spec();
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok),
                    static_cast<int>(service.put(identity, message, sizeof(message), rng, scratch,
                                                 sizeof(scratch))));
  TEST_ASSERT_EQUAL_MEMORY("CPER", store.bytes.data(), 4);
  TEST_ASSERT_FALSE(std::memcmp(message, store.bytes.data(), sizeof(message)) == 0);
  uint8_t plain[16]{};
  size_t size = 0;
  Metadata metadata{};
  TEST_ASSERT_EQUAL(static_cast<int>(Status::Ok),
                    static_cast<int>(service.get(identity, scratch, sizeof(scratch), plain,
                                                 sizeof(plain), size, metadata)));
  TEST_ASSERT_EQUAL_MEMORY(message, plain, size);
  identity.path = "/other";
  identity.path_len = 6;
  TEST_ASSERT_EQUAL(static_cast<int>(Status::AuthenticationFailed),
                    static_cast<int>(service.get(identity, scratch, sizeof(scratch), plain,
                                                 sizeof(plain), size, metadata)));
  uint8_t large[1]{};
  identity = spec();
  TEST_ASSERT_EQUAL(static_cast<int>(Status::TooLarge),
                    static_cast<int>(encrypt(seed, identity, large, kMaxPlaintext + 1, rng, scratch,
                                             sizeof(scratch), size)));
}
