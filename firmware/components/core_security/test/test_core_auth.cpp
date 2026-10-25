#include "core_auth.hpp"
#include "unity.h"

#include <cstring>
namespace {
class Random final : public core_security::RandomSource {
  unsigned value_{};

public:
  core_security::Status fill(uint8_t* out, size_t size) override {
    for (size_t i = 0; i < size; ++i)
      out[i] = ++value_;
    return core_security::Status::Ok;
  }
};
char physical_code[17]{};
void capture(const char* code, uint64_t) {
  std::memcpy(physical_code, code, sizeof(physical_code));
}
} // namespace
TEST_CASE("RAM auth bounds attempts sessions and expiry", "[core_security]") {
  Random random;
  core_security::Auth auth;
  TEST_ASSERT_EQUAL(0, static_cast<int>(auth.begin(random, 10, capture)));
  char token[65]{};
  for (unsigned i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL(0, static_cast<int>(auth.exchange(physical_code, random, 11, token)));
    TEST_ASSERT_TRUE(auth.authorize(token, 12));
  }
  TEST_ASSERT_NOT_EQUAL(0, static_cast<int>(auth.exchange(physical_code, random, 11, token)));
  TEST_ASSERT_FALSE(auth.bootstrap_open(11));
  TEST_ASSERT_EQUAL(0, static_cast<int>(auth.begin(random, 20, capture)));
  TEST_ASSERT_EQUAL(0, static_cast<int>(auth.exchange(physical_code, random, 21, token)));
  TEST_ASSERT_FALSE(auth.authorize(token, 21 + core_security::Auth::kSessionSeconds));
  TEST_ASSERT_FALSE(auth.bootstrap_open(320));
  TEST_ASSERT_EQUAL(0, static_cast<int>(auth.begin(random, 30, capture)));
  for (unsigned i = 0; i < 5; ++i)
    TEST_ASSERT_NOT_EQUAL(0, static_cast<int>(auth.exchange("wrong", random, 31, token)));
  TEST_ASSERT_FALSE(auth.bootstrap_open(31));
}
