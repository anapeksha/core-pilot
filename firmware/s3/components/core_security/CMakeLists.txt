idf_component_register(SRCS "src/core_security.cpp" "src/core_auth.cpp" "src/esp_seed.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES mbedtls esp_partition esp_hw_support)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_26)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
