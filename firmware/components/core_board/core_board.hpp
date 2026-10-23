#pragma once

#include "esp_err.h"

#include <cstddef>
#include <cstdint>

namespace core_pilot::board {

// Serializes complete LCD and FAT operations on the CoreS3 shared SPI2 bus.
// Guards are recursive so a high-level operation may call guarded helpers.
class SharedSpiGuard final {
public:
  enum class Use { display, storage };

  explicit SharedSpiGuard(Use use);
  ~SharedSpiGuard();
  SharedSpiGuard(const SharedSpiGuard&) = delete;
  SharedSpiGuard& operator=(const SharedSpiGuard&) = delete;
  bool locked() const {
    return locked_;
  }

private:
  bool locked_;
};

// Adopts the SPI2 bus initialized by M5Unified.
esp_err_t initialize_shared_spi();

// Drives the CoreS3 panel through ESP-IDF while M5Canvas remains the renderer.
esp_err_t initialize_lcd();
esp_err_t draw_lcd_region(int x, int y, int width, int height, const void* pixels,
                          std::size_t pixel_bytes);

esp_err_t mount_sd();
// Destructively reformats only the mounted TF volume. It never touches flash.
esp_err_t format_sd();
bool sd_mounted();

} // namespace core_pilot::board
