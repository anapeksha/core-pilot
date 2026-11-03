#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace core_pilot::board {

struct LcdRegion final {
  int x;
  int y;
  int width;
  int height;
  std::size_t pixel_bytes;
};

[[nodiscard]] constexpr bool valid_lcd_region(const LcdRegion& region) {
  constexpr int display_width = 320;
  constexpr int display_height = 240;
  constexpr std::size_t bytes_per_pixel = sizeof(std::uint16_t);

  if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
      region.x >= display_width || region.y >= display_height ||
      region.width > display_width - region.x || region.height > display_height - region.y)
    return false;

  const auto width = static_cast<std::size_t>(region.width);
  const auto height = static_cast<std::size_t>(region.height);
  if (width > std::numeric_limits<std::size_t>::max() / height / bytes_per_pixel)
    return false;
  return region.pixel_bytes == width * height * bytes_per_pixel;
}

} // namespace core_pilot::board
