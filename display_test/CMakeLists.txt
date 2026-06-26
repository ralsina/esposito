# Minimal LovyanGFX display test for the Guition JC4827W543.
# Standalone ESP-IDF project; reuses the LovyanGFX already vendored by the
# main esposito project (no duplication).
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../managed_components/LovyanGFX")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(display_test)
