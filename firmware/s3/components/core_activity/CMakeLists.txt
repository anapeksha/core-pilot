idf_component_register(
    SRCS "src/core_activity.cpp"
    INCLUDE_DIRS "include"
    REQUIRES core_security esp_timer freertos
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_26)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
