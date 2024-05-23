cmake_minimum_required(VERSION 3.16.0)
set(CMAKE_MAKE_PROGRAM /opt/homebrew/bin/ninja CACHE FILEPATH "Path to Ninja")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Yakasaur)