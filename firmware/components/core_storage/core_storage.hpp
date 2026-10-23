#pragma once

#include "core_security.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#include <cstddef>
#include <cstdint>

namespace core_storage {

// Matches firmware/src/storage/mod.rs::encrypted_record_fat_names. These files
// live in FAT volume 0's root, not in physical /core-pilot directories.
struct FatNames {
  const char* record;
  const char* temporary;
  const char* backup;
};
const FatNames* fat_names(const char* logical_path, size_t length);

// The storage cap applies to the complete encrypted CPER record.
constexpr size_t kMaxStoredBytes = 1024 * 1024;
// NVS needs free pages while replacing blobs. Keep journal records bounded so
// one record cannot consume the entire 2 MiB partition.
constexpr size_t kMaxFlashRecordBytes = 256 * 1024;
constexpr TickType_t kLockTicks = pdMS_TO_TICKS(1000);

struct IoDiagnostic final {
  const char* operation;
  int error_number;
};

// Reports only the operation category and errno from the latest failed write.
// It never contains a logical path, record contents, or cryptographic material.
IoDiagnostic last_io_diagnostic();

struct ReplicationDiagnostic final {
  bool storage_ready;
  bool sd_available;
  bool sd_synchronized;
  core_security::Status last_sd_status;
  std::uint32_t successful_sd_writes;
};

ReplicationDiagnostic replication_diagnostic();

// Stores authenticated CPER records in the dedicated internal-flash NVS
// partition. Device Seed material is owned by core_security and never enters
// this partition.
class FlashEncryptedStore final : public core_security::EncryptedStore {
public:
  FlashEncryptedStore();
  ~FlashEncryptedStore() override;
  FlashEncryptedStore(const FlashEncryptedStore&) = delete;
  FlashEncryptedStore& operator=(const FlashEncryptedStore&) = delete;

  core_security::Status begin();
  bool ready() const;
  core_security::Status write_atomic(const char* path, size_t path_len, const uint8_t* record,
                                     size_t size) override;
  core_security::Status read(const char* path, size_t path_len, uint8_t* record, size_t capacity,
                             size_t& size) override;
  core_security::Status archive(const char* path, size_t path_len, const char* archive_path,
                                size_t archive_path_len) override;
  core_security::Status archive_and_replace(const char* path, size_t path_len,
                                            const char* archive_path, size_t archive_path_len,
                                            const uint8_t* record, size_t size) override;

private:
  nvs_handle_t handle_{};
  StaticSemaphore_t mutex_buffer_{};
  SemaphoreHandle_t mutex_;
  bool ready_{};
};

// A VFS mount at /sdcard must ALREADY have been installed by the board's
// shared-LCD/SD bus owner. This class NEVER initializes SPI, controls GPIO35,
// mounts a card, calls M5.begin/update, or touches the display. Until an owner
// provides a verified CoreS3-safe mounted FAT volume, leave this store unused.
// Keep the mount alive and prohibit uncoordinated LCD traffic for the lifetime
// of this store. All application SD mutations must pass through StorageService.
class SdEncryptedStore final : public core_security::EncryptedStore {
public:
  SdEncryptedStore();
  SdEncryptedStore(const SdEncryptedStore&) = delete;
  SdEncryptedStore& operator=(const SdEncryptedStore&) = delete;
  core_security::Status write_atomic(const char* path, size_t path_len, const uint8_t* record,
                                     size_t size) override;
  core_security::Status read(const char* path, size_t path_len, uint8_t* record, size_t capacity,
                             size_t& size) override;
  core_security::Status archive(const char* path, size_t path_len, const char* archive_path,
                                size_t archive_path_len) override;
  core_security::Status archive_and_replace(const char* path, size_t path_len,
                                            const char* archive_path, size_t archive_path_len,
                                            const uint8_t* record, size_t size) override;

private:
  StaticSemaphore_t mutex_buffer_{};
  SemaphoreHandle_t mutex_;
};

// Flash is authoritative. SD receives best-effort encrypted replicas and can
// seed flash from legacy records when the primary has no copy.
class ReplicatedEncryptedStore final : public core_security::EncryptedStore {
public:
  ReplicatedEncryptedStore(FlashEncryptedStore& primary, SdEncryptedStore* replica);
  core_security::Status write_atomic(const char* path, size_t path_len, const uint8_t* record,
                                     size_t size) override;
  core_security::Status read(const char* path, size_t path_len, uint8_t* record, size_t capacity,
                             size_t& size) override;
  core_security::Status archive(const char* path, size_t path_len, const char* archive_path,
                                size_t archive_path_len) override;
  core_security::Status archive_and_replace(const char* path, size_t path_len,
                                            const char* archive_path, size_t archive_path_len,
                                            const uint8_t* record, size_t size) override;

private:
  void recordReplicaStatus(core_security::Status status);

  FlashEncryptedStore& primary_;
  SdEncryptedStore* replica_;
};

// Construct StorageService(seed, store), then use put/get with caller-owned,
// non-overlapping scratch buffers. Never supply a seed from SD or log payloads.
} // namespace core_storage
