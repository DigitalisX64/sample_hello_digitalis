# Hello Digitalis

Minimal ARM64-only Vulkan triangle app for testing Digitalis binary translation. Renders an RGB-colored triangle using hardcoded vertex positions and colors.

## Prerequisites

- Android SDK with NDK (cmake 3.22.1+)
- `glslangValidator` for GLSL-to-SPIR-V shader compilation (included in the NDK's `shader-tools/` or install via the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home))

## Build

```bash
cd sample/hellodigitalis
./gradlew assembleDebug
```

The APK is output to `app/build/outputs/apk/debug/app-debug.apk`.

## Shaders

GLSL shader sources are in `app/src/main/cpp/shaders/`:

- `triangle.vert` — vertex shader (hardcoded triangle positions and RGB colors)
- `triangle.frag` — fragment shader (pass-through color)

CMake compiles these to SPIR-V at build time using `glslangValidator` and generates C headers (`triangle.vert.spv.h`, `triangle.frag.spv.h`) that are included by `vulkan_renderer.cpp`.

To modify the shaders, edit the `.vert`/`.frag` files and rebuild.

## Install and Run

On the Digitalis emulator (x86_64 with NativeBridge):

```bash
adb install app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.example.hellodigitalis/android.app.NativeActivity
```

The app is ARM64-only (`arm64-v8a`). It will not run on x86_64 devices without NativeBridge binary translation.
