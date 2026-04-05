# Hello Digitalis Samples

ARM64-only sample apps for testing Digitalis binary translation.

## Credits

Most sample modules in this project are ported from the
[Android NDK Samples](https://github.com/android/ndk-samples) repository
by Google, licensed under the
[Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
Modifications were made to build as ARM64-only modules for testing
Digitalis binary translation.

The original `hello-vulkan` module was written for the Digitalis project.

## Modules

| Module | Description | Build |
|--------|-------------|-------|
| hello-vulkan | Vulkan triangle renderer | `./gradlew :hello-vulkan:assembleDebug` |

## Prerequisites

- Android SDK with NDK (cmake 3.22.1+)
- `glslangValidator` for GLSL-to-SPIR-V shader compilation (included in the NDK's `shader-tools/` or install via the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home))

## Build

Build all modules:

```bash
./gradlew assembleDebug
```

## Install and Run

On the Digitalis emulator (x86_64 with NativeBridge):

```bash
adb install hello-vulkan/build/outputs/apk/debug/hello-vulkan-debug.apk
adb shell am start -n com.example.hellodigitalis/android.app.NativeActivity
```

The apps are ARM64-only (`arm64-v8a`). They will not run on x86_64 devices without NativeBridge binary translation.
