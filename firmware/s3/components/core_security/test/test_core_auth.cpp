#include "core_auth.hpp"
#include "unity.h"

#include <cstring>

namespace {
using namespace core_security;
class Random final : public RandomSource {
  unsigned value_{};

public:
  unsigned calls{};
  bool fail{};
  Status fill(uint8_t* out, size_t size) override {
    ++calls;
    for (size_t i = 0; i < size; ++i)
      out[i] = ++value_;
    return fail ? Status::CryptoError : Status::Ok;
  }
};
char physical_code[17]{};
unsigned sink_calls{};
uint64_t physical_expiry{};
void capture(const char* code, uint64_t expiry) {
  ++sink_calls;
  physical_expiry = expiry;
  std::memcpy(physical_code, code, sizeof(physical_code));
}
void state(Auth& auth, BootstrapState expected, uint64_t now) {
  TEST_ASSERT_EQUAL(static_cast<int>(expected), static_cast<int>(auth.bootstrap_state(now)));
}
void exchange(Auth& auth, const char* code, Random& random, uint64_t now, char (&token)[65],
              ExchangeResult expected) {
  TEST_ASSERT_EQUAL(static_cast<int>(expected),
                    static_cast<int>(auth.exchange(code, random, now, token)));
}
void empty_code(Auth& auth) {
  char code[17];
  std::memset(code, 'x', sizeof(code));
  auth.copy_bootstrap(code);
  for (char byte : code)
    TEST_ASSERT_EQUAL(0, byte);
  TEST_ASSERT_EQUAL(0, auth.bootstrap_expires());
}
} // namespace

TEST_CASE("boot and begin stay idle without random or physical output", "[core_security]") {
  Random random;
  Auth auth;
  const auto calls = sink_calls;
  state(auth, BootstrapState::idle, 0);
  auth.begin(capture);
  state(auth, BootstrapState::idle, 10);
  empty_code(auth);
  char token[65]{};
  exchange(auth, "0000000000000000", random, 10, token, ExchangeResult::unavailable);
  TEST_ASSERT_FALSE(auth.authorize(token, 10));
  TEST_ASSERT_EQUAL(0, random.calls);
  TEST_ASSERT_EQUAL(calls, sink_calls);
}

TEST_CASE("physical code is one-time and session expiry never slides", "[core_security]") {
  Random random;
  Auth auth;
  auth.begin(capture);
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 10));
  TEST_ASSERT_EQUAL(310, physical_expiry);
  char token[65]{};
  exchange(auth, physical_code, random, 11, token, ExchangeResult::ok);
  state(auth, BootstrapState::consumed, 12);
  empty_code(auth);
  char rejected[65]{};
  exchange(auth, physical_code, random, 12, rejected, ExchangeResult::unavailable);
  TEST_ASSERT_TRUE(auth.authorize(token, 1810));
  TEST_ASSERT_FALSE(auth.authorize(token, 1811));
}

TEST_CASE("expiry and exhaustion require physical regeneration", "[core_security]") {
  Random random;
  Auth auth;
  auth.begin(capture);
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 10));
  char token[65]{};
  exchange(auth, physical_code, random, 310, token, ExchangeResult::expired);
  empty_code(auth);
  exchange(auth, physical_code, random, 900, token, ExchangeResult::expired);
  TEST_ASSERT_EQUAL(1, random.calls);
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 1000));
  const auto calls = sink_calls;
  for (unsigned i = 1; i <= Auth::kBootstrapAttemptLimit; ++i)
    exchange(auth, "wrong", random, 1001, token,
             i == Auth::kBootstrapAttemptLimit ? ExchangeResult::attempts_exhausted
                                               : ExchangeResult::incorrect);
  empty_code(auth);
  exchange(auth, physical_code, random, 100000, token, ExchangeResult::attempts_exhausted);
  state(auth, BootstrapState::attempts_exhausted, 100000);
  TEST_ASSERT_EQUAL(2, random.calls);
  TEST_ASSERT_EQUAL(calls, sink_calls);
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 100001));
  exchange(auth, physical_code, random, 100002, token, ExchangeResult::ok);
}

TEST_CASE("authorize another browser preserves sessions but begin invalidates all",
          "[core_security]") {
  Random random;
  Auth auth;
  auth.begin(capture);
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 10));
  char first[65]{}, second[65]{}, rejected[65]{};
  exchange(auth, physical_code, random, 11, first, ExchangeResult::ok);
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 20));
  exchange(auth, physical_code, random, 21, second, ExchangeResult::ok);
  TEST_ASSERT_TRUE(auth.authorize(first, 22));
  TEST_ASSERT_TRUE(auth.authorize(second, 22));
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 30));
  const auto calls = random.calls;
  const auto outputs = sink_calls;
  auth.begin(capture);
  TEST_ASSERT_EQUAL(calls, random.calls);
  TEST_ASSERT_EQUAL(outputs, sink_calls);
  empty_code(auth);
  TEST_ASSERT_FALSE(auth.authorize(first, 31));
  TEST_ASSERT_FALSE(auth.authorize(second, 31));
  exchange(auth, physical_code, random, 31, rejected, ExchangeResult::unavailable);
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 40));
  exchange(auth, physical_code, random, 41, first, ExchangeResult::ok);
  TEST_ASSERT_FALSE(auth.revoke(nullptr));
  TEST_ASSERT_FALSE(auth.revoke("short"));
  TEST_ASSERT_TRUE(auth.revoke(first));
  TEST_ASSERT_FALSE(auth.authorize(first, 42));
  TEST_ASSERT_FALSE(auth.revoke(first));
}

TEST_CASE("random failures close codes without harming existing sessions", "[core_security]") {
  Random random;
  Auth auth;
  auth.begin(capture);
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 10));
  char first[65]{}, rejected[65]{};
  exchange(auth, physical_code, random, 11, first, ExchangeResult::ok);
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 20));
  const auto outputs = sink_calls;
  random.fail = true;
  TEST_ASSERT_EQUAL(Status::CryptoError, auth.regenerate(random, 21));
  TEST_ASSERT_EQUAL(outputs, sink_calls);
  state(auth, BootstrapState::unavailable, 22);
  empty_code(auth);
  TEST_ASSERT_TRUE(auth.authorize(first, 22));
  random.fail = false;
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 30));
  random.fail = true;
  std::memset(rejected, 'x', sizeof(rejected));
  exchange(auth, physical_code, random, 31, rejected, ExchangeResult::unavailable);
  for (char byte : rejected)
    TEST_ASSERT_EQUAL(0, byte);
  empty_code(auth);
  TEST_ASSERT_TRUE(auth.authorize(first, 32));
  exchange(auth, physical_code, random, 32, rejected, ExchangeResult::unavailable);
}

TEST_CASE("session capacity is bounded and expired slots can be reused", "[core_security]") {
  Random random;
  Auth auth;
  auth.begin(capture);
  char tokens[4][65]{};
  for (auto& token : tokens) {
    TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 10));
    exchange(auth, physical_code, random, 11, token, ExchangeResult::ok);
  }
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 20));
  char rejected[65]{};
  const auto calls = random.calls;
  exchange(auth, physical_code, random, 21, rejected, ExchangeResult::unavailable);
  TEST_ASSERT_EQUAL(calls, random.calls);
  for (auto& token : tokens)
    TEST_ASSERT_TRUE(auth.authorize(token, 22));
  TEST_ASSERT_TRUE(auth.revoke(tokens[1]));
  exchange(auth, physical_code, random, 23, rejected, ExchangeResult::ok);
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 1810));
  exchange(auth, physical_code, random, 1811, tokens[0], ExchangeResult::ok);
  TEST_ASSERT_TRUE(auth.authorize(rejected, 1811));
}

TEST_CASE("replacing a code rejects the old code and malformed credentials", "[core_security]") {
  Random random;
  Auth auth;
  auth.begin(capture);
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 10));
  char old_code[17]{};
  std::memcpy(old_code, physical_code, sizeof(old_code));
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, 20));
  char token[65]{};
  exchange(auth, old_code, random, 21, token, ExchangeResult::incorrect);
  exchange(auth, nullptr, random, 21, token, ExchangeResult::incorrect);
  exchange(auth, "", random, 21, token, ExchangeResult::incorrect);
  exchange(auth, "00000000000000000", random, 21, token, ExchangeResult::incorrect);
  exchange(auth, physical_code, random, 22, token, ExchangeResult::ok);
  TEST_ASSERT_FALSE(auth.authorize(nullptr, 23));
  TEST_ASSERT_FALSE(auth.authorize("", 23));
  char changed[65]{};
  std::memcpy(changed, token, sizeof(changed));
  changed[63] = changed[63] == '0' ? '1' : '0';
  TEST_ASSERT_FALSE(auth.authorize(changed, 23));
  TEST_ASSERT_FALSE(auth.revoke(changed));
  TEST_ASSERT_TRUE(auth.authorize(token, 23));
}
