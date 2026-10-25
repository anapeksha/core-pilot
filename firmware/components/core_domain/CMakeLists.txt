idf_component_register(SRCS "src/core_domain.cpp" "src/domain_contract.cpp" INCLUDE_DIRS "include" REQUIRES espressif__cjson)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
