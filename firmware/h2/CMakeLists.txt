# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

if(NOT "$ENV{IDF_VERSION}" STREQUAL "6.0.2")
    message(FATAL_ERROR "Gateway RCP must be built with ESP-IDF 6.0.2 (found $ENV{IDF_VERSION})")
endif()
string(LENGTH "${GATEWAY_RCP_MANIFEST_HEX}" gateway_rcp_manifest_length)
if(NOT DEFINED GATEWAY_RCP_MANIFEST_HEX OR NOT gateway_rcp_manifest_length EQUAL 64 OR
   NOT GATEWAY_RCP_MANIFEST_HEX MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "GATEWAY_RCP_MANIFEST_HEX must be a 64-digit lowercase SHA-256")
endif()

set(GATEWAY_RCP_MANIFEST_BYTES "")
foreach(index RANGE 0 31)
    math(EXPR position "${index} * 2")
    string(SUBSTRING "${GATEWAY_RCP_MANIFEST_HEX}" ${position} 2 byte)
    string(APPEND GATEWAY_RCP_MANIFEST_BYTES "0x${byte},")
endforeach()
configure_file(main/gateway_rcp_manifest.h.in generated/gateway_rcp_manifest.h @ONLY)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(esp_ot_rcp)
