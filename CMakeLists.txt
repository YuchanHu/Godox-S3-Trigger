# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET "esp32s3" CACHE STRING "Only supported chip")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/third_party/esp32_iq_tx/firmware")
set(PROJECT_VER "0.6.5")
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Godox-S3-Trigger)
if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR "Only ESP32-S3 is supported")
endif()
include("$ENV{IDF_PATH}/tools/cmake/version.cmake")
if(NOT "$ENV{IDF_VERSION}" STREQUAL "5.5.5")
    message(FATAL_ERROR "Private PHY integration requires ESP-IDF 5.5.5; got $ENV{IDF_VERSION}")
endif()
