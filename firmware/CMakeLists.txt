cmake_minimum_required(VERSION 3.16)

# OTA ordering requires a stable SemVer application version, not Git describe output.
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "0.1.0")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(core_pilot)

# ESP-Matter 1.6.0~1 does not compile its optional pairing CLI with IDF 5.5.
# Core Pilot commissions through core_matter, so do not build that unused helper.
set(esp_matter_target __idf_espressif__esp_matter)
if(TARGET ${esp_matter_target})
    get_target_property(esp_matter_sources ${esp_matter_target} SOURCES)
    list(FILTER esp_matter_sources EXCLUDE REGEX "esp_matter_controller_pairing_command\\.cpp$")
    set_property(TARGET ${esp_matter_target} PROPERTY SOURCES "${esp_matter_sources}")
endif()

# Keep M5GFX's result type aligned with the firmware-wide no-exceptions policy.
idf_build_set_property(COMPILE_DEFINITIONS RESULT_DISABLE_EXCEPTIONS APPEND)
