#include "core_security.hpp"

#include "mbedtls/platform_util.h"
#include "psa/crypto.h"

#include <cstring>

namespace core_security {
namespace {
constexpr uint8_t kMagic[] = {'C', 'P', 'E', 'R'};
constexpr uint8_t kSeedMagic[] = {'C', 'P', 'S', 'E', 'E', 'D', '0', '1'};
constexpr char kSalt[] = "CorePilot Device Seed HKDF v1";
constexpr const char* kLabels[] = {"core_pilot:v1:filesystem",    "core_pilot:v1:matter",
                                   "core_pilot:v1:thread",        "core_pilot:v1:ai",
                                   "core_pilot:v1:configuration", "core_pilot:v1:conversation",
                                   "core_pilot:v1:backup",        "core_pilot:v1:diagnostics"};
constexpr size_t kNonceSize = 12;
constexpr size_t kTagSize = 16;
constexpr size_t kAadPrefix = 4 + 1 + 1 + 1 + 8 + 2;

struct ClearOnExit {
  uint8_t* ptr;
  size_t size;
  ~ClearOnExit() {
    mbedtls_platform_zeroize(ptr, size);
  }
};

struct PsaKey final {
  psa_key_id_t id{0};
  ~PsaKey() {
    if (id != 0) {
      psa_destroy_key(id);
    }
  }
};

bool import_aead_key(const uint8_t (&key)[32], psa_key_usage_t usage, PsaKey& imported) {
  if (psa_crypto_init() != PSA_SUCCESS) {
    return false;
  }
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attributes, PSA_KEY_TYPE_CHACHA20);
  psa_set_key_bits(&attributes, 256);
  psa_set_key_usage_flags(&attributes, usage);
  psa_set_key_algorithm(&attributes, PSA_ALG_CHACHA20_POLY1305);
  const psa_status_t result = psa_import_key(&attributes, key, sizeof(key), &imported.id);
  psa_reset_key_attributes(&attributes);
  return result == PSA_SUCCESS;
}

bool valid_spec(const RecordSpec& s) {
  return s.path && s.path_len <= kMaxPathBytes && static_cast<unsigned>(s.name_space) < 8 &&
         static_cast<unsigned>(s.type) <=
             static_cast<unsigned>(RecordType::MatterCommissioningBoot);
}
void le(uint8_t* out, uint64_t value, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    out[i] = static_cast<uint8_t>(value);
    value >>= 8;
  }
}
uint64_t from_le(const uint8_t* in, size_t count) {
  uint64_t value = 0;
  for (size_t i = 0; i < count; ++i)
    value |= uint64_t(in[i]) << (8 * i);
  return value;
}
size_t varint_size(uint64_t value) {
  size_t size = 1;
  while (value >= 128) {
    ++size;
    value >>= 7;
  }
  return size;
}
size_t write_varint(uint8_t* out, uint64_t value) {
  size_t i = 0;
  while (value >= 128) {
    out[i++] = static_cast<uint8_t>(value) | 0x80;
    value >>= 7;
  }
  out[i++] = static_cast<uint8_t>(value);
  return i;
}
bool read_varint(const uint8_t* input, size_t size, size_t& pos, uint64_t& value) {
  value = 0;
  const size_t start = pos;
  for (size_t i = 0; i < 10 && pos < size; ++i) {
    uint8_t byte = input[pos++];
    if (i == 9 && byte > 1)
      return false;
    value |= uint64_t(byte & 0x7f) << (7 * i);
    if (!(byte & 0x80))
      return pos - start == varint_size(value);
  }
  return false;
}
Status derive(const DeviceSeed& seed, Namespace ns, uint8_t (&key)[32]) {
  if (psa_crypto_init() != PSA_SUCCESS) {
    return Status::CryptoError;
  }
  const char* label = kLabels[static_cast<unsigned>(ns)];
  psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
  psa_status_t result = psa_key_derivation_setup(&operation, PSA_ALG_HKDF(PSA_ALG_SHA_256));
  if (result == PSA_SUCCESS) {
    result =
        psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                       reinterpret_cast<const uint8_t*>(kSalt), sizeof(kSalt) - 1);
  }
  if (result == PSA_SUCCESS) {
    result = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET,
                                            seed.data(), kSeedBytes);
  }
  if (result == PSA_SUCCESS) {
    result =
        psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_INFO,
                                       reinterpret_cast<const uint8_t*>(label), std::strlen(label));
  }
  if (result == PSA_SUCCESS) {
    result = psa_key_derivation_output_bytes(&operation, key, sizeof(key));
  }
  psa_key_derivation_abort(&operation);
  return result == PSA_SUCCESS ? Status::Ok : Status::CryptoError;
}
size_t aad(const RecordSpec& spec, uint8_t* out) {
  std::memcpy(out, kMagic, sizeof(kMagic));
  out[4] = 1;
  out[5] = static_cast<uint8_t>(spec.type);
  out[6] = static_cast<uint8_t>(spec.name_space);
  le(out + 7, spec.generation, 8);
  le(out + 15, spec.schema_version, 2);
  std::memcpy(out + kAadPrefix, spec.path, spec.path_len);
  return kAadPrefix + spec.path_len;
}
uint32_t checksum(const uint8_t* seed) {
  uint32_t result = 0xa5a55a5a;
  for (size_t i = 0; i < kSeedBytes; ++i)
    result = (result << 5 | result >> 27) ^ seed[i];
  return result;
}
} // namespace

DeviceSeed::~DeviceSeed() {
  clear();
}
void DeviceSeed::clear() {
  mbedtls_platform_zeroize(bytes_, sizeof(bytes_));
}
void DeviceSeed::assign(const uint8_t (&bytes)[kSeedBytes]) {
  std::memcpy(bytes_, bytes, sizeof(bytes_));
}

Status load_or_generate(InternalSeedStore& store, RandomSource& rng, DeviceSeed& seed,
                        SeedOrigin& origin) {
  seed.clear();
  uint8_t record[kSeedRecordBytes]{};
  ClearOnExit wipe{record, sizeof(record)};
  Status status = store.read(record);
  if (status == Status::Ok) {
    if (std::memcmp(record, kSeedMagic, sizeof(kSeedMagic)) != 0 ||
        from_le(record + 40, 4) != checksum(record + 8))
      return Status::Invalid;
    uint8_t loaded[kSeedBytes];
    std::memcpy(loaded, record + 8, sizeof(loaded));
    seed.assign(loaded);
    mbedtls_platform_zeroize(loaded, sizeof(loaded));
    origin = SeedOrigin::Existing;
    return Status::Ok;
  }
  if (status != Status::NotFound)
    return status;
  std::memset(record, 0xff, sizeof(record));
  std::memcpy(record, kSeedMagic, sizeof(kSeedMagic));
  status = rng.fill(record + 8, kSeedBytes);
  if (status != Status::Ok)
    return status;
  le(record + 40, checksum(record + 8), 4);
  status = store.write(record);
  if (status != Status::Ok)
    return status;
  // Verify before accepting a newly persisted key. Never regenerate on a failed
  // read.
  uint8_t verified[kSeedRecordBytes]{};
  ClearOnExit wipe_verified{verified, sizeof(verified)};
  status = store.read(verified);
  if (status != Status::Ok)
    return status == Status::NotFound ? Status::IoError : status;
  if (std::memcmp(record, verified, sizeof(record)) != 0)
    return Status::IoError;
  uint8_t loaded[kSeedBytes];
  std::memcpy(loaded, record + 8, sizeof(loaded));
  seed.assign(loaded);
  mbedtls_platform_zeroize(loaded, sizeof(loaded));
  origin = SeedOrigin::Generated;
  return Status::Ok;
}
Status destroy_seed(InternalSeedStore& store, DeviceSeed& seed) {
  Status status = store.destroy();
  if (status == Status::Ok)
    seed.clear();
  return status;
}

Status encrypt(const DeviceSeed& seed, const RecordSpec& spec, const uint8_t* plaintext,
               size_t plaintext_len, RandomSource& rng, uint8_t* output, size_t capacity,
               size_t& written) {
  written = 0;
  if (!valid_spec(spec) || (!plaintext && plaintext_len) || !output)
    return Status::Invalid;
  if (plaintext_len > kMaxPlaintext)
    return Status::TooLarge;
  const size_t cipher_len = plaintext_len + kTagSize;
  const size_t required = 4 + 1 + 1 + 1 + varint_size(spec.generation) +
                          varint_size(spec.schema_version) + kNonceSize + varint_size(cipher_len) +
                          cipher_len;
  if (capacity < required)
    return Status::BufferTooSmall;
  uint8_t key[32]{};
  ClearOnExit wipe_key{key, sizeof(key)};
  uint8_t auth[kAadPrefix + kMaxPathBytes]{};
  ClearOnExit wipe_aad{auth, sizeof(auth)};
  Status status = derive(seed, spec.name_space, key);
  if (status != Status::Ok)
    return status;
  size_t pos = 0;
  std::memcpy(output, kMagic, sizeof(kMagic));
  pos += sizeof(kMagic);
  output[pos++] = 1;
  output[pos++] = static_cast<uint8_t>(spec.type);
  output[pos++] = static_cast<uint8_t>(spec.name_space);
  pos += write_varint(output + pos, spec.generation);
  pos += write_varint(output + pos, spec.schema_version);
  status = rng.fill(output + pos, kNonceSize);
  if (status != Status::Ok) {
    mbedtls_platform_zeroize(output, required);
    return status;
  }
  const uint8_t* nonce = output + pos;
  pos += kNonceSize;
  pos += write_varint(output + pos, cipher_len);
  const size_t auth_len = aad(spec, auth);
  PsaKey imported;
  size_t encrypted_size = 0;
  const bool key_ready = import_aead_key(key, PSA_KEY_USAGE_ENCRYPT, imported);
  const psa_status_t result =
      key_ready ? psa_aead_encrypt(imported.id, PSA_ALG_CHACHA20_POLY1305, nonce, kNonceSize, auth,
                                   auth_len, plaintext, plaintext_len, output + pos, cipher_len,
                                   &encrypted_size)
                : PSA_ERROR_INVALID_HANDLE;
  if (result != PSA_SUCCESS || encrypted_size != cipher_len) {
    mbedtls_platform_zeroize(output, required);
    return Status::CryptoError;
  }
  written = required;
  return Status::Ok;
}

Status decrypt(const DeviceSeed& seed, const RecordSpec& expected, const uint8_t* record,
               size_t record_len, uint8_t* plaintext, size_t capacity, size_t& written,
               Metadata& metadata) {
  written = 0;
  metadata = {};
  if (!valid_spec(expected) || !record || !plaintext)
    return Status::Invalid;
  if (record_len > kMaxRecordBytes)
    return Status::TooLarge;
  if (record_len < 4 || std::memcmp(record, kMagic, 4) != 0)
    return Status::Invalid;
  size_t pos = 4;
  if (record_len - pos < 3)
    return Status::Invalid;
  uint8_t version = record[pos++];
  if (version != 1)
    return Status::UnsupportedFormat;
  uint8_t type = record[pos++];
  uint8_t ns = record[pos++];
  if (type > static_cast<uint8_t>(RecordType::MatterCommissioningBoot) || ns >= 8)
    return Status::Invalid;
  uint64_t generation = 0, schema = 0, cipher_len = 0;
  if (!read_varint(record, record_len, pos, generation) ||
      !read_varint(record, record_len, pos, schema) || schema > UINT16_MAX ||
      record_len - pos < kNonceSize)
    return Status::Invalid;
  const uint8_t* nonce = record + pos;
  pos += kNonceSize;
  if (!read_varint(record, record_len, pos, cipher_len) || cipher_len < kTagSize ||
      cipher_len > kMaxPlaintext + kTagSize || cipher_len != record_len - pos)
    return Status::Invalid;
  if (ns != static_cast<uint8_t>(expected.name_space) ||
      type != static_cast<uint8_t>(expected.type))
    return Status::AuthenticationFailed;
  size_t plain_len = static_cast<size_t>(cipher_len) - kTagSize;
  if (capacity < plain_len)
    return Status::BufferTooSmall;
  RecordSpec actual = expected;
  actual.generation = generation;
  actual.schema_version = static_cast<uint16_t>(schema);
  uint8_t key[32]{};
  ClearOnExit wipe_key{key, sizeof(key)};
  uint8_t auth[kAadPrefix + kMaxPathBytes]{};
  ClearOnExit wipe_aad{auth, sizeof(auth)};
  Status status = derive(seed, expected.name_space, key);
  if (status != Status::Ok)
    return status;
  const size_t auth_len = aad(actual, auth);
  PsaKey imported;
  size_t decrypted_size = 0;
  const bool key_ready = import_aead_key(key, PSA_KEY_USAGE_DECRYPT, imported);
  const psa_status_t result =
      key_ready ? psa_aead_decrypt(imported.id, PSA_ALG_CHACHA20_POLY1305, nonce, kNonceSize, auth,
                                   auth_len, record + pos, cipher_len, plaintext, capacity,
                                   &decrypted_size)
                : PSA_ERROR_INVALID_HANDLE;
  if (result != PSA_SUCCESS || decrypted_size != plain_len) {
    mbedtls_platform_zeroize(plaintext, plain_len);
    return result == PSA_ERROR_INVALID_SIGNATURE ? Status::AuthenticationFailed
                                                 : Status::CryptoError;
  }
  written = plain_len;
  metadata = {actual.schema_version, actual.generation};
  return Status::Ok;
}

Status StorageService::put(const RecordSpec& spec, const uint8_t* data, size_t size,
                           RandomSource& rng, uint8_t* scratch, size_t scratch_capacity) {
  size_t written = 0;
  Status status = encrypt(seed_, spec, data, size, rng, scratch, scratch_capacity, written);
  if (status != Status::Ok)
    return status;
  ClearOnExit wipe{scratch, written};
  return store_.write_atomic(spec.path, spec.path_len, scratch, written);
}
Status StorageService::archive_and_put(const RecordSpec& spec, const char* archive_path,
                                       size_t archive_path_len, const uint8_t* data, size_t size,
                                       RandomSource& rng, uint8_t* scratch,
                                       size_t scratch_capacity) {
  size_t written = 0;
  Status status = encrypt(seed_, spec, data, size, rng, scratch, scratch_capacity, written);
  if (status != Status::Ok)
    return status;
  ClearOnExit wipe{scratch, written};
  return store_.archive_and_replace(spec.path, spec.path_len, archive_path, archive_path_len,
                                    scratch, written);
}

Status StorageService::get(const RecordSpec& spec, uint8_t* scratch, size_t scratch_capacity,
                           uint8_t* data, size_t capacity, size_t& size, Metadata& metadata) {
  size = 0;
  if (!valid_spec(spec) || !scratch || scratch_capacity > kMaxRecordBytes)
    return Status::Invalid;
  ClearOnExit wipe{scratch, scratch_capacity};
  size_t stored = 0;
  Status status = store_.read(spec.path, spec.path_len, scratch, scratch_capacity, stored);
  if (status != Status::Ok)
    return status;
  if (stored > scratch_capacity || stored > kMaxRecordBytes)
    return Status::TooLarge;
  return decrypt(seed_, spec, scratch, stored, data, capacity, size, metadata);
}
} // namespace core_security
