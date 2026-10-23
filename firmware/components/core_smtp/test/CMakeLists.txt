idf_component_register(SRCS "test_core_smtp.cpp" INCLUDE_DIRS "." REQUIRES unity core_smtp)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
