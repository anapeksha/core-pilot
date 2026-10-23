idf_component_register(
    SRCS "core_activity.cpp"
    INCLUDE_DIRS "include"
    REQUIRES core_security esp_timer freertos
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
