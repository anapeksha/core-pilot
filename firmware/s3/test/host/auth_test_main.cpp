#include "unity.h"

int main() {
  if (!auth_test::count)
    return 1;
  for (unsigned i = 0; i < auth_test::count; ++i) {
    auth_test::cases[i].run();
    std::printf("PASS: %s\n", auth_test::cases[i].name);
  }
  return 0;
}
