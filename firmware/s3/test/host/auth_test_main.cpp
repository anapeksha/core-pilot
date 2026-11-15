#include "unity.h"

int main() {
#ifdef COMPONENT_SUITES
  const char* required[] = {
      "test_core_board.cpp",   "test_core_display.cpp",     "test_core_gateway.cpp",
      "test_lifecycle.cpp",    "test_operation_policy.cpp", "test_terminal_history.cpp",
      "test_core_network.cpp", "test_core_ota.cpp",         "test_access_integration.cpp"};
#else
  const char* required[] = {"test_core_auth.cpp"};
#endif
  for (const auto* suite : required) {
    unsigned count = 0;
    for (unsigned i = 0; i < auth_test::count; ++i)
      if (std::strstr(auth_test::cases[i].file, suite))
        ++count;
    std::printf("SUITE: %s (%u cases)\n", suite, count);
    if (!count)
      return 1;
  }
  for (unsigned i = 0; i < auth_test::count; ++i) {
    std::printf("RUN: %s: %s\n", auth_test::cases[i].file, auth_test::cases[i].name);
    std::fflush(stdout);
    auth_test::cases[i].run();
    std::printf("PASS: %s\n", auth_test::cases[i].name);
  }
  std::printf("PASS: %u cases\n", auth_test::count);
  return 0;
}
