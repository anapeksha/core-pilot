idf_component_register(SRCS "test_core_storage.cpp" REQUIRES unity core_storage)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
