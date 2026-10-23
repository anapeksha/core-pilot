#include "application.hpp"

extern "C" void app_main() {
  static core_pilot::Application application;
  application.run();
}
