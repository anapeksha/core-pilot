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

TEST_CASE("bootstrap is one-time and sessions expire", "[core_security]") {
  Random random;
  core_security::Auth auth;
  TEST_ASSERT_EQUAL(0, static_cast<int>(auth.begin(random, 10, capture)));
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::BootstrapState::open),
                    static_cast<int>(auth.bootstrap_state(10)));
  char token[65]{};
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::ExchangeResult::ok),
                    static_cast<int>(auth.exchange(physical_code, random, 11, token)));
  TEST_ASSERT_TRUE(auth.authorize(token, 12));
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::BootstrapState::consumed),
                    static_cast<int>(auth.bootstrap_state(12)));
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::ExchangeResult::unavailable),
                    static_cast<int>(auth.exchange(physical_code, random, 12, token)));
  TEST_ASSERT_FALSE(auth.authorize(token, 11 + core_security::Auth::kSessionSeconds));
}

TEST_CASE("bootstrap expiry does not permit network denial", "[core_security]") {
  Random random;
  core_security::Auth auth;
  char token[65]{};
  TEST_ASSERT_EQUAL(0, static_cast<int>(auth.begin(random, 10, capture)));
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::ExchangeResult::expired),
                    static_cast<int>(auth.exchange(physical_code, random, 310, token)));

  TEST_ASSERT_EQUAL(0, static_cast<int>(auth.regenerate(random, 400, capture)));
  for (unsigned attempt = 0; attempt < 8; ++attempt) {
    TEST_ASSERT_EQUAL(static_cast<int>(core_security::ExchangeResult::incorrect),
                      static_cast<int>(auth.exchange("0000000000000000", random, 401, token)));
  }
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::ExchangeResult::ok),
                    static_cast<int>(auth.exchange(physical_code, random, 402, token)));
}

TEST_CASE("physical regeneration revokes existing sessions", "[core_security]") {
  Random random;
  core_security::Auth auth;
  char first_token[65]{};
  TEST_ASSERT_EQUAL(0, static_cast<int>(auth.begin(random, 10, capture)));
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::ExchangeResult::ok),
                    static_cast<int>(auth.exchange(physical_code, random, 11, first_token)));
  TEST_ASSERT_EQUAL(0, static_cast<int>(auth.regenerate(random, 20, capture)));
  TEST_ASSERT_FALSE(auth.authorize(first_token, 21));
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::BootstrapState::open),
                    static_cast<int>(auth.bootstrap_state(21)));

  char second_token[65]{};
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::ExchangeResult::ok),
                    static_cast<int>(auth.exchange(physical_code, random, 22, second_token)));
  TEST_ASSERT_FALSE(auth.authorize(first_token, 23));
  TEST_ASSERT_TRUE(auth.authorize(second_token, 23));
}
