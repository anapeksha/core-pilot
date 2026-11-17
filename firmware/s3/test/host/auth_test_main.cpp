#include "unity.h"
#ifdef AI_DOMAIN_SUITES
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

int main() {
#if defined(AI_DOMAIN_SUITES)
  const char* required[] = {"test_core_ai.cpp", "test_core_domain.cpp"};
#elif defined(COMPONENT_SUITES)
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
  unsigned failures = 0;
  for (unsigned i = 0; i < auth_test::count; ++i) {
    std::printf("RUN: %s: %s\n", auth_test::cases[i].file, auth_test::cases[i].name);
    std::fflush(stdout);
#ifdef AI_DOMAIN_SUITES
    // An assertion abort must not prevent execution of the remaining component cases.
    const pid_t child = fork();
    if (child < 0) {
      std::perror("fork");
      return 1;
    }
    if (child == 0) {
      auth_test::cases[i].run();
      std::exit(0);
    }
    int status{};
    pid_t waited;
    do {
      waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child) {
      std::perror("waitpid");
      return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      ++failures;
      if (WIFSIGNALED(status))
        std::printf("FAIL: %s (signal %d)\n", auth_test::cases[i].name, WTERMSIG(status));
      else if (WIFEXITED(status))
        std::printf("FAIL: %s (exit %d)\n", auth_test::cases[i].name, WEXITSTATUS(status));
      else
        std::printf("FAIL: %s (wait status %d)\n", auth_test::cases[i].name, status);
      continue;
    }
#else
    auth_test::cases[i].run();
#endif
    std::printf("PASS: %s\n", auth_test::cases[i].name);
  }
  std::printf("RESULT: %u passed, %u failed\n", auth_test::count - failures, failures);
  return failures ? 1 : 0;
}
