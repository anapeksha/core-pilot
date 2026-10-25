#include "core_board.hpp"

#include "M5Unified.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "rom/gpio.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"

#include <algorithm>
#include <cstring>

namespace core_pilot::board {
namespace {
constexpr gpio_num_t kLcdCs = GPIO_NUM_3;
constexpr gpio_num_t kTfCs = GPIO_NUM_4;
constexpr gpio_num_t kSharedDcMiso = GPIO_NUM_35;
constexpr char kMountPoint[] = "/sdcard";
constexpr char kTag[] = "core_board";

StaticSemaphore_t mutex_storage{};
SemaphoreHandle_t mutex = nullptr;
StaticSemaphore_t lcd_done_storage{};
SemaphoreHandle_t lcd_done = nullptr;
esp_lcd_panel_io_handle_t lcd_io = nullptr;
constexpr std::size_t kLcdDmaBufferBytes = 320 * sizeof(std::uint16_t);
constexpr std::size_t kSdDmaBufferBytes = 4096;
sdmmc_card_t* card = nullptr;
void* lcd_dma_buffer = nullptr;
void* sd_dma_buffer = nullptr;

bool bus_initialized = false;
bool lcd_initialized = false;
bool mount_attempted = false;
esp_err_t mount_result = ESP_ERR_INVALID_STATE;
bool mounted = false;

bool ensure_mutex() {
  if (mutex != nullptr)
    return true;
  mutex = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage);
  return mutex != nullptr;
}

bool lcd_transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  BaseType_t task_woken = pdFALSE;
  xSemaphoreGiveFromISR(lcd_done, &task_woken);
  return task_woken == pdTRUE;
}

void select_lcd() {
  gpio_set_pull_mode(kSharedDcMiso, GPIO_FLOATING);
  gpio_set_direction(kSharedDcMiso, GPIO_MODE_OUTPUT);
  esp_rom_gpio_connect_out_signal(kSharedDcMiso, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_direction(kLcdCs, GPIO_MODE_OUTPUT);
  gpio_set_level(kLcdCs, 0);
}

esp_err_t send_lcd_command(std::uint8_t command, const void* parameters = nullptr,
                           std::size_t parameter_bytes = 0) {
  return esp_lcd_panel_io_tx_param(lcd_io, command, parameters, parameter_bytes);
}

// This is the only application-owned CoreS3 GPIO-matrix handoff.
void restore_sd_safe_idle() {
  gpio_set_direction(kLcdCs, GPIO_MODE_OUTPUT);
  gpio_set_level(kLcdCs, 1);
  // Before SDSPI registration GPIO4 is an ordinary output. Afterwards it is
  // owned by the SPI-master device and must not be reconfigured as a GPIO.
  if (!mounted) {
    gpio_set_direction(kTfCs, GPIO_MODE_OUTPUT);
    gpio_set_level(kTfCs, 1);
  }
  // Match M5GFX's CoreS3 idle state: keep the SPI2 Q matrix route selected,
  // disable output, and use GPIO35 strictly as the card's MISO input.
  esp_rom_gpio_connect_out_signal(kSharedDcMiso, FSPIQ_OUT_IDX, false, false);
  gpio_set_direction(kSharedDcMiso, GPIO_MODE_INPUT);
  gpio_set_pull_mode(kSharedDcMiso, GPIO_PULLUP_ONLY);
  esp_rom_gpio_connect_in_signal(kSharedDcMiso, FSPIQ_IN_IDX, false);
}

esp_err_t shared_sdspi_transaction(int slot, sdmmc_command_t* command) {
  SharedSpiGuard guard(SharedSpiGuard::Use::storage);
  if (!guard.locked())
    return ESP_ERR_TIMEOUT;
  return sdspi_host_do_transaction(slot, command);
}

} // namespace

SharedSpiGuard::SharedSpiGuard(Use) : locked_(false) {
  if (!ensure_mutex())
    return;
  locked_ = xSemaphoreTakeRecursive(mutex, portMAX_DELAY) == pdTRUE;
  if (locked_)
    restore_sd_safe_idle();
}

SharedSpiGuard::~SharedSpiGuard() {
  if (!locked_)
    return;
  // LCD DMA is complete before a display guard can leave its scope.
  restore_sd_safe_idle();
  xSemaphoreGiveRecursive(mutex);
}

esp_err_t initialize_shared_spi() {
  if (bus_initialized)
    return ESP_OK;
  if (!ensure_mutex())
    return ESP_ERR_NO_MEM;
  spi_bus_config_t config{};
  config.mosi_io_num = GPIO_NUM_37;
  config.miso_io_num = GPIO_NUM_35;
  config.sclk_io_num = GPIO_NUM_36;
  config.quadwp_io_num = -1;
  config.quadhd_io_num = -1;
  config.max_transfer_sz = 320 * 240 * 2;
  const esp_err_t result = spi_bus_initialize(SPI2_HOST, &config, SPI_DMA_CH_AUTO);
  if (result == ESP_OK || result == ESP_ERR_INVALID_STATE) {
    bus_initialized = true;
    return ESP_OK;
  }
  return result;
}

esp_err_t initialize_lcd() {
  if (lcd_initialized)
    return ESP_OK;
  if (!bus_initialized)
    return ESP_ERR_INVALID_STATE;

  lcd_done = xSemaphoreCreateBinaryStatic(&lcd_done_storage);
  if (lcd_done == nullptr)
    return ESP_ERR_NO_MEM;
  lcd_dma_buffer = heap_caps_malloc(kLcdDmaBufferBytes, MALLOC_CAP_DMA);
  if (lcd_dma_buffer == nullptr)
    return ESP_ERR_NO_MEM;

  esp_lcd_panel_io_spi_config_t config{};
  config.cs_gpio_num = GPIO_NUM_NC;
  config.dc_gpio_num = kSharedDcMiso;
  config.spi_mode = 0;
  config.pclk_hz = 40 * 1000 * 1000;
  config.trans_queue_depth = 4;
  config.on_color_trans_done = lcd_transfer_done;
  config.lcd_cmd_bits = 8;
  config.lcd_param_bits = 8;
  esp_err_t result = esp_lcd_new_panel_io_spi(SPI2_HOST, &config, &lcd_io);
  if (result != ESP_OK) {
    heap_caps_free(lcd_dma_buffer);
    lcd_dma_buffer = nullptr;
    return result;
  }

  SharedSpiGuard guard(SharedSpiGuard::Use::display);
  if (!guard.locked())
    return ESP_ERR_TIMEOUT;
  select_lcd();

  result = send_lcd_command(0x01);
  if (result == ESP_OK)
    vTaskDelay(pdMS_TO_TICKS(150));

  const std::uint8_t c8[] = {0xff, 0x93, 0x42};
  const std::uint8_t c0[] = {0x12, 0x12};
  const std::uint8_t c1[] = {0x03};
  const std::uint8_t c5[] = {0xf2};
  const std::uint8_t b0[] = {0xe0};
  const std::uint8_t f6[] = {0x01, 0x00, 0x00};
  const std::uint8_t e0[] = {0x00, 0x0c, 0x11, 0x04, 0x11, 0x08, 0x37, 0x89,
                             0x4c, 0x06, 0x0c, 0x0a, 0x2e, 0x34, 0x0f};
  const std::uint8_t e1[] = {0x00, 0x0b, 0x11, 0x05, 0x13, 0x09, 0x33, 0x67,
                             0x48, 0x07, 0x0e, 0x0b, 0x2e, 0x33, 0x0f};
  const std::uint8_t b6[] = {0x08, 0x82, 0x1d, 0x04};
  const std::uint8_t pixel_format[] = {0x55};
  const std::uint8_t madctl[] = {0x08};
  const struct {
    std::uint8_t command;
    const void* data;
    std::size_t size;
  } commands[] = {{0xc8, c8, sizeof(c8)}, {0xc0, c0, sizeof(c0)}, {0xc1, c1, sizeof(c1)},
                  {0xc5, c5, sizeof(c5)}, {0xb0, b0, sizeof(b0)}, {0xf6, f6, sizeof(f6)},
                  {0xe0, e0, sizeof(e0)}, {0xe1, e1, sizeof(e1)}, {0xb6, b6, sizeof(b6)}};
  for (const auto& command : commands) {
    if (result == ESP_OK)
      result = send_lcd_command(command.command, command.data, command.size);
  }
  if (result == ESP_OK)
    result = send_lcd_command(0x11);
  if (result == ESP_OK)
    vTaskDelay(pdMS_TO_TICKS(120));
  if (result == ESP_OK)
    result = send_lcd_command(0x3a, pixel_format, sizeof(pixel_format));
  if (result == ESP_OK)
    result = send_lcd_command(0x36, madctl, sizeof(madctl));
  if (result == ESP_OK)
    result = send_lcd_command(0x21);
  if (result == ESP_OK)
    result = send_lcd_command(0x38);
  if (result == ESP_OK)
    result = send_lcd_command(0x29);
  if (result == ESP_OK)
    vTaskDelay(pdMS_TO_TICKS(20));

  gpio_set_level(kLcdCs, 1);
  lcd_initialized = result == ESP_OK;
  if (!lcd_initialized) {
    esp_lcd_panel_io_del(lcd_io);
    lcd_io = nullptr;
    heap_caps_free(lcd_dma_buffer);
    lcd_dma_buffer = nullptr;
  } else {
    ESP_LOGI(kTag, "ESP-LCD backend initialized at 40 MHz");
  }
  return result;
}

esp_err_t draw_lcd_region(int x, int y, int width, int height, const void* pixels,
                          std::size_t pixel_bytes) {
  if (!lcd_initialized || pixels == nullptr || width <= 0 || height <= 0 || x < 0 || y < 0 ||
      x + width > 320 || y + height > 240 ||
      pixel_bytes != static_cast<std::size_t>(width) * height * 2)
    return ESP_ERR_INVALID_ARG;

  SharedSpiGuard guard(SharedSpiGuard::Use::display);
  if (!guard.locked())
    return ESP_ERR_TIMEOUT;
  select_lcd();

  const std::uint16_t x_end = static_cast<std::uint16_t>(x + width - 1);
  const std::uint8_t columns[] = {static_cast<std::uint8_t>(x >> 8), static_cast<std::uint8_t>(x),
                                  static_cast<std::uint8_t>(x_end >> 8),
                                  static_cast<std::uint8_t>(x_end)};
  const std::size_t row_bytes = static_cast<std::size_t>(width) * 2;
  const int rows_per_transfer = std::max(1, static_cast<int>(kLcdDmaBufferBytes / row_bytes));
  const auto* source = static_cast<const std::uint8_t*>(pixels);
  esp_err_t result = ESP_OK;

  for (int row = 0; result == ESP_OK && row < height; row += rows_per_transfer) {
    const int rows_in_transfer = std::min(rows_per_transfer, height - row);
    const std::uint16_t y_start = static_cast<std::uint16_t>(y + row);
    const std::uint16_t y_end = static_cast<std::uint16_t>(y_start + rows_in_transfer - 1);
    const std::uint8_t rows[] = {
        static_cast<std::uint8_t>(y_start >> 8), static_cast<std::uint8_t>(y_start),
        static_cast<std::uint8_t>(y_end >> 8), static_cast<std::uint8_t>(y_end)};
    const std::size_t chunk_bytes = static_cast<std::size_t>(rows_in_transfer) * row_bytes;
    std::memcpy(lcd_dma_buffer, source + static_cast<std::size_t>(row) * row_bytes, chunk_bytes);

    result = send_lcd_command(0x2a, columns, sizeof(columns));
    if (result == ESP_OK)
      result = send_lcd_command(0x2b, rows, sizeof(rows));
    if (result == ESP_OK) {
      xSemaphoreTake(lcd_done, 0);
      result = esp_lcd_panel_io_tx_color(lcd_io, 0x2c, lcd_dma_buffer, chunk_bytes);
    }
    if (result == ESP_OK)
      xSemaphoreTake(lcd_done, portMAX_DELAY);
  }
  gpio_set_level(kLcdCs, 1);
  return result;
}

esp_err_t mount_sd() {
  if (mount_attempted)
    return mount_result;
  mount_attempted = true;
  if (!bus_initialized) {
    mount_result = ESP_ERR_INVALID_STATE;
    return mount_result;
  }
  SharedSpiGuard guard(SharedSpiGuard::Use::storage);
  if (!guard.locked())
    return ESP_ERR_NO_MEM;

  // The TF controller can remain powered and stale across an S3 reset. Cycle
  // its dedicated AXP2101 ALDO4 rail before the first protocol command.
  M5.Power.Axp2101.setALDO4(0);
  vTaskDelay(pdMS_TO_TICKS(500));
  M5.Power.Axp2101.setALDO4(3300);
  vTaskDelay(pdMS_TO_TICKS(750));
  restore_sd_safe_idle();

  sd_dma_buffer = heap_caps_malloc(kSdDmaBufferBytes, MALLOC_CAP_DMA);
  if (sd_dma_buffer == nullptr) {
    mount_result = ESP_ERR_NO_MEM;
    return mount_result;
  }
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;
  // GPIO35 changes between LCD D/C output and card MISO. A conservative card
  // clock provides margin across that handoff and varied microSD signal quality.
  host.max_freq_khz = 1000;
  host.do_transaction = shared_sdspi_transaction;
  host.dma_aligned_buffer = sd_dma_buffer;
  host.unaligned_multi_block_rw_max_chunk_size = kSdDmaBufferBytes / 512;
  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.host_id = SPI2_HOST;
  slot.gpio_cs = kTfCs;
  slot.gpio_cd = SDSPI_SLOT_NO_CD;
  slot.gpio_wp = SDSPI_SLOT_NO_WP;
  slot.gpio_int = SDSPI_SLOT_NO_INT;

  esp_vfs_fat_sdmmc_mount_config_t config{};
  config.format_if_mount_failed = false;
  // Storage mutations may overlap with Matter credential and application record
  // activity. Keep this bounded, but leave enough descriptors for atomic replace.
  config.max_files = 12;
  config.allocation_unit_size = 16 * 1024;
  config.disk_status_check_enable = false;
  config.use_one_fat = false;

  ESP_LOGI(kTag, "mounting CoreS3 TF card on shared SPI2");
  mount_result = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot, &config, &card);
  mounted = mount_result == ESP_OK;
  if (mounted) {
    ESP_LOGI(kTag, "CoreS3 TF card mounted");
  } else {
    heap_caps_free(sd_dma_buffer);
    sd_dma_buffer = nullptr;
    ESP_LOGE(kTag, "CoreS3 TF card mount failed: %s", esp_err_to_name(mount_result));
  }
  return mount_result;
}

esp_err_t format_sd() {
  if (!mounted || card == nullptr)
    return ESP_ERR_INVALID_STATE;
  SharedSpiGuard guard(SharedSpiGuard::Use::storage);
  if (!guard.locked())
    return ESP_ERR_TIMEOUT;

  esp_vfs_fat_sdcard_unmount(kMountPoint, card);
  card = nullptr;
  mounted = false;
  M5.Power.Axp2101.setALDO4(0);
  vTaskDelay(pdMS_TO_TICKS(150));
  M5.Power.Axp2101.setALDO4(3300);
  vTaskDelay(pdMS_TO_TICKS(500));
  restore_sd_safe_idle();

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;
  host.max_freq_khz = 1000;
  host.do_transaction = shared_sdspi_transaction;
  host.dma_aligned_buffer = sd_dma_buffer;
  host.unaligned_multi_block_rw_max_chunk_size = kSdDmaBufferBytes / 512;
  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.host_id = SPI2_HOST;
  slot.gpio_cs = kTfCs;
  slot.gpio_cd = SDSPI_SLOT_NO_CD;
  slot.gpio_wp = SDSPI_SLOT_NO_WP;
  slot.gpio_int = SDSPI_SLOT_NO_INT;
  esp_vfs_fat_sdmmc_mount_config_t config{};
  config.format_if_mount_failed = false;
  config.max_files = 12;
  config.allocation_unit_size = 16 * 1024;
  config.disk_status_check_enable = false;
  config.use_one_fat = true;

  mount_result = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot, &config, &card);
  mounted = mount_result == ESP_OK;
  if (!mounted)
    return mount_result;
  return esp_vfs_fat_sdcard_format(kMountPoint, card);
}

bool sd_mounted() {
  return mounted;
}

} // namespace core_pilot::board
