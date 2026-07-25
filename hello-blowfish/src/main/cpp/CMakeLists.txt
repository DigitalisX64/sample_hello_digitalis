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
project(helloblowfish LANGUAGES C)

add_library(helloblowfish SHARED native-lib.c)

# The whole point of this probe is the ARM64 instruction shape the Blowfish
# round function compiles to: ubfiz / lsr + and #0x3fc / indexed ldr for the
# four S-box lookups, an unrolled 16-round chain living in one region, and a
# stp pair storing back into the tables across a loop back-edge. The NDK debug
# build passes no -O flag, so clang defaults to -O0 and emits none of that --
# every value round-trips through the stack instead, and the sample stops
# testing what it was written to test. Force -O2 here (it comes after the
# per-config flags on the command line, so it wins) to pin the codegen.
target_compile_options(helloblowfish PRIVATE -O2)

target_link_libraries(helloblowfish
    PRIVATE
    log
)
