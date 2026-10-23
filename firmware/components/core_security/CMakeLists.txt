idf_component_register(SRCS "core_security.cpp" "esp_seed.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES mbedtls esp_partition esp_hw_support)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
