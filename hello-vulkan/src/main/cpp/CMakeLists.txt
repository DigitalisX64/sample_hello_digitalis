cmake_minimum_required(VERSION 3.22.1)
# region digitalis
# project(hello_digitalis CXX)
project(hello_digitalis C CXX)
# endregion

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android native app glue
set(APP_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
add_library(native_app_glue STATIC ${APP_GLUE_DIR}/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${APP_GLUE_DIR})

# Ensure native_app_glue symbols are not stripped
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

# region digitalis - compile GLSL shaders to SPIR-V headers
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

# Find glslangValidator — check NDK shader-tools first, then system PATH
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS ${ANDROID_NDK}/shader-tools/${ANDROID_HOST_TAG}
          ${ANDROID_NDK}/shader-tools/linux-x86_64
          ${ANDROID_NDK}/shader-tools/darwin-x86_64
          ${ANDROID_NDK}/shader-tools/windows-x86_64)

if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found. Install the Vulkan SDK or use an NDK that includes shader-tools.")
endif()

set(SHADER_SOURCES
    ${SHADER_DIR}/triangle.vert
    ${SHADER_DIR}/triangle.frag
)

set(SHADER_HEADERS)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    string(REPLACE "." "_" SHADER_VAR ${SHADER_NAME})
    set(SPV_FILE ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
    set(HEADER_FILE ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv.h)

    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.0 -o ${SPV_FILE} ${SHADER}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${SHADER_NAME} -> SPIR-V"
    )

    # Convert .spv binary to a C uint32_t array header using xxd-style generation
    add_custom_command(
        OUTPUT ${HEADER_FILE}
        COMMAND ${CMAKE_COMMAND} -DSPV_FILE=${SPV_FILE} -DHEADER_FILE=${HEADER_FILE} -DVAR_NAME=${SHADER_VAR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/spv_to_header.cmake
        DEPENDS ${SPV_FILE}
        COMMENT "Generating ${SHADER_NAME}.spv.h"
    )

    list(APPEND SHADER_HEADERS ${HEADER_FILE})
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_HEADERS})
# endregion

add_library(hello_digitalis SHARED
    main.cpp
    vulkan_renderer.cpp
)

add_dependencies(hello_digitalis shaders)

find_library(android-lib android)
find_library(log-lib log)

target_include_directories(hello_digitalis PRIVATE
    ${APP_GLUE_DIR}
    ${SHADER_OUTPUT_DIR}
)

target_link_libraries(hello_digitalis
    native_app_glue
    ${android-lib}
    ${log-lib}
    vulkan
)
