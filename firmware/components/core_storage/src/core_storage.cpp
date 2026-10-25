#include "core_storage.hpp"

#include "core_board.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace core_storage {
namespace {
struct Entry {
  const char* logical;
  FatNames fat;
};
// Stable logical paths map to fixed FAT 8.3 names. Backup names are reserved
// for interrupted atomic-replacement recovery.
constexpr Entry kEntries[] = {
    {"/core-pilot/config/wifi.enc", {"WIFI.ENC", "WIFI.TMP", "WIFI.BAK"}},
    {"/core-pilot/config/ai_provider.enc", {"AIPROV.ENC", "AIPROV.TMP", "AIPROV.BAK"}},
    {"/core-pilot/ai/memory.enc", {"AIMEM.ENC", "AIMEM.TMP", "AIMEM.BAK"}},
    {"/core-pilot/ai/memory-v2.enc", {"AIMEM2.ENC", "AIMEM2.TMP", "AIMEM2.BAK"}},
    {"/core-pilot/ai/prompts.enc", {"AIPROM.ENC", "AIPROM.TMP", "AIPROM.BAK"}},
    {"/core-pilot/ai/tool-permissions.enc", {"AITOOL.ENC", "AITOOL.TMP", "AITOOL.BAK"}},
    {"/core-pilot/config/time.enc", {"TIME.ENC", "TIME.TMP", "TIME.BAK"}},
    {"/core-pilot/config/smtp.enc", {"SMTP.ENC", "SMTP.TMP", "SMTP.BAK"}},
    {"/core-pilot/matter/fabric.enc", {"FABRIC.ENC", "FABRIC.TMP", "FABRIC.BAK"}},
    {"/core-pilot/matter/devices.enc", {"MATDEV.ENC", "MATDEV.TMP", "MATDEV.BAK"}},
    {"/core-pilot/matter/devices-legacy.enc", {"MATOLD.ENC", "MATOLD.TMP", "MATOLD.BAK"}},
    {"/core-pilot/matter/kv-image.enc", {"MATKV.ENC", "MATKV.TMP", "MATKV.BAK"}},
    {"/core-pilot/matter/commissioning-boot.enc", {"MATBOOT.ENC", "MATBOOT.TMP", "MATBOOT.BAK"}},
    {"/core-pilot/matter/controller-attestation.enc", {"MATATT.ENC", "MATATT.TMP", "MATATT.BAK"}},
    {"/core-pilot/matter/paa-anchors.enc", {"PAA.ENC", "PAA.TMP", "PAA.BAK"}},
    {"/core-pilot/matter/trusted-products.enc", {"MATPROD.ENC", "MATPROD.TMP", "MATPROD.BAK"}},
    {"/core-pilot/thread/dataset.enc", {"THREAD.ENC", "THREAD.TMP", "THREAD.BAK"}},
    {"/core-pilot/thread/settings.enc", {"OTSET.ENC", "OTSET.TMP", "OTSET.BAK"}},
    {"/core-pilot/models/database.enc", {"MODELS.ENC", "MODELS.TMP", "MODELS.BAK"}},
    {"/core-pilot/models/database-unreadable.enc", {"MODBAD.ENC", "MODBAD.TMP", "MODBAD.BAK"}},
    {"/core-pilot/diagnostics/activity-history.enc",
     {"ACTIVITY.ENC", "ACTIVITY.TMP", "ACTIVITY.BAK"}},
    {"/core-pilot/diagnostics/activity-history-unreadable.enc",
     {"ACTBAD.ENC", "ACTBAD.TMP", "ACTBAD.BAK"}},
    {"/core-pilot/config/system.enc", {"SYSTEM.ENC", "SYSTEM.TMP", "SYSTEM.BAK"}},
    {"/core-pilot/config/ui.enc", {"UI.ENC", "UI.TMP", "UI.BAK"}},
    {"/core-pilot/diagnostics/storage-smoke.enc", {"SMOKE.ENC", "SMOKE.TMP", "SMOKE.BAK"}},
};
constexpr char kMount[] = "/sdcard/";
constexpr char kTag[] = "core_storage";
std::atomic<const char*> last_operation{"none"};
std::atomic<int> last_error{0};
std::atomic<bool> storage_ready{false};
std::atomic<bool> sd_available{false};
std::atomic<bool> sd_synchronized{false};
std::atomic<int> last_sd_status{static_cast<int>(core_security::Status::NotFound)};
std::atomic<std::uint32_t> successful_sd_writes{0};

core_security::Status nvs_status(esp_err_t error) {
  if (error == ESP_OK)
    return core_security::Status::Ok;
  if (error == ESP_ERR_NVS_NOT_FOUND)
    return core_security::Status::NotFound;
  if (error == ESP_ERR_NVS_INVALID_LENGTH)
    return core_security::Status::BufferTooSmall;
  return core_security::Status::IoError;
}

bool valid_record(const uint8_t* record, size_t size, size_t maximum) {
  return record != nullptr && size >= 4 && size <= maximum && std::memcmp(record, "CPER", 4) == 0;
}

// Mark a path before changing its flash blob. Once tracked, a missing flash
// record must never be silently resurrected from an older SD replica.
constexpr uint8_t kTracked = 1;

void anchor_key(const FatNames& names, char (&key)[16]) {
  key[0] = 'a';
  const size_t length = std::strlen(names.record);
  std::memcpy(key + 1, names.record, length + 1);
}

core_security::Status tracked_path(nvs_handle_t handle, const FatNames& names, bool& tracked) {
  char key[16];
  anchor_key(names, key);
  uint8_t value = 0;
  const esp_err_t result = nvs_get_u8(handle, key, &value);
  tracked = result != ESP_ERR_NVS_NOT_FOUND;
  if (!tracked)
    return core_security::Status::Ok; // one-time legacy SD import
  return result == ESP_OK && value == kTracked ? core_security::Status::Ok
                                               : core_security::Status::IoError;
}

esp_err_t mark_tracked(nvs_handle_t handle, const FatNames& names) {
  char key[16];
  anchor_key(names, key);
  return nvs_set_u8(handle, key, kTracked);
}

void record_io_error(const char* operation, int error) {
  last_error.store(error, std::memory_order_relaxed);
  last_operation.store(operation, std::memory_order_release);
  ESP_LOGE(kTag, "encrypted storage %s failed (errno=%d)", operation, error);
}

core_security::Status io_error(const char* operation) {
  record_io_error(operation, errno);
  return core_security::Status::IoError;
}

// Never accept a user-controlled physical pathname.
bool physical(const char* name, char (&out)[32]) {
  const size_t n = std::strlen(name);
  if (sizeof(kMount) - 1 + n >= sizeof(out))
    return false;
  std::memcpy(out, kMount, sizeof(kMount) - 1);
  std::memcpy(out + sizeof(kMount) - 1, name, n + 1);
  return true;
}

class Lock final {
public:
  explicit Lock(SemaphoreHandle_t mutex)
      : mutex_(mutex), held_(mutex && xSemaphoreTake(mutex, kLockTicks) == pdTRUE) {}
  ~Lock() {
    if (held_)
      xSemaphoreGive(mutex_);
  }
  bool held() const {
    return held_;
  }

private:
  SemaphoreHandle_t mutex_;
  bool held_;
};

bool exists(const char* path, bool& found) {
  struct stat st{};
  if (stat(path, &st) == 0) {
    found = true;
    return S_ISREG(st.st_mode);
  }
  found = false;
  return errno == ENOENT;
}

core_security::Status read_file(const char* path, uint8_t* out, size_t capacity, size_t& size) {
  const int fd = open(path, O_RDONLY);
  if (fd < 0)
    return errno == ENOENT ? core_security::Status::NotFound : core_security::Status::IoError;
  struct stat st{};
  core_security::Status result = core_security::Status::Ok;
  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
    result = core_security::Status::IoError;
  else if (st.st_size < 4 || static_cast<uint64_t>(st.st_size) > kMaxStoredBytes)
    result = core_security::Status::TooLarge;
  else if (static_cast<uint64_t>(st.st_size) > capacity)
    result = core_security::Status::BufferTooSmall;
  if (result == core_security::Status::Ok) {
    const size_t length = static_cast<size_t>(st.st_size);
    size_t pos = 0;
    while (pos < length) {
      const ssize_t got = ::read(fd, out + pos, length - pos);
      if (got <= 0) {
        result = core_security::Status::IoError;
        break;
      }
      pos += static_cast<size_t>(got);
    }
    if (result == core_security::Status::Ok && std::memcmp(out, "CPER", 4) != 0)
      result = core_security::Status::Invalid;
    if (result == core_security::Status::Ok)
      size = length;
  }
  if (close(fd) != 0)
    result = core_security::Status::IoError;
  return result;
}
} // namespace

core_security::Status missing_flash_status(bool tracked) {
  return tracked ? core_security::Status::AuthenticationFailed : core_security::Status::NotFound;
}

IoDiagnostic last_io_diagnostic() {
  const char* operation = last_operation.load(std::memory_order_acquire);
  return {operation, last_error.load(std::memory_order_relaxed)};
}

ReplicationDiagnostic replication_diagnostic() {
  return {storage_ready.load(std::memory_order_acquire),
          sd_available.load(std::memory_order_acquire),
          sd_synchronized.load(std::memory_order_acquire),
          static_cast<core_security::Status>(last_sd_status.load(std::memory_order_relaxed)),
          successful_sd_writes.load(std::memory_order_relaxed)};
}

const FatNames* fat_names(const char* path, size_t length) {
  if (!path || length == 0 || length > core_security::kMaxPathBytes)
    return nullptr;
  for (const auto& entry : kEntries)
    if (length == std::strlen(entry.logical) && std::memcmp(path, entry.logical, length) == 0)
      return &entry.fat;
  return nullptr;
}

FlashEncryptedStore::FlashEncryptedStore() : mutex_(xSemaphoreCreateMutexStatic(&mutex_buffer_)) {}

FlashEncryptedStore::~FlashEncryptedStore() {
  if (ready_)
    nvs_close(handle_);
}

core_security::Status FlashEncryptedStore::begin() {
  if (ready_)
    return core_security::Status::Ok;
  esp_err_t result = nvs_flash_init_partition("journal");
  if (result != ESP_OK)
    return nvs_status(result);
  result = nvs_open_from_partition("journal", "records", NVS_READWRITE, &handle_);
  if (result != ESP_OK)
    return nvs_status(result);
  ready_ = true;
  storage_ready.store(true, std::memory_order_release);
  return core_security::Status::Ok;
}

bool FlashEncryptedStore::ready() const {
  return ready_;
}

core_security::Status FlashEncryptedStore::read(const char* path, size_t path_len, uint8_t* record,
                                                size_t capacity, size_t& size) {
  size = 0;
  const FatNames* names = fat_names(path, path_len);
  if (!ready_ || !names || !record)
    return core_security::Status::Invalid;
  Lock lock(mutex_);
  if (!lock.held())
    return core_security::Status::IoError;
  size_t stored_size = 0;
  esp_err_t result = nvs_get_blob(handle_, names->record, nullptr, &stored_size);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    bool tracked = false;
    const auto status = tracked_path(handle_, *names, tracked);
    return status == core_security::Status::Ok ? missing_flash_status(tracked) : status;
  }
  if (result != ESP_OK)
    return nvs_status(result);
  if (stored_size < 4 || stored_size > kMaxFlashRecordBytes)
    return core_security::Status::TooLarge;
  if (stored_size > capacity)
    return core_security::Status::BufferTooSmall;
  result = nvs_get_blob(handle_, names->record, record, &stored_size);
  if (result != ESP_OK)
    return nvs_status(result);
  if (!valid_record(record, stored_size, kMaxFlashRecordBytes))
    return core_security::Status::Invalid;
  bool tracked = false;
  const auto status = tracked_path(handle_, *names, tracked);
  if (status != core_security::Status::Ok)
    return status;
  // Existing flash records predate markers. Adopt them before permitting reads.
  if (!tracked) {
    if (mark_tracked(handle_, *names) != ESP_OK || nvs_commit(handle_) != ESP_OK)
      return core_security::Status::IoError;
  }
  size = stored_size;
  return core_security::Status::Ok;
}

core_security::Status FlashEncryptedStore::write_atomic(const char* path, size_t path_len,
                                                        const uint8_t* record, size_t size) {
  const FatNames* names = fat_names(path, path_len);
  if (!ready_ || !names || !valid_record(record, size, kMaxFlashRecordBytes))
    return size > kMaxFlashRecordBytes ? core_security::Status::TooLarge
                                       : core_security::Status::Invalid;
  Lock lock(mutex_);
  if (!lock.held())
    return core_security::Status::IoError;
  esp_err_t result = mark_tracked(handle_, *names);
  if (result == ESP_OK)
    result = nvs_set_blob(handle_, names->record, record, size);
  if (result == ESP_OK)
    result = nvs_commit(handle_);
  return nvs_status(result);
}

core_security::Status FlashEncryptedStore::archive(const char* path, size_t path_len,
                                                   const char* archive_path,
                                                   size_t archive_path_len) {
  const FatNames* source = fat_names(path, path_len);
  const FatNames* destination = fat_names(archive_path, archive_path_len);
  if (!ready_ || !source || !destination || source == destination)
    return core_security::Status::Invalid;
  Lock lock(mutex_);
  if (!lock.held())
    return core_security::Status::IoError;
  size_t size = 0;
  esp_err_t result = nvs_get_blob(handle_, source->record, nullptr, &size);
  if (result != ESP_OK)
    return nvs_status(result);
  size_t destination_size = 0;
  result = nvs_get_blob(handle_, destination->record, nullptr, &destination_size);
  if (result == ESP_OK)
    return core_security::Status::Invalid;
  if (result != ESP_ERR_NVS_NOT_FOUND || size < 4 || size > kMaxFlashRecordBytes)
    return result == ESP_ERR_NVS_NOT_FOUND ? core_security::Status::TooLarge : nvs_status(result);
  auto* record = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!record)
    return core_security::Status::IoError;
  result = nvs_get_blob(handle_, source->record, record, &size);
  if (result == ESP_OK && valid_record(record, size, kMaxFlashRecordBytes))
    result = mark_tracked(handle_, *source);
  else if (result == ESP_OK)
    result = ESP_ERR_INVALID_CRC;
  if (result == ESP_OK)
    result = mark_tracked(handle_, *destination);
  if (result == ESP_OK)
    result = nvs_set_blob(handle_, destination->record, record, size);
  if (result == ESP_OK)
    result = nvs_erase_key(handle_, source->record);
  if (result == ESP_OK)
    result = nvs_commit(handle_);
  heap_caps_free(record);
  return nvs_status(result);
}

core_security::Status FlashEncryptedStore::archive_and_replace(const char* path, size_t path_len,
                                                               const char* archive_path,
                                                               size_t archive_path_len,
                                                               const uint8_t* record, size_t size) {
  const FatNames* source = fat_names(path, path_len);
  const FatNames* destination = fat_names(archive_path, archive_path_len);
  if (!ready_ || !source || !destination || source == destination ||
      !valid_record(record, size, kMaxFlashRecordBytes))
    return size > kMaxFlashRecordBytes ? core_security::Status::TooLarge
                                       : core_security::Status::Invalid;
  Lock lock(mutex_);
  if (!lock.held())
    return core_security::Status::IoError;
  size_t old_size = 0;
  esp_err_t result = nvs_get_blob(handle_, source->record, nullptr, &old_size);
  if (result != ESP_OK)
    return nvs_status(result);
  size_t destination_size = 0;
  result = nvs_get_blob(handle_, destination->record, nullptr, &destination_size);
  if (result == ESP_OK)
    return core_security::Status::Invalid;
  if (result != ESP_ERR_NVS_NOT_FOUND || old_size < 4 || old_size > kMaxFlashRecordBytes)
    return result == ESP_ERR_NVS_NOT_FOUND ? core_security::Status::TooLarge : nvs_status(result);
  auto* old_record =
      static_cast<uint8_t*>(heap_caps_malloc(old_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!old_record)
    return core_security::Status::IoError;
  result = nvs_get_blob(handle_, source->record, old_record, &old_size);
  if (result == ESP_OK && valid_record(old_record, old_size, kMaxFlashRecordBytes))
    result = mark_tracked(handle_, *source);
  else if (result == ESP_OK)
    result = ESP_ERR_INVALID_CRC;
  if (result == ESP_OK)
    result = mark_tracked(handle_, *destination);
  if (result == ESP_OK)
    result = nvs_set_blob(handle_, destination->record, old_record, old_size);
  if (result == ESP_OK)
    result = nvs_set_blob(handle_, source->record, record, size);
  if (result == ESP_OK)
    result = nvs_commit(handle_);
  heap_caps_free(old_record);
  return nvs_status(result);
}

SdEncryptedStore::SdEncryptedStore() : mutex_(xSemaphoreCreateMutexStatic(&mutex_buffer_)) {
  sd_available.store(true, std::memory_order_release);
  sd_synchronized.store(true, std::memory_order_release);
  storage_ready.store(true, std::memory_order_release);
}

core_security::Status SdEncryptedStore::read(const char* path, size_t path_len, uint8_t* record,
                                             size_t capacity, size_t& size) {
  size = 0;
  const FatNames* names = fat_names(path, path_len);
  if (!names || !record)
    return core_security::Status::Invalid;
  Lock lock(mutex_);
  if (!lock.held())
    return core_security::Status::IoError;
  core_pilot::board::SharedSpiGuard bus(core_pilot::board::SharedSpiGuard::Use::storage);
  if (!bus.locked())
    return core_security::Status::IoError;
  char primary[32], backup[32];
  if (!physical(names->record, primary) || !physical(names->backup, backup))
    return core_security::Status::Invalid;
  // A failed rename leaves the last complete encrypted file in .BAK.
  auto status = read_file(primary, record, capacity, size);
  if (status == core_security::Status::NotFound)
    status = read_file(backup, record, capacity, size);
  return status;
}

core_security::Status SdEncryptedStore::archive(const char* path, size_t path_len,
                                                const char* archive_path, size_t archive_path_len) {
  const FatNames* source_names = fat_names(path, path_len);
  const FatNames* archive_names = fat_names(archive_path, archive_path_len);
  if (!source_names || !archive_names || source_names == archive_names)
    return core_security::Status::Invalid;
  Lock lock(mutex_);
  if (!lock.held())
    return core_security::Status::IoError;
  core_pilot::board::SharedSpiGuard bus(core_pilot::board::SharedSpiGuard::Use::storage);
  if (!bus.locked())
    return core_security::Status::IoError;
  char source[32], destination[32];
  if (!physical(source_names->record, source) || !physical(archive_names->record, destination))
    return core_security::Status::Invalid;
  bool source_present = false;
  bool destination_present = false;
  if (!exists(source, source_present) || !exists(destination, destination_present))
    return core_security::Status::IoError;
  if (!source_present)
    return core_security::Status::NotFound;
  if (destination_present)
    return core_security::Status::Invalid;
  return rename(source, destination) == 0 ? core_security::Status::Ok
                                          : core_security::Status::IoError;
}

core_security::Status SdEncryptedStore::archive_and_replace(const char* path, size_t path_len,
                                                            const char* archive_path,
                                                            size_t archive_path_len,
                                                            const uint8_t* record, size_t size) {
  const FatNames* source_names = fat_names(path, path_len);
  const FatNames* archive_names = fat_names(archive_path, archive_path_len);
  if (!source_names || !archive_names || source_names == archive_names || !record || size < 4 ||
      std::memcmp(record, "CPER", 4) != 0)
    return core_security::Status::Invalid;
  if (size > kMaxStoredBytes)
    return core_security::Status::TooLarge;
  Lock lock(mutex_);
  if (!lock.held())
    return core_security::Status::IoError;
  core_pilot::board::SharedSpiGuard bus(core_pilot::board::SharedSpiGuard::Use::storage);
  if (!bus.locked())
    return core_security::Status::IoError;

  char source[32], temporary[32], archive[32];
  if (!physical(source_names->record, source) || !physical(source_names->temporary, temporary) ||
      !physical(archive_names->record, archive))
    return core_security::Status::Invalid;
  bool source_present = false;
  bool archive_present = false;
  if (!exists(source, source_present) || !exists(archive, archive_present))
    return core_security::Status::IoError;
  if (archive_present && source_present)
    return core_security::Status::Invalid;
  if (!archive_present && !source_present)
    return core_security::Status::NotFound;
  if (unlink(temporary) != 0 && errno != ENOENT)
    return core_security::Status::IoError;

  const int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return core_security::Status::IoError;
  size_t position = 0;
  bool written = true;
  while (position < size) {
    const ssize_t count = ::write(fd, record + position, size - position);
    if (count <= 0) {
      written = false;
      break;
    }
    position += static_cast<size_t>(count);
  }
  if (written && fsync(fd) != 0)
    written = false;
  if (close(fd) != 0)
    written = false;
  if (!written) {
    unlink(temporary);
    return core_security::Status::IoError;
  }

  if (!archive_present && rename(source, archive) != 0) {
    unlink(temporary);
    return core_security::Status::IoError;
  }
  if (rename(temporary, source) != 0) {
    if (!archive_present)
      rename(archive, source);
    return core_security::Status::IoError;
  }
  return core_security::Status::Ok;
}

core_security::Status SdEncryptedStore::write_atomic(const char* path, size_t path_len,
                                                     const uint8_t* record, size_t size) {
  const FatNames* names = fat_names(path, path_len);
  if (!names || !record)
    return core_security::Status::Invalid;
  if (size < 4 || std::memcmp(record, "CPER", 4) != 0)
    return core_security::Status::Invalid;
  if (size > kMaxStoredBytes)
    return core_security::Status::TooLarge;
  Lock lock(mutex_);
  if (!lock.held())
    return core_security::Status::IoError;
  core_pilot::board::SharedSpiGuard bus(core_pilot::board::SharedSpiGuard::Use::storage);
  if (!bus.locked())
    return core_security::Status::IoError;
  char primary[32], temporary[32], backup[32];
  if (!physical(names->record, primary) || !physical(names->temporary, temporary) ||
      !physical(names->backup, backup))
    return core_security::Status::Invalid;
  bool present = false;
  if (!exists(primary, present))
    return io_error("primary stat");
  // Do not discard the sole recoverable prior version after an interrupted
  // replace.
  if (present && unlink(backup) != 0 && errno != ENOENT)
    return io_error("backup unlink");
  if (unlink(temporary) != 0 && errno != ENOENT)
    return io_error("temporary unlink");
  const int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return io_error("temporary open");
  size_t pos = 0;
  bool ok = true;
  while (pos < size) {
    const ssize_t n = ::write(fd, record + pos, size - pos);
    if (n <= 0) {
      record_io_error("write", errno);
      ok = false;
      break;
    }
    pos += static_cast<size_t>(n);
  }
  if (ok && fsync(fd) != 0) {
    record_io_error("fsync", errno);
    ok = false;
  }
  if (close(fd) != 0) {
    record_io_error("close", errno);
    ok = false;
  }
  if (!ok) {
    unlink(temporary);
    return core_security::Status::IoError;
  }
  if (present && rename(primary, backup) != 0)
    return io_error("primary backup rename");
  if (rename(temporary, primary) != 0) {
    if (present)
      rename(backup, primary); // preserve .BAK if rollback fails
    return io_error("temporary primary rename");
  }
  // Best-effort cleanup; a .BAK still holds only authenticated ciphertext.
  unlink(backup);
  last_sd_status.store(static_cast<int>(core_security::Status::Ok), std::memory_order_relaxed);
  successful_sd_writes.fetch_add(1, std::memory_order_relaxed);
  return core_security::Status::Ok;
}

ReplicatedEncryptedStore::ReplicatedEncryptedStore(FlashEncryptedStore& primary,
                                                   SdEncryptedStore* replica)
    : primary_(primary), replica_(replica) {
  sd_available.store(replica != nullptr, std::memory_order_release);
  sd_synchronized.store(replica != nullptr, std::memory_order_release);
}

void ReplicatedEncryptedStore::recordReplicaStatus(core_security::Status status) {
  last_sd_status.store(static_cast<int>(status), std::memory_order_relaxed);
  if (status != core_security::Status::Ok && status != core_security::Status::NotFound)
    sd_synchronized.store(false, std::memory_order_release);
}

core_security::Status ReplicatedEncryptedStore::write_atomic(const char* path, size_t path_len,
                                                             const uint8_t* record, size_t size) {
  const auto result = primary_.write_atomic(path, path_len, record, size);
  if (result != core_security::Status::Ok)
    return result;
  if (replica_ && sd_synchronized.load(std::memory_order_acquire)) {
    const auto replica_result = replica_->write_atomic(path, path_len, record, size);
    recordReplicaStatus(replica_result);
  }
  return core_security::Status::Ok;
}

core_security::Status ReplicatedEncryptedStore::read(const char* path, size_t path_len,
                                                     uint8_t* record, size_t capacity,
                                                     size_t& size) {
  auto result = primary_.read(path, path_len, record, capacity, size);
  if (result == core_security::Status::Ok && replica_ &&
      sd_synchronized.load(std::memory_order_acquire)) {
    recordReplicaStatus(replica_->write_atomic(path, path_len, record, size));
    return result;
  }
  if (result != core_security::Status::NotFound || !replica_ ||
      !sd_synchronized.load(std::memory_order_acquire))
    return result;
  result = replica_->read(path, path_len, record, capacity, size);
  recordReplicaStatus(result);
  if (result != core_security::Status::Ok)
    return result;
  const auto import_result = primary_.write_atomic(path, path_len, record, size);
  return import_result == core_security::Status::Ok ? core_security::Status::Ok : import_result;
}

core_security::Status ReplicatedEncryptedStore::archive(const char* path, size_t path_len,
                                                        const char* archive_path,
                                                        size_t archive_path_len) {
  const auto result = primary_.archive(path, path_len, archive_path, archive_path_len);
  if (result != core_security::Status::Ok)
    return result;
  if (replica_ && sd_synchronized.load(std::memory_order_acquire))
    recordReplicaStatus(replica_->archive(path, path_len, archive_path, archive_path_len));
  return core_security::Status::Ok;
}

core_security::Status
ReplicatedEncryptedStore::archive_and_replace(const char* path, size_t path_len,
                                              const char* archive_path, size_t archive_path_len,
                                              const uint8_t* record, size_t size) {
  const auto result =
      primary_.archive_and_replace(path, path_len, archive_path, archive_path_len, record, size);
  if (result != core_security::Status::Ok)
    return result;
  if (replica_ && sd_synchronized.load(std::memory_order_acquire))
    recordReplicaStatus(replica_->archive_and_replace(path, path_len, archive_path,
                                                      archive_path_len, record, size));
  return core_security::Status::Ok;
}
} // namespace core_storage
