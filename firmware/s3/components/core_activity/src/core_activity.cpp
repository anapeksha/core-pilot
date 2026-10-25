#include "core_activity.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/platform_util.h"

#include <cstring>
#include <ctime>

namespace core_pilot {
namespace {

constexpr char kTag[] = "core_activity";
constexpr char kPath[] = "/core-pilot/diagnostics/activity-history.enc";
constexpr char kUnreadablePath[] = "/core-pilot/diagnostics/activity-history-unreadable.enc";
constexpr std::uint16_t kSchema = 1;
constexpr std::uint8_t kMagic[] = {'A', 'C', 'T', '1'};
constexpr std::size_t kHeaderBytes = sizeof(kMagic) + 4 + 4 + 2;
constexpr std::size_t kEncodedEventBytes =
    4 + 8 + 4 + 1 + ActivityService::kMaxKindBytes + 1 + ActivityService::kMaxDetailBytes;
constexpr std::size_t kMaxPayloadBytes =
    kHeaderBytes + ActivityService::kCapacity * kEncodedEventBytes;
constexpr std::size_t kEncryptedBytes = kMaxPayloadBytes + 64;

void put16(std::uint8_t* output, std::size_t& position, std::uint16_t value) {
  output[position++] = static_cast<std::uint8_t>(value);
  output[position++] = static_cast<std::uint8_t>(value >> 8);
}

void put32(std::uint8_t* output, std::size_t& position, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    output[position++] = static_cast<std::uint8_t>(value >> shift);
}

void put64(std::uint8_t* output, std::size_t& position, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8)
    output[position++] = static_cast<std::uint8_t>(value >> shift);
}

bool get16(const std::uint8_t* input, std::size_t length, std::size_t& position,
           std::uint16_t& value) {
  if (length - position < 2)
    return false;
  value = static_cast<std::uint16_t>(input[position]) |
          static_cast<std::uint16_t>(input[position + 1] << 8);
  position += 2;
  return true;
}

bool get32(const std::uint8_t* input, std::size_t length, std::size_t& position,
           std::uint32_t& value) {
  if (length - position < 4)
    return false;
  value = 0;
  for (unsigned shift = 0; shift < 32; shift += 8)
    value |= static_cast<std::uint32_t>(input[position++]) << shift;
  return true;
}

bool get64(const std::uint8_t* input, std::size_t length, std::size_t& position,
           std::uint64_t& value) {
  if (length - position < 8)
    return false;
  value = 0;
  for (unsigned shift = 0; shift < 64; shift += 8)
    value |= static_cast<std::uint64_t>(input[position++]) << shift;
  return true;
}

bool putString(const char* value, std::size_t maximum, std::uint8_t* output, std::size_t capacity,
               std::size_t& position) {
  const std::size_t length = strnlen(value, maximum + 1);
  if (length > maximum || position + 1 + length > capacity)
    return false;
  output[position++] = static_cast<std::uint8_t>(length);
  std::memcpy(output + position, value, length);
  position += length;
  return true;
}

bool getString(const std::uint8_t* input, std::size_t length, std::size_t maximum,
               std::size_t& position, char* output) {
  if (position == length)
    return false;
  const std::size_t size = input[position++];
  if (size > maximum || length - position < size)
    return false;
  std::memcpy(output, input + position, size);
  output[size] = '\0';
  position += size;
  return true;
}

bool copyBounded(const char* source, char* destination, std::size_t capacity) {
  if (source == nullptr || capacity == 0)
    return false;
  const std::size_t length = strnlen(source, capacity);
  if (length == 0 || length == capacity)
    return false;
  std::memcpy(destination, source, length + 1);
  return true;
}

} // namespace

ActivityService::ActivityService(core_security::StorageService& storage,
                                 core_security::RandomSource& random)
    : storage_(storage), random_(random) {}

ActivityService::~ActivityService() {
  if (task_ != nullptr)
    vTaskDelete(task_);
  if (plaintext_ != nullptr) {
    mbedtls_platform_zeroize(plaintext_, kMaxPayloadBytes);
    heap_caps_free(plaintext_);
  }
  if (encrypted_ != nullptr) {
    mbedtls_platform_zeroize(encrypted_, kEncryptedBytes);
    heap_caps_free(encrypted_);
  }
  if (events_ != nullptr) {
    mbedtls_platform_zeroize(events_, sizeof(Event) * kCapacity);
    heap_caps_free(events_);
  }
}

core_security::Status ActivityService::start() {
  if (task_ != nullptr)
    return core_security::Status::Ok;
  events_ = static_cast<Event*>(
      heap_caps_calloc(kCapacity, sizeof(Event), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  plaintext_ = static_cast<std::uint8_t*>(
      heap_caps_malloc(kMaxPayloadBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  encrypted_ = static_cast<std::uint8_t*>(
      heap_caps_malloc(kEncryptedBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (events_ == nullptr || plaintext_ == nullptr || encrypted_ == nullptr)
    return core_security::Status::IoError;

  mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
  queue_ = xQueueCreateStatic(kQueueDepth, sizeof(PendingEvent), queue_storage_, &queue_buffer_);
  if (mutex_ == nullptr || queue_ == nullptr)
    return core_security::Status::IoError;

  auto loaded = load();
  if (loaded != core_security::Status::Ok && loaded != core_security::Status::NotFound) {
    const auto archived =
        storage_.archive(kPath, sizeof(kPath) - 1, kUnreadablePath, sizeof(kUnreadablePath) - 1);
    if (archived != core_security::Status::Ok)
      return loaded;
    loaded = core_security::Status::NotFound;
  }

  task_ =
      xTaskCreateStaticPinnedToCore(taskEntry, "activity", kTaskStackBytes / sizeof(StackType_t),
                                    this, 3, task_stack_, &task_buffer_, 0);
  ESP_LOGI(kTag, "startup load=%d task=%s", static_cast<int>(loaded),
           task_ == nullptr ? "failed" : "ready");
  return task_ == nullptr ? core_security::Status::IoError : core_security::Status::Ok;
}

bool ActivityService::append(const char* kind, const char* detail) {
  if (queue_ == nullptr)
    return false;
  PendingEvent pending{};
  if (!copyBounded(kind, pending.kind, sizeof(pending.kind)) ||
      !copyBounded(detail, pending.detail, sizeof(pending.detail)))
    return false;
  if (xQueueSend(queue_, &pending, 0) == pdTRUE)
    return true;
  dropped_events_.fetch_add(1, std::memory_order_relaxed);
  return false;
}

bool ActivityService::page(bool has_before_sequence, std::uint32_t before_sequence,
                           std::uint8_t limit, Page& output) const {
  output = {};
  if (mutex_ == nullptr || events_ == nullptr || limit == 0 || limit > kMaxPageSize ||
      xSemaphoreTake(mutex_, pdMS_TO_TICKS(250)) != pdTRUE)
    return false;

  output.retained = count_;
  output.dropped_events = dropped_events_.load(std::memory_order_relaxed);
  std::size_t eligible = 0;
  for (std::size_t offset = 0; offset < count_; ++offset) {
    const std::size_t index = (head_ + kCapacity - 1 - offset) % kCapacity;
    const Event& event = events_[index];
    if (has_before_sequence && event.sequence >= before_sequence)
      continue;
    ++eligible;
    if (output.count < limit)
      output.events[output.count++] = event;
  }
  if (eligible > output.count && output.count != 0) {
    output.has_next = true;
    output.next_before_sequence = output.events[output.count - 1].sequence;
  }
  xSemaphoreGive(mutex_);
  return true;
}

void ActivityService::taskEntry(void* context) {
  static_cast<ActivityService*>(context)->taskLoop();
}

void ActivityService::taskLoop() {
  ESP_LOGI(kTag, "owner task started");
  PendingEvent pending{};
  bool dirty = false;
  TickType_t last_change = 0;
  while (true) {
    const TickType_t wait = dirty ? kFlushDelay : portMAX_DELAY;
    if (xQueueReceive(queue_, &pending, wait) == pdTRUE) {
      insert(pending);
      dirty = true;
      last_change = xTaskGetTickCount();
      continue;
    }
    if (dirty && xTaskGetTickCount() - last_change >= kFlushDelay) {
      if (persist() == core_security::Status::Ok)
        dirty = false;
    }
  }
}

void ActivityService::insert(const PendingEvent& pending) {
  Event event{};
  event.uptime_seconds = static_cast<std::uint32_t>(esp_timer_get_time() / 1000000);
  const std::time_t now = std::time(nullptr);
  event.unix_seconds = now >= 1577836800 ? static_cast<std::int64_t>(now) : 0;
  std::memcpy(event.kind, pending.kind, sizeof(event.kind));
  std::memcpy(event.detail, pending.detail, sizeof(event.detail));

  xSemaphoreTake(mutex_, portMAX_DELAY);
  event.sequence = next_sequence_++;
  if (next_sequence_ == 0)
    next_sequence_ = 1;
  events_[head_] = event;
  head_ = static_cast<std::uint16_t>((head_ + 1) % kCapacity);
  if (count_ < kCapacity)
    ++count_;
  else
    dropped_events_.fetch_add(1, std::memory_order_relaxed);
  xSemaphoreGive(mutex_);
}

core_security::Status ActivityService::load() {
  std::size_t size = 0;
  core_security::Metadata metadata{};
  const core_security::RecordSpec spec{kPath,
                                       sizeof(kPath) - 1,
                                       core_security::Namespace::Diagnostics,
                                       core_security::RecordType::Diagnostics,
                                       kSchema,
                                       0};
  const auto result =
      storage_.get(spec, encrypted_, kEncryptedBytes, plaintext_, kMaxPayloadBytes, size, metadata);
  if (result != core_security::Status::Ok)
    return result;
  if (size < kHeaderBytes || std::memcmp(plaintext_, kMagic, sizeof(kMagic)) != 0)
    return core_security::Status::Invalid;

  std::size_t position = sizeof(kMagic);
  std::uint32_t next = 0;
  std::uint32_t dropped = 0;
  std::uint16_t count = 0;
  if (!get32(plaintext_, size, position, next) || !get32(plaintext_, size, position, dropped) ||
      !get16(plaintext_, size, position, count) || next == 0 || count > kCapacity)
    return core_security::Status::Invalid;

  for (std::size_t index = 0; index < count; ++index) {
    Event event{};
    std::uint64_t unix_seconds = 0;
    if (!get32(plaintext_, size, position, event.sequence) ||
        !get64(plaintext_, size, position, unix_seconds) ||
        !get32(plaintext_, size, position, event.uptime_seconds) ||
        !getString(plaintext_, size, kMaxKindBytes, position, event.kind) ||
        !getString(plaintext_, size, kMaxDetailBytes, position, event.detail))
      return core_security::Status::Invalid;
    event.unix_seconds = static_cast<std::int64_t>(unix_seconds);
    events_[index] = event;
  }
  if (position != size)
    return core_security::Status::Invalid;
  count_ = count;
  head_ = count % kCapacity;
  next_sequence_ = next;
  generation_ = metadata.generation;
  dropped_events_.store(dropped, std::memory_order_relaxed);
  return core_security::Status::Ok;
}

core_security::Status ActivityService::persist() {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE)
    return core_security::Status::IoError;
  std::size_t position = 0;
  std::memcpy(plaintext_, kMagic, sizeof(kMagic));
  position += sizeof(kMagic);
  put32(plaintext_, position, next_sequence_);
  put32(plaintext_, position, dropped_events_.load(std::memory_order_relaxed));
  put16(plaintext_, position, count_);
  bool encoded = true;
  for (std::size_t offset = count_; encoded && offset > 0; --offset) {
    const Event& event = events_[(head_ + kCapacity - offset) % kCapacity];
    put32(plaintext_, position, event.sequence);
    put64(plaintext_, position, static_cast<std::uint64_t>(event.unix_seconds));
    put32(plaintext_, position, event.uptime_seconds);
    encoded = putString(event.kind, kMaxKindBytes, plaintext_, kMaxPayloadBytes, position) &&
              putString(event.detail, kMaxDetailBytes, plaintext_, kMaxPayloadBytes, position);
  }
  xSemaphoreGive(mutex_);
  if (!encoded)
    return core_security::Status::BufferTooSmall;

  const core_security::RecordSpec spec{kPath,
                                       sizeof(kPath) - 1,
                                       core_security::Namespace::Diagnostics,
                                       core_security::RecordType::Diagnostics,
                                       kSchema,
                                       generation_ + 1};
  const auto result =
      storage_.put(spec, plaintext_, position, random_, encrypted_, kEncryptedBytes);
  if (result == core_security::Status::Ok)
    ++generation_;
  return result;
}

} // namespace core_pilot
