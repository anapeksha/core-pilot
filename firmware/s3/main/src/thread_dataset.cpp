#include "thread_dataset.hpp"

#include "core_matter.hpp"
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

ThreadDatasetService::ThreadDatasetService(core_security::StorageService& storage,
                                           core_security::RandomSource& random, CoreMatter& matter)
    : storage_(storage), random_(random), matter_(matter) {}

esp_err_t ThreadDatasetService::start() {
  if (queue_ != nullptr)
    return ESP_ERR_INVALID_STATE;
  queue_ = xQueueCreateStatic(1, sizeof(Request), queue_buffer_, &queue_storage_);
  return queue_ == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t ThreadDatasetService::submit(const ThreadDataset& dataset, std::uint32_t& operation_id) {
  operation_id = 0;
  if (queue_ == nullptr || !validateThreadDataset(dataset.tlvs, dataset.length))
    return ESP_ERR_INVALID_ARG;

  Request request{};
  request.length = dataset.length;
  std::memcpy(request.tlvs, dataset.tlvs, dataset.length);

  portENTER_CRITICAL(&lock_);
  if (status_.state == State::queued || status_.state == State::persisting ||
      status_.state == State::submitting) {
    portEXIT_CRITICAL(&lock_);
    mbedtls_platform_zeroize(&request, sizeof(request));
    return ESP_ERR_INVALID_STATE;
  }
  request.operation_id = next_operation_id_++;
  if (next_operation_id_ == 0)
    next_operation_id_ = 1;
  status_ = {State::queued, request.operation_id, ESP_OK};
  portEXIT_CRITICAL(&lock_);

  if (xQueueSend(queue_, &request, 0) != pdTRUE) {
    publish(State::failed, request.operation_id, ESP_ERR_TIMEOUT);
    mbedtls_platform_zeroize(&request, sizeof(request));
    return ESP_ERR_TIMEOUT;
  }
  operation_id = request.operation_id;
  mbedtls_platform_zeroize(&request, sizeof(request));
  return ESP_OK;
}

void ThreadDatasetService::reconcile() {
  if (queue_ == nullptr)
    return;
  Request request{};
  if (xQueueReceive(queue_, &request, 0) != pdTRUE)
    return;
  mbedtls_platform_zeroize(queue_buffer_, sizeof(queue_buffer_));

  publish(State::persisting, request.operation_id, ESP_OK);
  ThreadDataset dataset{};
  dataset.length = request.length;
  std::memcpy(dataset.tlvs, request.tlvs, request.length);
  ThreadDataset current{};
  const auto loaded = loadThreadDataset(storage_, current);
  if (loaded == core_security::Status::Ok)
    dataset.generation = current.generation;
  current.clear();

  if (loaded != core_security::Status::Ok && loaded != core_security::Status::NotFound &&
      loaded != core_security::Status::Invalid) {
    publish(State::failed, request.operation_id, ESP_ERR_INVALID_STATE);
  } else if (saveThreadDataset(storage_, random_, dataset) != core_security::Status::Ok) {
    publish(State::failed, request.operation_id, ESP_FAIL);
  } else {
    publish(State::submitting, request.operation_id, ESP_OK);
    std::uint32_t matter_operation_id = 0;
    const esp_err_t result =
        matter_.applyThreadDataset(dataset.tlvs, dataset.length, matter_operation_id);
    if (result == ESP_OK) {
      portENTER_CRITICAL(&lock_);
      matter_operation_id_ = matter_operation_id;
      portEXIT_CRITICAL(&lock_);
    } else {
      publish(State::failed, request.operation_id, result);
    }
  }
  dataset.clear();
  mbedtls_platform_zeroize(&request, sizeof(request));
}

void ThreadDatasetService::reconcileMatterTerminal(std::uint32_t matter_operation_id,
                                                   bool succeeded, esp_err_t error) {
  portENTER_CRITICAL(&lock_);
  if (status_.state == State::submitting && matter_operation_id != 0 &&
      matter_operation_id == matter_operation_id_) {
    status_.state = succeeded ? State::complete : State::failed;
    status_.last_error = succeeded ? ESP_OK : error;
    matter_operation_id_ = 0;
  }
  portEXIT_CRITICAL(&lock_);
}

ThreadDatasetService::Status ThreadDatasetService::status() const {
  portENTER_CRITICAL(&lock_);
  const Status snapshot = status_;
  portEXIT_CRITICAL(&lock_);
  return snapshot;
}

void ThreadDatasetService::publish(State state, std::uint32_t operation_id, esp_err_t error) {
  portENTER_CRITICAL(&lock_);
  status_ = {state, operation_id, error};
  portEXIT_CRITICAL(&lock_);
}

const char* threadDatasetOperationState(ThreadDatasetService::State state) {
  switch (state) {
  case ThreadDatasetService::State::idle:
    return "idle";
  case ThreadDatasetService::State::queued:
    return "queued";
  case ThreadDatasetService::State::persisting:
    return "persisting";
  case ThreadDatasetService::State::submitting:
    return "submitting";
  case ThreadDatasetService::State::complete:
    return "complete";
  case ThreadDatasetService::State::failed:
    return "failed";
  }
  return "failed";
}

} // namespace core_pilot
