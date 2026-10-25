idf_component_register(
    SRCS "esp_ot_rcp.c"
    INCLUDE_DIRS "." "${CMAKE_BINARY_DIR}/generated"
    REQUIRES esp_driver_uart esp_event nvs_flash openthread vfs
)
