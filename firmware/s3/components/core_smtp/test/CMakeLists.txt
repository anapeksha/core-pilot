idf_component_register(SRCS "test_core_smtp.cpp" INCLUDE_DIRS "." REQUIRES unity core_smtp lwip)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_26)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
