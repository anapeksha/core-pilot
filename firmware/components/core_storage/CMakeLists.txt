idf_component_register(SRCS "core_storage.cpp" "matter_devices_codec.cpp" "models_codec.cpp"
                            "wifi_codec.cpp"
                       INCLUDE_DIRS "." REQUIRES core_board core_domain core_security freertos nvs_flash)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
