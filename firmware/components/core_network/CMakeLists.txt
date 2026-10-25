idf_component_register(SRCS "src/core_network.cpp" "src/core_network_policy.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_event esp_netif mdns)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
