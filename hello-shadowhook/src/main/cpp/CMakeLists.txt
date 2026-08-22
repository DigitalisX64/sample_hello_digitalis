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
project(helloshadowhook LANGUAGES CXX)

# ShadowHook ships a prefab module (shadowhook.h + libshadowhook.so) in its
# AAR; AGP's prefab=true generates the package config that find_package
# resolves. The imported target is shadowhook::shadowhook.
find_package(shadowhook REQUIRED CONFIG)

add_library(helloshadowhook SHARED native-lib.cpp)

target_link_libraries(helloshadowhook
    shadowhook::shadowhook
    android
    log)
