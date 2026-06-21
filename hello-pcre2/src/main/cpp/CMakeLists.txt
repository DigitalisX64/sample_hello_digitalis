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
project(hellopcre2 LANGUAGES C)

# PCRE2 ships as a prefab AAR (pcre2-ndk26-shared); AGP's prefab support exposes
# the "pcre2" CMake package, which provides the imported target pcre2::pcre2-8
# (the 8-bit code-unit build, libpcre2-8.so) plus the pcre2.h header.
find_package(pcre2 REQUIRED CONFIG)

add_library(hellopcre2 SHARED native-lib.c)

target_link_libraries(hellopcre2
    PRIVATE
    pcre2::pcre2-8
    log
)
