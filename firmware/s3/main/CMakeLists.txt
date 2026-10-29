idf_component_register(
    SRCS
        "src/ai_chat_service.cpp"
        "src/application.cpp"
        "src/automation_service.cpp"
        "src/device_registry.cpp"
        "src/http_server.cpp"
        "src/http_access.cpp"
        "main.cpp"
        "src/mcp_service.cpp"
        "src/model_repository.cpp"
        "src/ota_service.cpp"
        "src/smtp_service.cpp"
        "src/thread_dataset.cpp"
        "src/wifi_profile.cpp"
        "src/wifi_provision_service.cpp"
    INCLUDE_DIRS "include"
    LDFRAGMENTS "matter_linker.lf"
    REQUIRES
        core_activity
        core_ai
        core_board
        core_display
        core_domain
        core_gateway
        core_network
        core_matter
        core_ota
        core_security
        core_smtp
        core_storage
        app_update
        bootloader_support
        esp_event
        esp_http_client
        esp_http_server
        esp_netif
        esp_wifi
        espressif__cjson
        mbedtls
        nvs_flash
    EMBED_FILES
        "../../../dist/index.html.gz"
    EMBED_TXTFILES
        "../../../dist/csp.txt"
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_26)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
