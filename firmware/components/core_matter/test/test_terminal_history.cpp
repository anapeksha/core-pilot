#include "core_matter_terminal_history.hpp"

#include <cstdint>
#include <unity.h>

namespace {

struct Entry final {
  std::uint64_t sequence;
  std::uint32_t value;
};

} // namespace

TEST_CASE("terminal history retains the newest bounded entries", "[core_matter]") {
  core_pilot::detail::TerminalHistory<Entry, 8> history;
  for (std::uint32_t operation = 1; operation <= 10; ++operation) {
    TEST_ASSERT_TRUE(history.append(operation, {operation, operation * 10}));
  }

  Entry entry{};
  TEST_ASSERT_FALSE(history.find(1, entry));
  TEST_ASSERT_TRUE(history.find(3, entry));
  TEST_ASSERT_EQUAL_UINT64(3, entry.sequence);
  TEST_ASSERT_FALSE(history.append(3, {11, 30}));

  Entry entries[8]{};
  const std::size_t count = history.after(5, entries, 8);
  TEST_ASSERT_EQUAL_UINT32(5, count);
  for (std::size_t index = 0; index < count; ++index) {
    TEST_ASSERT_EQUAL_UINT64(index + 6, entries[index].sequence);
  }
}

TEST_CASE("terminal history respects output capacity", "[core_matter]") {
  core_pilot::detail::TerminalHistory<Entry, 8> history;
  TEST_ASSERT_TRUE(history.append(1, {1, 10}));
  TEST_ASSERT_TRUE(history.append(2, {2, 20}));
  TEST_ASSERT_TRUE(history.append(3, {3, 30}));

  Entry entries[2]{};
  TEST_ASSERT_EQUAL_UINT32(2, history.after(0, entries, 2));
  TEST_ASSERT_EQUAL_UINT64(1, entries[0].sequence);
  TEST_ASSERT_EQUAL_UINT64(2, entries[1].sequence);
  TEST_ASSERT_EQUAL_UINT32(0, history.after(0, nullptr, 2));
}
