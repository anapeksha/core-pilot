set(core_pilot_components "${CMAKE_CURRENT_LIST_DIR}/../../../components")

idf_component_register(
    SRCS
        "firmware_test_main.cpp"
        "${core_pilot_components}/core_board/test/test_core_board.cpp"
        "${core_pilot_components}/core_display/test/test_core_display.cpp"
        "${core_pilot_components}/core_gateway/test/test_core_gateway.cpp"
        "${core_pilot_components}/core_matter/test/test_lifecycle.cpp"
        "${core_pilot_components}/core_matter/test/test_terminal_history.cpp"
        "${core_pilot_components}/core_network/test/test_core_network.cpp"
        "${core_pilot_components}/core_network/src/core_network_policy.cpp"
        "${core_pilot_components}/core_ota/test/test_core_ota.cpp"
    INCLUDE_DIRS
        "${core_pilot_components}/core_board/include"
        "${core_pilot_components}/core_display/include"
        "${core_pilot_components}/core_gateway/include"
        "${core_pilot_components}/core_matter/include"
        "${core_pilot_components}/core_network/include"
        "${core_pilot_components}/core_ota/include"
    REQUIRES unity
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_26)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
