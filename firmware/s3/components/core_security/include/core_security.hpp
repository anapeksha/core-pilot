#pragma once

#include <cstddef>
#include <cstdint>

namespace core_security {

constexpr size_t kSeedBytes = 32;
constexpr size_t kSeedRecordBytes = 64;
constexpr size_t kMaxPlaintext = 1024 * 1024;
constexpr size_t kMaxPathBytes = 1024;
// CPER + postcard header (worst-case 10-byte generation and 3-byte schema)
// + 5-byte length + ciphertext and 16-byte tag.
constexpr size_t kMaxRecordBytes = 4 + 1 + 1 + 1 + 10 + 3 + 12 + 5 + kMaxPlaintext + 16;

// No error includes key material, paths, or record contents.
enum class Status {
  Ok,
  NotFound,
  Invalid,
  TooLarge,
  AuthenticationFailed,
  UnsupportedFormat,
  IoError,
  CryptoError,
  BufferTooSmall
};
enum class Namespace : uint8_t {
  Filesystem,
  Matter,
  Thread,
  Ai,
  Configuration,
  Conversation,
  Backup,
  Diagnostics
};
enum class RecordType : uint8_t {
  SystemConfig,
  WifiConfig,
  UiConfig,
  MatterFabric,
  MatterDevices,
  ThreadDataset,
  Rooms,
  Groups,
  Scenes,
  Automations,
  AiProvider,
  AiMemory,
  AiConversation,
  AiToolPermissions,
  OtaManifest,
  OtaChunk,
  Diagnostics,
  ModelsDatabase,
  AiPrompts,
  TimeConfig,
  SmtpConfig,
  ThreadActiveDataset,
  MatterKvImage,
  MatterControllerAttestation,
  MatterPaaAnchors,
  MatterTrustedProducts,
  OpenThreadSettings,
  MatterCommissioningBoot
};

// Only the internal-flash backend may persist these bytes. The owner is
// noncopyable and clears its contents on destruction. Do not put this object on
// SD or in logs.
class DeviceSeed final {
public:
  DeviceSeed() = default;
  ~DeviceSeed();
  DeviceSeed(const DeviceSeed&) = delete;
  DeviceSeed& operator=(const DeviceSeed&) = delete;
  const uint8_t* data() const {
    return bytes_;
  }
  void assign(const uint8_t (&bytes)[kSeedBytes]);
  void clear();

private:
  uint8_t bytes_[kSeedBytes]{};
};

// read: NotFound iff all 64 bytes are erased; never generate after a corrupt
// read. write: fresh seed only; destroy erases/verifies the entire dedicated
// partition.
class InternalSeedStore {
public:
  virtual ~InternalSeedStore() = default;
  virtual Status read(uint8_t (&record)[kSeedRecordBytes]) = 0;
  virtual Status write(const uint8_t (&record)[kSeedRecordBytes]) = 0;
  virtual Status destroy() = 0;
};
class RandomSource {
public:
  virtual ~RandomSource() = default;
  virtual Status fill(uint8_t* out, size_t size) = 0;
};
enum class SeedOrigin { Existing, Generated };
Status load_or_generate(InternalSeedStore& store, RandomSource& rng, DeviceSeed& seed,
                        SeedOrigin& origin);
Status destroy_seed(InternalSeedStore& store, DeviceSeed& seed);

// ESP-IDF adapters: the seed partition is internal flash at 0x920000, size
// 0x10000.
class EspSeedPartition final : public InternalSeedStore {
public:
  Status read(uint8_t (&record)[kSeedRecordBytes]) override;
  Status write(const uint8_t (&record)[kSeedRecordBytes]) override;
  Status destroy() override;
};
class EspHardwareRandom final : public RandomSource {
public:
  Status fill(uint8_t* out, size_t size) override;
};

struct RecordSpec {
  const char* path; // UTF-8 bytes, not including terminating NUL
  size_t path_len;
  Namespace name_space;
  RecordType type;
  uint16_t schema_version;
  uint64_t generation;
};
struct Metadata {
  uint16_t schema_version;
  uint64_t generation;
};

// Wire: "CPER" + postcard(EncryptedRecord { header, ciphertext }).
// Payload bytes are caller-serialized postcard data; no C++ struct
// serialization. Buffers must not overlap. Encrypt output may be as large as
// kMaxRecordBytes.
Status encrypt(const DeviceSeed& seed, const RecordSpec& spec, const uint8_t* plaintext,
               size_t plaintext_len, RandomSource& rng, uint8_t* output, size_t capacity,
               size_t& written);
Status decrypt(const DeviceSeed& seed, const RecordSpec& expected, const uint8_t* record,
               size_t record_len, uint8_t* plaintext, size_t capacity, size_t& written,
               Metadata& metadata);

// Implementations must atomically replace a record, never expose partial or
// plaintext writes, and reject overflow instead of truncating a read. NotFound
// is a valid read.
class EncryptedStore {
public:
  virtual ~EncryptedStore() = default;
  virtual Status write_atomic(const char* path, size_t path_len, const uint8_t* record,
                              size_t size) = 0;
  virtual Status read(const char* path, size_t path_len, uint8_t* record, size_t capacity,
                      size_t& size) = 0;
  virtual Status archive(const char*, size_t, const char*, size_t) {
    return Status::UnsupportedFormat;
  }
  virtual Status archive_and_replace(const char*, size_t, const char*, size_t, const uint8_t*,
                                     size_t) {
    return Status::UnsupportedFormat;
  }
};
class StorageService final {
public:
  StorageService(const DeviceSeed& seed, EncryptedStore& store) : seed_(seed), store_(store) {}
  // scratch must hold kMaxRecordBytes at most; supplied by caller, not stack.
  Status put(const RecordSpec& spec, const uint8_t* data, size_t size, RandomSource& rng,
             uint8_t* scratch, size_t scratch_capacity);
  Status get(const RecordSpec& spec, uint8_t* scratch, size_t scratch_capacity, uint8_t* data,
             size_t capacity, size_t& size, Metadata& metadata);
  Status archive(const char* path, size_t path_len, const char* archive_path,
                 size_t archive_path_len) {
    return store_.archive(path, path_len, archive_path, archive_path_len);
  }
  Status archive_and_put(const RecordSpec& spec, const char* archive_path, size_t archive_path_len,
                         const uint8_t* data, size_t size, RandomSource& rng, uint8_t* scratch,
                         size_t scratch_capacity);

private:
  const DeviceSeed& seed_;
  EncryptedStore& store_;
};

} // namespace core_security
