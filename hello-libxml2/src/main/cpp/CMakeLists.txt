# Copyright (C) 2026 utzcoz
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
project(hellolibxml2 LANGUAGES C)

# libxml2 is consumed as a prefab AAR (com.viliussutkus89.ndk.thirdparty:
# libxml2-ndk26-shared). The Android Gradle plugin generates the prefab CMake
# config; find_package(libxml2 CONFIG) imports the target libxml2::xml2.
find_package(libxml2 REQUIRED CONFIG)

add_library(hellolibxml2 SHARED native-lib.c)

# The prefab exports the module's include/ directory, but libxml2's public
# headers live under include/libxml2/libxml/ and reference each other as
# <libxml/...>. So the correct include root is the libxml2/ subdirectory of the
# prefab include dir; derive it from the imported target instead of hard-coding.
get_target_property(XML2_INCLUDE_DIRS libxml2::xml2 INTERFACE_INCLUDE_DIRECTORIES)
foreach(_inc ${XML2_INCLUDE_DIRS})
    target_include_directories(hellolibxml2 PRIVATE "${_inc}/libxml2")
endforeach()

target_link_libraries(hellolibxml2
    PRIVATE
    libxml2::xml2
    log
)
