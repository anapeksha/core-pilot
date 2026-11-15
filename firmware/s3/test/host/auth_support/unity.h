#pragma once

// Minimal host adapter for the shared Unity auth tests. Never print operands.
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace auth_test {
struct Case {
  const char* name;
  const char* file;
  void (*run)();
};
inline Case cases[256]{};
inline unsigned count{};
struct Register {
  Register(const char* name, const char* file, void (*run)()) {
    if (count == 256)
      std::abort();
    cases[count++] = {name, file, run};
  }
};
inline void check(bool condition, int line) {
  if (!condition) {
    std::fprintf(stderr, "auth assertion failed at line %d\n", line);
    std::abort();
  }
}
} // namespace auth_test

#define AUTH_JOIN_(a, b) a##b
#define AUTH_JOIN(a, b) AUTH_JOIN_(a, b)
#define TEST_CASE(name, tags)                                                                      \
  static void AUTH_JOIN(auth_case_, __LINE__)();                                                   \
  static auth_test::Register AUTH_JOIN(auth_register_, __LINE__)(name, __FILE__,                   \
                                                                 AUTH_JOIN(auth_case_, __LINE__)); \
  static void AUTH_JOIN(auth_case_, __LINE__)()
#define TEST_ASSERT_EQUAL(expected, actual)                                                        \
  auth_test::check(static_cast<long long>(expected) == static_cast<long long>(actual), __LINE__)
#define TEST_ASSERT_TRUE(value) auth_test::check(!!(value), __LINE__)
#define TEST_ASSERT_FALSE(value) auth_test::check(!(value), __LINE__)

#define TEST_ASSERT_EQUAL_UINT TEST_ASSERT_EQUAL
#define TEST_ASSERT_EQUAL_UINT8 TEST_ASSERT_EQUAL
#define TEST_ASSERT_EQUAL_UINT32 TEST_ASSERT_EQUAL
#define TEST_ASSERT_EQUAL_UINT64 TEST_ASSERT_EQUAL
#define TEST_ASSERT_EQUAL_INT TEST_ASSERT_EQUAL
#define TEST_ASSERT_EQUAL_HEX8 TEST_ASSERT_EQUAL
#define TEST_ASSERT_EQUAL_HEX64 TEST_ASSERT_EQUAL
#define TEST_ASSERT_NOT_EQUAL(a, b) auth_test::check((a) != (b), __LINE__)
#define TEST_ASSERT_NOT_NULL(a) auth_test::check((a) != nullptr, __LINE__)
#define TEST_ASSERT_EQUAL_STRING(a, b) auth_test::check(std::strcmp((a), (b)) == 0, __LINE__)
#define TEST_ASSERT_GREATER_THAN_UINT32(a, b) auth_test::check((b) > (a), __LINE__)
#define TEST_ASSERT_LESS_THAN_UINT32(a, b) auth_test::check((b) < (a), __LINE__)
