#include "access_display_policy.hpp"
#include "unity.h"

namespace {
using namespace core_security;
using namespace core_pilot;
class Random final : public RandomSource {
  unsigned value{};

public:
  unsigned calls{};
  Status fill(uint8_t* out, size_t size) override {
    ++calls;
    for (size_t i = 0; i < size; ++i)
      out[i] = ++value;
    return Status::Ok;
  }
};
bool pressed(Auth& auth, uint64_t now, bool home = true, bool fresh = true, int x = 16,
             int y = 176) {
  return display_policy::access_button_pressed(accessDisplayState(auth.bootstrap_state(now)), home,
                                               fresh, x, y);
}
void tap(Auth& auth, Random& random, uint64_t now) {
  TEST_ASSERT_TRUE(pressed(auth, now));
  TEST_ASSERT_EQUAL(Status::Ok, auth.regenerate(random, now));
}
} // namespace

TEST_CASE("auth display boot wake hold and outside contact cannot issue access", "[integration]") {
  Auth auth;
  Random random;
  auth.begin();
  TEST_ASSERT_EQUAL(display_policy::AccessState::idle, accessDisplayState(auth.bootstrap_state(0)));
  TEST_ASSERT_FALSE(pressed(auth, 0, false));
  TEST_ASSERT_FALSE(pressed(auth, 0, true, false));
  TEST_ASSERT_FALSE(pressed(auth, 0, true, true, 196, 190));
  char code[17]{}, token[65]{};
  auth.copy_bootstrap(code);
  TEST_ASSERT_EQUAL_STRING("", code);
  TEST_ASSERT_EQUAL(ExchangeResult::unavailable, auth.exchange(code, random, 0, token));
  TEST_ASSERT_EQUAL(0, random.calls);
  tap(auth, random, 1);
  TEST_ASSERT_FALSE(pressed(auth, 1));
  auth.copy_bootstrap(code);
  TEST_ASSERT_EQUAL(ExchangeResult::ok, auth.exchange(code, random, 2, token));
  TEST_ASSERT_TRUE(auth.authorize(token, 2));
  TEST_ASSERT_EQUAL(display_policy::AccessState::granted,
                    accessDisplayState(auth.bootstrap_state(2)));
}

TEST_CASE("auth display rejection expiry exhaustion and recovery retain physical gate",
          "[integration]") {
  Auth auth;
  Random random;
  auth.begin();
  tap(auth, random, 10);
  char token[65]{};
  TEST_ASSERT_EQUAL(ExchangeResult::incorrect, auth.exchange("wrong", random, 11, token));
  TEST_ASSERT_FALSE(pressed(auth, 11));
  TEST_ASSERT_EQUAL(ExchangeResult::expired, auth.exchange("wrong", random, 310, token));
  TEST_ASSERT_EQUAL(display_policy::AccessState::expired,
                    accessDisplayState(auth.bootstrap_state(310)));
  tap(auth, random, 311);
  for (unsigned i = 0; i < Auth::kBootstrapAttemptLimit; ++i)
    TEST_ASSERT_EQUAL(i + 1 == Auth::kBootstrapAttemptLimit ? ExchangeResult::attempts_exhausted
                                                            : ExchangeResult::incorrect,
                      auth.exchange("wrong", random, 312, token));
  TEST_ASSERT_EQUAL(display_policy::AccessState::attempts_exhausted,
                    accessDisplayState(auth.bootstrap_state(312)));
  TEST_ASSERT_FALSE(pressed(auth, 312, false));
  tap(auth, random, 313);
  TEST_ASSERT_FALSE(pressed(auth, 313));
  TEST_ASSERT_EQUAL(display_policy::AccessState::unavailable,
                    accessDisplayState(static_cast<BootstrapState>(255)));
}

TEST_CASE("auth display independent browsers and reboot invalidate prior credentials",
          "[integration]") {
  Auth auth;
  Random random;
  auth.begin();
  char first[65]{}, second[65]{}, code[17]{}, rejected[65]{};
  tap(auth, random, 10);
  auth.copy_bootstrap(code);
  TEST_ASSERT_EQUAL(ExchangeResult::ok, auth.exchange(code, random, 11, first));
  tap(auth, random, 20);
  auth.copy_bootstrap(code);
  TEST_ASSERT_EQUAL(ExchangeResult::ok, auth.exchange(code, random, 21, second));
  TEST_ASSERT_TRUE(auth.authorize(first, 22));
  TEST_ASSERT_TRUE(auth.authorize(second, 22));
  TEST_ASSERT_TRUE(auth.revoke(first));
  TEST_ASSERT_TRUE(auth.authorize(second, 22));
  tap(auth, random, 30);
  auth.copy_bootstrap(code);
  const auto calls = random.calls;
  auth.begin();
  TEST_ASSERT_EQUAL(calls, random.calls);
  TEST_ASSERT_FALSE(auth.authorize(first, 31));
  TEST_ASSERT_FALSE(auth.authorize(second, 31));
  TEST_ASSERT_EQUAL(ExchangeResult::unavailable, auth.exchange(code, random, 31, rejected));
  TEST_ASSERT_EQUAL(display_policy::AccessState::idle,
                    accessDisplayState(auth.bootstrap_state(31)));
}

TEST_CASE("auth display maps every production state without permitting active or unavailable taps",
          "[integration]") {
  constexpr BootstrapState states[]{BootstrapState::idle,
                                    BootstrapState::open,
                                    BootstrapState::consumed,
                                    BootstrapState::expired,
                                    BootstrapState::attempts_exhausted,
                                    BootstrapState::unavailable};
  constexpr display_policy::AccessState expected[]{display_policy::AccessState::idle,
                                                   display_policy::AccessState::active,
                                                   display_policy::AccessState::granted,
                                                   display_policy::AccessState::expired,
                                                   display_policy::AccessState::attempts_exhausted,
                                                   display_policy::AccessState::unavailable};
  for (unsigned i = 0; i < 6; ++i) {
    TEST_ASSERT_EQUAL(expected[i], accessDisplayState(states[i]));
    TEST_ASSERT_EQUAL(i != 1 && i != 5, display_policy::access_button_pressed(
                                            accessDisplayState(states[i]), true, true, 16, 176));
    TEST_ASSERT_FALSE(
        display_policy::access_button_pressed(accessDisplayState(states[i]), false, true, 16, 176));
  }
}
