idf_component_register(SRCS "core_ota.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES app_update mbedtls freertos)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
