idf_component_register(SRCS "src/core_network.cpp" "src/core_network_policy.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_event esp_netif mbedtls mdns)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_26)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
