cmake_minimum_required(VERSION 3.20)
project(core_pilot_host_tests LANGUAGES CXX)

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

foreach(target IN ITEMS firmware_logic_tests firmware_auth_tests firmware_component_tests)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_options(
        ${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(
        ${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
    )
endforeach()

set_tests_properties(firmware_logic firmware_auth firmware_components PROPERTIES TIMEOUT 30)
