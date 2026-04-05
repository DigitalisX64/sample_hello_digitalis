# Copyright (C) The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.22.1)
project("unittest")

find_package(googletest REQUIRED CONFIG)

add_library(adder OBJECT adder.cpp)

add_library(unittest SHARED
    $<TARGET_OBJECTS:adder>
    native-lib.cpp
)

target_link_libraries(unittest PRIVATE log)

add_library(app_tests SHARED adder_test.cpp)
target_link_libraries(app_tests
    PRIVATE
    $<TARGET_OBJECTS:adder>
    googletest::gtest
)
