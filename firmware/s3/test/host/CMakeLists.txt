cmake_minimum_required(VERSION 3.20)
project(core_pilot_host_tests LANGUAGES C CXX)

enable_testing()

set(components "${CMAKE_CURRENT_LIST_DIR}/../../components")
set(policy_includes
    "${components}/core_board/include"
    "${components}/core_display/include"
    "${components}/core_gateway/include"
    "${components}/core_matter/include"
    "${components}/core_network/include"
    "${components}/core_ota/include"
)

add_executable(
    firmware_logic_tests
    firmware_logic_tests.cpp
    "${components}/core_network/src/core_network_policy.cpp"
)
target_include_directories(firmware_logic_tests PRIVATE ${policy_includes})
add_test(NAME firmware_logic COMMAND firmware_logic_tests)

add_executable(
    firmware_auth_tests
    auth_test_main.cpp
    "${components}/core_security/src/core_auth.cpp"
    "${components}/core_security/test/test_core_auth.cpp"
)
target_include_directories(
    firmware_auth_tests PRIVATE auth_support "${components}/core_security/include"
)
add_test(NAME firmware_auth COMMAND firmware_auth_tests)

add_executable(
    firmware_component_tests
    auth_test_main.cpp
    ../test_access_integration.cpp
    "${components}/core_security/src/core_auth.cpp"
    "${components}/core_network/src/core_network_policy.cpp"
    "${components}/core_board/test/test_core_board.cpp"
    "${components}/core_display/test/test_core_display.cpp"
    "${components}/core_gateway/test/test_core_gateway.cpp"
    "${components}/core_matter/test/test_lifecycle.cpp"
    "${components}/core_matter/test/test_operation_policy.cpp"
    "${components}/core_matter/test/test_terminal_history.cpp"
    "${components}/core_network/test/test_core_network.cpp"
    "${components}/core_ota/test/test_core_ota.cpp"
)
target_include_directories(
    firmware_component_tests PRIVATE
    auth_support "${components}/core_security/include" ../../main/include ${policy_includes}
)
target_compile_definitions(firmware_component_tests PRIVATE COMPONENT_SUITES=1)
add_test(NAME firmware_components COMMAND firmware_component_tests)

set(CORE_PILOT_PSA_SOURCE "$ENV{IDF_PATH}/components/mbedtls/mbedtls/tf-psa-crypto" CACHE PATH "Pinned ESP-IDF TF-PSA-Crypto source directory")
if(NOT EXISTS "${CORE_PILOT_PSA_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "Set CORE_PILOT_PSA_SOURCE to ESP-IDF 6.0.2 components/mbedtls/mbedtls/tf-psa-crypto")
endif()
set(GEN_FILES OFF CACHE BOOL "Use SDK's shipped generated crypto sources" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
set(TF_PSA_CRYPTO_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/platform_support/crypto_config.h" CACHE FILEPATH "" FORCE)
add_subdirectory("${CORE_PILOT_PSA_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}/psa")

set(cjson_source "${CMAKE_CURRENT_BINARY_DIR}/deps/espressif__cjson/cJSON")
if(NOT EXISTS "${cjson_source}/cJSON.c")
    message(FATAL_ERROR "Run prepare_dependencies.py --build-dir ${CMAKE_CURRENT_BINARY_DIR} with the ESP-IDF 6.0.2 Python environment first")
endif()
add_library(host_cjson STATIC "${cjson_source}/cJSON.c")
add_executable(firmware_ai_domain_tests
    auth_test_main.cpp
    platform_support/host_random.cpp
    "${components}/core_ai/src/core_ai.cpp"
    "${components}/core_ai/test/test_core_ai.cpp"
    "${components}/core_security/src/core_security.cpp"
    "${components}/core_domain/src/core_domain.cpp"
    "${components}/core_domain/src/domain_contract.cpp"
    "${components}/core_domain/src/request_rate_limiter.cpp"
    "${components}/core_domain/test/test_core_domain.cpp"
)
target_include_directories(firmware_ai_domain_tests BEFORE PRIVATE
    "${CORE_PILOT_PSA_SOURCE}/include"
    platform_support auth_support
    "${components}/core_ai/include" "${components}/core_security/include"
    "${components}/core_domain/include"
    "${cjson_source}"
)
target_link_libraries(firmware_ai_domain_tests PRIVATE tfpsacrypto host_cjson)
target_compile_definitions(firmware_ai_domain_tests PRIVATE AI_DOMAIN_SUITES=1)
add_test(NAME firmware_ai_domain COMMAND firmware_ai_domain_tests)

foreach(target IN ITEMS firmware_logic_tests firmware_auth_tests firmware_component_tests firmware_ai_domain_tests)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_options(
        ${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
        -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
    )
    target_link_options(
        ${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
    )
endforeach()

set_tests_properties(firmware_logic firmware_auth firmware_components firmware_ai_domain PROPERTIES TIMEOUT 30)
