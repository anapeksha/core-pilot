idf_component_register(
    SRCS "core_matter.cpp" "active_ble_scan.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        bt
        esp_event
        esp_netif
        esp_wifi
        espressif__esp_matter
        openthread
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
# Remove this wrapper when project-chip/connectedhomeip#74922 reaches ESP-Matter.
target_link_libraries(
    ${COMPONENT_LIB} INTERFACE "-Wl,--wrap=ble_gap_disc" "-Wl,--undefined=__wrap_ble_gap_disc"
)
