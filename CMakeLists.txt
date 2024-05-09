cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(YakasaurDemo)

target_add_binary_data(YakasaurDemo.elf "src/isrg_root.pem" TEXT)