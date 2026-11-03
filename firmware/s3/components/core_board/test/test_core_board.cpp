#include "core_board_policy.hpp"

#include <climits>
#include <unity.h>
#include <unity_test_runner.h>

using core_pilot::board::LcdRegion;
using core_pilot::board::valid_lcd_region;

TEST_CASE("LCD regions stay inside the panel and require exact RGB565 bytes", "[core_board]") {
  TEST_ASSERT_TRUE(valid_lcd_region({0, 0, 320, 240, 320U * 240U * 2U}));
  TEST_ASSERT_TRUE(valid_lcd_region({319, 239, 1, 1, 2}));
  TEST_ASSERT_FALSE(valid_lcd_region({0, 0, 1, 1, 1}));
  TEST_ASSERT_FALSE(valid_lcd_region({0, 0, 1, 1, 3}));
  TEST_ASSERT_FALSE(valid_lcd_region({319, 239, 2, 1, 4}));
  TEST_ASSERT_FALSE(valid_lcd_region({0, 239, 1, 2, 4}));
}

TEST_CASE("LCD validation rejects non-positive and overflow-shaped geometry", "[core_board]") {
  TEST_ASSERT_FALSE(valid_lcd_region({-1, 0, 1, 1, 2}));
  TEST_ASSERT_FALSE(valid_lcd_region({0, -1, 1, 1, 2}));
  TEST_ASSERT_FALSE(valid_lcd_region({0, 0, 0, 1, 0}));
  TEST_ASSERT_FALSE(valid_lcd_region({0, 0, 1, -1, 0}));
  TEST_ASSERT_FALSE(valid_lcd_region({1, 1, INT_MAX, 1, 2}));
  TEST_ASSERT_FALSE(valid_lcd_region({1, 1, 1, INT_MAX, 2}));
}
