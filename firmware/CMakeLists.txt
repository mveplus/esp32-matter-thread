# Universal firmware for the Seeed XIAO ESP32-C5. One source tree builds every
# product in products/*.json. Use tools/build.sh rather than calling idf.py
# directly: it generates PRODUCT_GEN_DIR and picks the sdkconfig fragments.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "ESP_MATTER_PATH is not set. Build inside the pinned esp-matter container.")
endif()
if(NOT DEFINED PRODUCT_GEN_DIR)
    message(FATAL_ERROR "PRODUCT_GEN_DIR is not set. Build with tools/build.sh.")
endif()

# PROJECT_VER_NUMBER is the Matter software version. OTA only accepts a higher
# number, so tools/build.sh derives it from the release version.
if(NOT DEFINED FW_VERSION)
    set(FW_VERSION "0.0.1-dev")
endif()
if(NOT DEFINED FW_VERSION_NUMBER)
    set(FW_VERSION_NUMBER 1)
endif()
set(PROJECT_VER "${FW_VERSION}")
set(PROJECT_VER_NUMBER ${FW_VERSION_NUMBER})

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "${ESP_MATTER_PATH}/examples/common"
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components")

project(matter_board)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# RISC-V targets set -Wno-format but leave the flags that depend on -Wformat.
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
