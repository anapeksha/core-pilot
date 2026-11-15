cmake_minimum_required(VERSION 3.20)
project(core_pilot_host_tests LANGUAGES CXX)

enable_testing()

add_executable(
    firmware_logic_tests
    firmware_logic_tests.cpp
    ../../components/core_network/src/core_network_policy.cpp
)
target_include_directories(
    firmware_logic_tests
    PRIVATE
        ../../components/core_board/include
        ../../components/core_display/include
        ../../components/core_gateway/include
        ../../components/core_matter/include
        ../../components/core_network/include
        ../../components/core_ota/include
)
target_compile_features(firmware_logic_tests PRIVATE cxx_std_23)
target_compile_options(
    firmware_logic_tests
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fno-exceptions
        -fno-rtti
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
)
target_link_options(
    firmware_logic_tests
    PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
)

add_test(NAME firmware_logic COMMAND firmware_logic_tests)

add_executable(
    firmware_auth_tests
    auth_test_main.cpp
    ../../components/core_security/src/core_auth.cpp
    ../../components/core_security/test/test_core_auth.cpp
)
target_include_directories(
    firmware_auth_tests PRIVATE auth_support ../../components/core_security/include
)
target_compile_features(firmware_auth_tests PRIVATE cxx_std_23)
target_compile_options(
    firmware_auth_tests PRIVATE -Wall -Wextra -Wpedantic -Werror
    -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer
)
target_link_options(
    firmware_auth_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
)
add_test(NAME firmware_auth COMMAND firmware_auth_tests)
