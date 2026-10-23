idf_component_register(
    SRCS "core_board.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_sdspi esp_lcd fatfs m5unified
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
