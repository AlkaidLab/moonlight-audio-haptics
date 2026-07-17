# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.16)
project(moonlight_haptics_package_consumer LANGUAGES CXX)

find_package(moonlight_haptics 0.5.14 CONFIG REQUIRED)

add_executable(moonlight_haptics_package_consumer main.cpp)
target_link_libraries(moonlight_haptics_package_consumer
    PRIVATE moonlight::haptics)
