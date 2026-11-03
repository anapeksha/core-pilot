#include "core_activity.hpp"

#include <cstring>
#include <limits>
#include <unity.h>
#include <unity_test_runner.h>

using namespace core_pilot::activity_policy;

TEST_CASE("activity event text enforces nonempty byte bounds", "[core_activity]") {
  char output[5]{};
  TEST_ASSERT_TRUE(copy_event_text("four", output, sizeof output));
  TEST_ASSERT_EQUAL_STRING("four", output);
  TEST_ASSERT_FALSE(copy_event_text("", output, sizeof output));
  TEST_ASSERT_FALSE(copy_event_text("five5", output, sizeof output));
  TEST_ASSERT_FALSE(copy_event_text(nullptr, output, sizeof output));
  TEST_ASSERT_FALSE(copy_event_text("x", nullptr, sizeof output));
  TEST_ASSERT_FALSE(copy_event_text("x", output, 0));
}

TEST_CASE("activity page policy uses an exclusive cursor and bounded limits", "[core_activity]") {
  TEST_ASSERT_FALSE(valid_page_limit(0, 32));
  TEST_ASSERT_TRUE(valid_page_limit(1, 32));
  TEST_ASSERT_TRUE(valid_page_limit(32, 32));
  TEST_ASSERT_FALSE(valid_page_limit(33, 32));

  TEST_ASSERT_TRUE(before_cursor(42, false, 0));
  TEST_ASSERT_TRUE(before_cursor(41, true, 42));
  TEST_ASSERT_FALSE(before_cursor(42, true, 42));
  TEST_ASSERT_FALSE(before_cursor(43, true, 42));
}

TEST_CASE("activity sequence progression never emits zero after wrap", "[core_activity]") {
  TEST_ASSERT_EQUAL_UINT32(2, following_sequence(1));
  TEST_ASSERT_EQUAL_UINT32(1, following_sequence(std::numeric_limits<std::uint32_t>::max()));
}
