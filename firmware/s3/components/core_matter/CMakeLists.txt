idf_component_register(
    SRCS "src/core_matter.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        bt
        esp_event
        esp_netif
        esp_wifi
        espressif__esp_matter
        openthread
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_26)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
