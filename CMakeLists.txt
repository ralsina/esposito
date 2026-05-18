cmake_minimum_required(VERSION 3.16)

# Set single-precision float as default for the entire project
# Disable double-promotion warnings since ESP-IDF logging uses double
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsingle-precision-constant -Wno-double-promotion")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsingle-precision-constant -Wno-double-promotion")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esposito)
