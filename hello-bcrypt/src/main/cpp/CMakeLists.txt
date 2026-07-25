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
project(hellobcrypt LANGUAGES C)

# There is no prefab AAR for bcrypt, so the upstream sources are vendored under
# cpp/ unmodified, in the same layout libbcrypt expects (it includes
# "crypt_blowfish/ow-crypt.h"). Both projects are public domain:
#   crypt_blowfish 1.3 - Solar Designer, https://www.openwall.com/crypt/
#   libbcrypt         - Ricardo Garcia, CC0
# x86.S from crypt_blowfish is deliberately omitted: it is the x86 assembly
# implementation, and this sample must build the portable C one for arm64-v8a so
# the translator sees real ARM64 Blowfish code.
add_library(hellobcrypt SHARED
    native-lib.c
    bcrypt.c
    crypt_blowfish/crypt_blowfish.c
    crypt_blowfish/crypt_gensalt.c
    crypt_blowfish/wrapper.c
)

target_include_directories(hellobcrypt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# wrapper.c exposes the crypt(3)-compatible names only when the platform ones
# are not being replaced; __SKIP_GNU keeps it from defining crypt/crypt_r, which
# bionic already provides, while still building crypt_rn and crypt_gensalt_rn.
target_compile_definitions(hellobcrypt PRIVATE __SKIP_GNU)

# The vendored sources are third-party and not warning-clean under the flags
# this project uses for its own code; keep their warnings from failing the build
# without weakening anything for native-lib.c.
set_source_files_properties(
    bcrypt.c
    crypt_blowfish/crypt_blowfish.c
    crypt_blowfish/crypt_gensalt.c
    crypt_blowfish/wrapper.c
    PROPERTIES COMPILE_OPTIONS "-Wno-everything"
)

# Optimize regardless of build type. A debug APK would otherwise compile the
# cipher at -O0, and the ARM64 code a translator has to get right here only
# appears with optimization on: the unrolled S-box round chain, and the
# vectorized key/salt XOR that reads a vector register at two different widths
# (a 64-bit fmov of the low lane next to a 128-bit eor of the whole register).
# Apps ship release-optimized native libraries, so an unoptimized sample would
# test a shape that never runs in the field.
target_compile_options(hellobcrypt PRIVATE -O2)

target_link_libraries(hellobcrypt PRIVATE log)
