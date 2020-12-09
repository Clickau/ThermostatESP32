cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_definitions(ARDUINO=100 ESP32 LOGGER_SELECTED_LEVEL=LOGGER_LEVEL_NONE)
project(ThermostatESP32)
