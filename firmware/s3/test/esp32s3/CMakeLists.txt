cmake_minimum_required(VERSION 3.16)

# Keep this compile/link check independent from the production component graph.
set(COMPONENTS main unity)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(core_pilot_esp32s3_tests)
