# Firmware build (ESP-IDF). For the desktop simulator see sim/CMakeLists.txt.
cmake_minimum_required(VERSION 3.22)
# Release builds write their version number to version.txt (see CI); local
# builds fall back to the git description.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/version.txt")
  file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" PROJECT_VER LIMIT_COUNT 1)
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vellum)
