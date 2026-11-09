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
