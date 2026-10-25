#include "core_gateway.hpp"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp32_port.h"
#include "esp_check.h"
#include "esp_loader.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gateway_rcp_payload.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace core_pilot {
namespace {

constexpr char kTag[] = "core_gateway";
constexpr uart_port_t kUart = UART_NUM_1;
constexpr gpio_num_t kRxPin = GPIO_NUM_10;
constexpr gpio_num_t kTxPin = GPIO_NUM_17;
constexpr gpio_num_t kEnablePin = GPIO_NUM_7;
constexpr gpio_num_t kBootPin = GPIO_NUM_18;
constexpr std::size_t kFlashBlockSize = 1024;
constexpr std::uint8_t kMaxFlashAttempts = 2;

struct Segment final {
  const char* name;
  std::uint32_t address;
  const std::uint8_t* data;
  std::size_t size;
  const std::uint8_t* md5_hex;
};

const Segment kSegments[] = {
    {"bootloader", 0x0000, gateway::payload::kBootloader, gateway::payload::kBootloaderSize,
     gateway::payload::kBootloaderMd5Hex},
    {"partition table", 0x8000, gateway::payload::kPartitionTable,
     gateway::payload::kPartitionTableSize, gateway::payload::kPartitionTableMd5Hex},
    {"application", 0x10000, gateway::payload::kApplication, gateway::payload::kApplicationSize,
     gateway::payload::kApplicationMd5Hex},
};

esp_err_t loaderError(esp_loader_error_t error) {
  switch (error) {
  case ESP_LOADER_SUCCESS:
    return ESP_OK;
  case ESP_LOADER_ERROR_TIMEOUT:
    return ESP_ERR_TIMEOUT;
  case ESP_LOADER_ERROR_INVALID_PARAM:
  case ESP_LOADER_ERROR_IMAGE_SIZE:
    return ESP_ERR_INVALID_ARG;
  case ESP_LOADER_ERROR_INVALID_TARGET:
  case ESP_LOADER_ERROR_UNSUPPORTED_CHIP:
    return ESP_ERR_NOT_SUPPORTED;
  case ESP_LOADER_ERROR_INVALID_MD5:
  case ESP_LOADER_ERROR_FAIL:
  case ESP_LOADER_ERROR_UNSUPPORTED_FUNC:
  case ESP_LOADER_ERROR_INVALID_RESPONSE:
    return ESP_FAIL;
  }
  return ESP_FAIL;
}

esp_err_t flashSegment(const Segment& segment) {
  ESP_LOGI(kTag, "flashing approved H2 %s segment (%u bytes at 0x%05lx)", segment.name,
           static_cast<unsigned>(segment.size), static_cast<unsigned long>(segment.address));
  auto error = esp_loader_flash_start(segment.address, static_cast<std::uint32_t>(segment.size),
                                      kFlashBlockSize);
  if (error != ESP_LOADER_SUCCESS)
    return loaderError(error);

  std::array<std::uint8_t, kFlashBlockSize> block{};
  std::size_t offset = 0;
  while (offset < segment.size) {
    block.fill(0xff);
    const std::size_t count = std::min(block.size(), segment.size - offset);
    std::memcpy(block.data(), segment.data + offset, count);
    error = esp_loader_flash_write(block.data(), static_cast<std::uint32_t>(count));
    if (error != ESP_LOADER_SUCCESS)
      return loaderError(error);
    offset += count;
  }
  return loaderError(esp_loader_flash_verify());
}

} // namespace

esp_err_t CoreGateway::provision() {
  const loader_esp32_config_t config{
      .baud_rate = 115200,
      .uart_port = kUart,
      .uart_rx_pin = kRxPin,
      .uart_tx_pin = kTxPin,
      .reset_trigger_pin = kEnablePin,
      .gpio0_trigger_pin = kBootPin,
      .rx_buffer_size = 512,
      .tx_buffer_size = 512,
      .queue_size = 0,
      .uart_queue = nullptr,
      .dont_initialize_peripheral = false,
  };
  auto error = loader_port_esp32_init(&config);
  if (error != ESP_LOADER_SUCCESS)
    return loaderError(error);
  loader_port_enter_bootloader();
  esp_loader_connect_args_t connect_args{.sync_timeout = 100, .trials = 5};
  error = esp_loader_connect(&connect_args);
  if (error == ESP_LOADER_SUCCESS && esp_loader_get_target() != ESP32H2_CHIP)
    error = ESP_LOADER_ERROR_INVALID_TARGET;
  esp_err_t result = loaderError(error);
  if (result == ESP_OK) {
    for (const auto& segment : kSegments) {
      error = esp_loader_flash_verify_known_md5(
          segment.address, static_cast<std::uint32_t>(segment.size), segment.md5_hex);
      if (error == ESP_LOADER_SUCCESS) {
        ESP_LOGI(kTag, "H2 %s segment is current", segment.name);
        continue;
      }
      if (error != ESP_LOADER_ERROR_INVALID_MD5) {
        result = loaderError(error);
        break;
      }
      result = flashSegment(segment);
      if (result != ESP_OK)
        break;
    }
  }
  loader_port_esp32_deinit();
  if (result != ESP_OK)
    return result;

  const gpio_config_t pins{
      .pin_bit_mask = (1ULL << kEnablePin) | (1ULL << kBootPin),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&pins), kTag, "configure H2 control pins");
  ESP_RETURN_ON_ERROR(gpio_set_level(kBootPin, 1), kTag, "release H2 boot pin");
  ESP_RETURN_ON_ERROR(gpio_set_level(kEnablePin, 0), kTag, "assert H2 reset");
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_RETURN_ON_ERROR(gpio_set_level(kEnablePin, 1), kTag, "release H2 reset");
  vTaskDelay(pdMS_TO_TICKS(100));
  return ESP_OK;
}

esp_err_t CoreGateway::ensureReady() {
  status_ = {State::provisioning, false, 0, ESP_OK};
  esp_err_t result = ESP_FAIL;
  for (std::uint8_t attempt = 1; attempt <= kMaxFlashAttempts; ++attempt) {
    status_.flash_attempts = attempt;
    result = provision();
    if (result == ESP_OK) {
      status_ = {State::ready, true, attempt, ESP_OK};
      return ESP_OK;
    }
    ESP_LOGE(kTag, "H2 RCP provisioning attempt %u failed: %s", attempt, esp_err_to_name(result));
  }
  status_.state = State::failed;
  status_.last_error = result;
  return result;
}

CoreGateway::Status CoreGateway::status() const {
  return status_;
}

} // namespace core_pilot
