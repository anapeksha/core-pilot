idf_component_register(SRCS "src/core_storage.cpp" "src/matter_devices_codec.cpp" "src/models_codec.cpp"
                            "src/wifi_codec.cpp"
                       INCLUDE_DIRS "include" REQUIRES core_board core_domain core_security freertos nvs_flash)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
