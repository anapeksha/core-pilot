#pragma once

// Minimal host adapter for the shared Unity auth tests. Never print operands.
#include <cstdio>
#include <cstdlib>

namespace auth_test {
struct Case {
  const char* name;
  void (*run)();
};
inline Case cases[32]{};
inline unsigned count{};
struct Register {
  Register(const char* name, void (*run)()) {
    if (count == 32)
      std::abort();
    cases[count++] = {name, run};
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
#define TEST_CASE(name, tags) \
  static void AUTH_JOIN(auth_case_, __LINE__)(); \
  static auth_test::Register AUTH_JOIN(auth_register_, __LINE__)(name, AUTH_JOIN(auth_case_, __LINE__)); \
  static void AUTH_JOIN(auth_case_, __LINE__)()
#define TEST_ASSERT_EQUAL(expected, actual) \
  auth_test::check(static_cast<long long>(expected) == static_cast<long long>(actual), __LINE__)
#define TEST_ASSERT_TRUE(value) auth_test::check(!!(value), __LINE__)
#define TEST_ASSERT_FALSE(value) auth_test::check(!(value), __LINE__)
