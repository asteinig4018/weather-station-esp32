cmake_minimum_required(VERSION 3.16)

# Pull in local components from the components/ directory
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(weather_station)
