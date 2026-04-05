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
| hello-jni | Basic JNI — calls C code from Kotlin Activity | `./gradlew :hello-jni:assembleDebug` |
| hello-jniCallback | JNI callbacks — native code calls Java methods | `./gradlew :hello-jniCallback:assembleDebug` |
| exceptions | C++ exception handling across JNI boundary | `./gradlew :exceptions:assembleDebug` |
| bitmap-plasma | Plasma effect rendered to Android Bitmap via JNI | `./gradlew :bitmap-plasma:assembleDebug` |
| hello-gl2 | OpenGL ES 2.0 triangle via JNI | `./gradlew :hello-gl2:assembleDebug` |
| gles3jni | OpenGL ES 3.0 with instanced rendering | `./gradlew :gles3jni:assembleDebug` |
| native-activity | Pure C++ NativeActivity with EGL/GLES rendering | `./gradlew :native-activity:assembleDebug` |
| native-audio | OpenSL ES audio playback and recording | `./gradlew :native-audio:assembleDebug` |
| native-codec | Video playback using Native Media Codec API | `./gradlew :native-codec:assembleDebug` |
| native-midi | Android Native MIDI API (requires Android 10+) | `./gradlew :native-midi:assembleDebug` |
| sensor-graph | Accelerometer sensor visualization with OpenGL | `./gradlew :sensor-graph:assembleDebug` |
| camera-basic | Camera2 NDK preview and JPEG capture | `./gradlew :camera-basic:assembleDebug` |
| camera-texture-view | Camera preview with TextureView rendering | `./gradlew :camera-texture-view:assembleDebug` |
| teapots-classic | Utah teapot with GLES 2.0 and touch gestures | `./gradlew :teapots-classic:assembleDebug` |
| teapots-more | GLES 3.0 instanced teapots rendering | `./gradlew :teapots-more:assembleDebug` |
| teapots-textured | Textured teapot with ImageDecoder (Android 11+) | `./gradlew :teapots-textured:assembleDebug` |
| endless-tunnel | 3D tunnel game with scene management and GLES 2.0 | `./gradlew :endless-tunnel:assembleDebug` |
| sanitizers | Address/UB sanitizer demo (HWASan/ASan/UBSan) | `./gradlew :sanitizers:assembleDebug` |
| unit-test | Native unit testing with GoogleTest via Prefab | `./gradlew :unit-test:assembleDebug` |
| vectorization | SIMD vectorization benchmarks (matrix multiplication) | `./gradlew :vectorization:assembleDebug` |
| orderfile | Binary optimization with linker order files | `./gradlew :orderfile:assembleDebug` |

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

## Digitalis Compatibility

Tested on the Digitalis x86_64 emulator with ARM64-to-x86_64 binary translation.

| Module | Status | Notes |
|--------|--------|-------|
| hello-vulkan | PASS | Renders Vulkan triangle correctly |
| hello-jni | CRASH | Missing CNT (popcount) instruction in translator |
| hello-jniCallback | CRASH | Same CNT instruction issue |
| exceptions | PASS | C++ exceptions work across JNI |
| bitmap-plasma | CRASH | SIGILL — missing SIMD FP instructions |
| hello-gl2 | CRASH | SIGILL in GL thread — missing SIMD FP |
| gles3jni | PASS | GLES 3.0 instanced rendering works |
| native-activity | PASS | NativeActivity with EGL/GLES works |
| native-audio | PASS | OpenSL ES audio works |
| native-codec | PASS | Media codec playback works |
| native-midi | PASS | MIDI API loads (no device on emulator) |
| sensor-graph | CRASH | Likely missing SIMD instructions |
| camera-basic | CRASH | Likely missing instructions or camera proxy |
| camera-texture-view | CRASH | Same as camera-basic |
| teapots-classic | CRASH | Likely missing SIMD/FP instructions |
| teapots-more | CRASH | Same |
| teapots-textured | CRASH | Same |
| endless-tunnel | CRASH | SIGILL — missing SIMD FP multiply |
| sanitizers | CRASH | Sanitizer runtime incompatible with translation |
| unit-test | CRASH | GoogleTest init hits missing instructions |
| vectorization | PASS | SIMD benchmarks run (basic paths) |
| orderfile | CRASH | Missing instructions during init |

**8 PASS / 14 CRASH**

### Common Crash Root Causes

Most crashes trace to 3 missing ARM64 instructions in the Berberis translator:

1. **`CNT V0.8B` (0x0e205800)** — AdvSIMD population count. Used by bionic's `__popcountsi2`. Blocks hello-jni, hello-jniCallback, and many others during libc init.
2. **`FMUL Vd.2D` (0x6ee04425)** — AdvSIMD floating-point multiply (double-precision). Used by math libraries. Blocks bitmap-plasma, hello-gl2, endless-tunnel.
3. **`UCVTF Dd, Xn` (0x9e230100)** — Scalar unsigned int-to-FP conversion. Used by various runtime paths.

### Fix Plan

Implementing these 3 instructions in the interpreter (`interpreter/arm64/interpreter.h`) and optionally the JIT (`lite_translator/arm64_to_x86_64/lite_translator.h`) should unblock most of the 14 crashing modules. The remaining crashes (camera, sanitizers) may need additional proxy libraries or runtime support.
