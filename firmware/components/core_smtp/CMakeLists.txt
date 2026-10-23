idf_component_register(
    SRCS "core_smtp.cpp"
    INCLUDE_DIRS "include"
    REQUIRES core_security esp-tls mbedtls freertos lwip
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
