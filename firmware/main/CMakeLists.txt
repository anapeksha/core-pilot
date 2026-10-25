idf_component_register(
    SRCS
        "ai_chat_service.cpp"
        "application.cpp"
        "automation_service.cpp"
        "device_registry.cpp"
        "http_server.cpp"
        "http_access.cpp"
        "main.cpp"
        "mcp_service.cpp"
        "model_repository.cpp"
        "ota_service.cpp"
        "smtp_service.cpp"
        "thread_dataset.cpp"
        "wifi_profile.cpp"
        "wifi_provision_service.cpp"
    INCLUDE_DIRS "."
    LDFRAGMENTS "matter_linker.lf"
    REQUIRES
        core_activity
        core_ai
        core_board
        core_display
        core_domain
        core_network
        core_matter
        core_ota
        core_security
        core_smtp
        core_storage
        app_update
        esp_event
        esp_http_client
        esp_http_server
        esp_netif
        esp_wifi
        espressif__cjson
        mbedtls
        nvs_flash
    EMBED_FILES
        "../../dist/index.html.gz"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
