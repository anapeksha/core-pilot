idf_component_register(SRCS "src/core_display.cpp" INCLUDE_DIRS "include" REQUIRES core_board m5unified esp_timer)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_26)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
