idf_component_register(SRCS "core_ota.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_update esp_app_format esp_timer core_domain mbedtls freertos)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
