idf_component_register(
    SRCS "src/core_gateway.cpp" "${CMAKE_BINARY_DIR}/gateway-rcp-generated/gateway_rcp_payload.cpp"
    INCLUDE_DIRS "include" "${CMAKE_BINARY_DIR}/gateway-rcp-generated"
    REQUIRES esp_driver_gpio esp_driver_uart mbedtls "espressif__esp-serial-flasher"
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_26)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
