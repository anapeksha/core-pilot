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

TEST_CASE("session revocation immediately zeroizes the matching bearer", "[core_security]") {
  Random random;
  core_security::Auth auth;
  TEST_ASSERT_EQUAL(0, static_cast<int>(auth.begin(random, 10, capture)));
  char token[65]{};
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::ExchangeResult::ok),
                    static_cast<int>(auth.exchange(physical_code, random, 11, token)));
  TEST_ASSERT_TRUE(auth.authorize(token, 12));

  char unknown[65]{};
  std::memset(unknown, 'f', 64);
  TEST_ASSERT_FALSE(auth.revoke(unknown));
  TEST_ASSERT_TRUE(auth.authorize(token, 12));
  TEST_ASSERT_FALSE(auth.revoke("short"));
  TEST_ASSERT_TRUE(auth.authorize(token, 12));

  TEST_ASSERT_TRUE(auth.revoke(token));
  TEST_ASSERT_FALSE(auth.authorize(token, 12));
  TEST_ASSERT_FALSE(auth.revoke(token));
}

TEST_CASE("failed bootstrap guesses require physical regeneration", "[core_security]") {
  Random random;
  core_security::Auth auth;
  char token[65]{};
  TEST_ASSERT_EQUAL(0, static_cast<int>(auth.begin(random, 400, capture)));

  for (unsigned attempt = 1; attempt < core_security::Auth::kBootstrapAttemptLimit; ++attempt) {
    TEST_ASSERT_EQUAL(static_cast<int>(core_security::ExchangeResult::incorrect),
                      static_cast<int>(auth.exchange("0000000000000000", random, 401, token)));
  }
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::ExchangeResult::attempts_exhausted),
                    static_cast<int>(auth.exchange("0000000000000000", random, 401, token)));
  TEST_ASSERT_EQUAL_STRING("", token);
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::BootstrapState::attempts_exhausted),
                    static_cast<int>(auth.bootstrap_state(450)));
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::ExchangeResult::attempts_exhausted),
                    static_cast<int>(auth.exchange(physical_code, random, 450, token)));
  TEST_ASSERT_EQUAL_STRING("", token);

  TEST_ASSERT_EQUAL(0, static_cast<int>(auth.regenerate(random, 460, capture)));
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::ExchangeResult::ok),
                    static_cast<int>(auth.exchange(physical_code, random, 461, token)));
  TEST_ASSERT_TRUE(auth.authorize(token, 461));
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::BootstrapState::consumed),
                    static_cast<int>(auth.bootstrap_state(461)));
}

TEST_CASE("bootstrap expiry closes an exhausted exchange", "[core_security]") {
  Random random;
  core_security::Auth auth;
  char token[65]{};
  TEST_ASSERT_EQUAL(0, static_cast<int>(auth.begin(random, 10, capture)));

  for (unsigned attempt = 0; attempt < core_security::Auth::kBootstrapAttemptLimit; ++attempt)
    auth.exchange("0000000000000000", random, 300, token);
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::BootstrapState::attempts_exhausted),
                    static_cast<int>(auth.bootstrap_state(309)));
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::ExchangeResult::expired),
                    static_cast<int>(auth.exchange(physical_code, random, 310, token)));
  TEST_ASSERT_EQUAL_STRING("", token);
  TEST_ASSERT_EQUAL(static_cast<int>(core_security::BootstrapState::expired),
                    static_cast<int>(auth.bootstrap_state(330)));
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
