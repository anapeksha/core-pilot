#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <unity_test_runner.h>
}

extern "C" void app_main() {
  const char* required[] = {"test_core_auth.cpp",        "test_core_board.cpp",
                            "test_core_display.cpp",     "test_core_gateway.cpp",
                            "test_lifecycle.cpp",        "test_operation_policy.cpp",
                            "test_terminal_history.cpp", "test_core_network.cpp",
                            "test_core_ota.cpp",         "test_access_integration.cpp"};
  for (const auto* suite : required) {
    unsigned count = 0;
    for (int i = 0; i < unity_get_test_count(); ++i) {
      test_desc_t info{};
      if (unity_get_test_info(i, &info) && info.file && std::strstr(info.file, suite))
        ++count;
    }
    std::printf("SUITE: %s (%u cases)\n", suite, count);
    if (!count) {
      std::printf("FAIL: required suite has no registered tests\n");
      std::abort();
    }
  }
  unity_run_menu();
}
