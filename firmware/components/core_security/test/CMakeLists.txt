idf_component_register(SRCS "test_core_security.cpp" "test_core_auth.cpp" REQUIRES unity core_security)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
