# SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
#
# espos-p4-cockpit: a pure ESP-IDF 6 project. WiFi, config, the
# web UI, SignalK discovery/token/stream and OTA come from the espOS
# submodule (espos/); this project adds the display HAL, LVGL and the JSON
# Layout Player. Build:  . espos/.idf-version's IDF; idf.py set-target esp32p4; idf.py build
cmake_minimum_required(VERSION 3.22)

file(READ "${CMAKE_CURRENT_LIST_DIR}/.idf-version" COCKPIT_IDF_VERSION)
string(STRIP "${COCKPIT_IDF_VERSION}" COCKPIT_IDF_VERSION)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(COCKPIT_IDF_ACTUAL "v${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")
if(NOT COCKPIT_IDF_ACTUAL STREQUAL COCKPIT_IDF_VERSION AND NOT COCKPIT_ALLOW_IDF_MISMATCH)
    message(FATAL_ERROR "cockpit: .idf-version pins ${COCKPIT_IDF_VERSION} but IDF_PATH is ${COCKPIT_IDF_ACTUAL} "
                        "(pass -DCOCKPIT_ALLOW_IDF_MISMATCH=1 to override).")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/espos/components/espos_config/CMakeLists.txt")
    message(FATAL_ERROR "cockpit: espos/ submodule is empty — run `git submodule update --init`.")
endif()
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/espos/components")

# The 4B (720x720 ST7703) HAL is parked until a board is on hand again; its
# vendored driver predates the ESP-IDF 6 esp_lcd API. Drop it from the build
# rather than carry a dead #ifdef.
set(EXCLUDE_COMPONENTS esp_lcd_st7703)

# App-signing key for espOS OTA (see espos/docs/ota.md); never committed.
set(COCKPIT_SIGNING_KEY "${CMAKE_CURRENT_LIST_DIR}/secure_boot_signing_key.pem")
if(NOT EXISTS "${COCKPIT_SIGNING_KEY}")
    message(WARNING "cockpit: no secure_boot_signing_key.pem — generating a development RSA-3072 signing key.")
    execute_process(COMMAND ${PYTHON} -m espsecure generate_signing_key --version 2 --scheme rsa3072 "${COCKPIT_SIGNING_KEY}"
                    RESULT_VARIABLE rc OUTPUT_QUIET)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "cockpit: could not generate secure_boot_signing_key.pem")
    endif()
endif()

# LVGL configuration lives in the project, not in Kconfig.
idf_build_set_property(COMPILE_DEFINITIONS "-DLV_CONF_PATH=\"${CMAKE_CURRENT_LIST_DIR}/components/cockpit_hal/lv_conf.h\"" APPEND)

project(cockpit)

# espOS web UI → LittleFS "storage" image (built in espos/ui by `npm run build`).
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/espos/ui/dist-gz/index.html.gz")
    littlefs_create_partition_image(storage "${CMAKE_CURRENT_LIST_DIR}/espos/ui/dist-gz" FLASH_IN_PROJECT)
else()
    # WARNING, not STATUS: a STATUS line disappears into cmake's output and
    # the panel then silently serves espOS' placeholder page instead of the
    # real config UI, which looks like a firmware bug rather than a missing
    # build step.
    message(WARNING "cockpit: espos/ui/dist-gz missing — the device will serve the placeholder page, "
                    "not the espOS web UI. Build it with: (cd espos/ui && npm ci && npm run build)")
endif()
