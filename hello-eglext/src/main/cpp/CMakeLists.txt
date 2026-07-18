cmake_minimum_required(VERSION 3.22.1)
project(helloeglext LANGUAGES CXX)

add_library(helloeglext SHARED native-lib.cpp)

# eglGetProcAddress extension-proc probe: creates a real ES2 pbuffer context and
# enforces that every ADVERTISED extension's entry point resolves non-NULL and
# behaves (robust getters cross-checked against the core API, EGL_KHR_debug
# guest-callback registration). Links EGL + GLESv2 through the translation
# proxy libraries.
target_link_libraries(helloeglext
    PRIVATE
    EGL
    GLESv2
    log
)
