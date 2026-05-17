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

| Module              | Description                                           | Build                                          |
|---------------------|-------------------------------------------------------|------------------------------------------------|
| hello-vulkan        | Vulkan triangle renderer                              | `./gradlew :hello-vulkan:assembleDebug`        |
| hello-jni           | Basic JNI — calls C code from Kotlin Activity         | `./gradlew :hello-jni:assembleDebug`           |
| hello-jniCallback   | JNI callbacks — native code calls Java methods        | `./gradlew :hello-jniCallback:assembleDebug`   |
| exceptions          | C++ exception handling across JNI boundary            | `./gradlew :exceptions:assembleDebug`          |
| bitmap-plasma       | Plasma effect rendered to Android Bitmap via JNI      | `./gradlew :bitmap-plasma:assembleDebug`       |
| hello-gl2           | OpenGL ES 2.0 triangle via JNI                        | `./gradlew :hello-gl2:assembleDebug`           |
| gles3jni            | OpenGL ES 3.0 with instanced rendering                | `./gradlew :gles3jni:assembleDebug`            |
| native-activity     | Pure C++ NativeActivity with EGL/GLES rendering       | `./gradlew :native-activity:assembleDebug`     |
| native-audio        | OpenSL ES audio playback and recording                | `./gradlew :native-audio:assembleDebug`        |
| native-codec        | Video playback using Native Media Codec API           | `./gradlew :native-codec:assembleDebug`        |
| native-midi         | Android Native MIDI API (requires Android 10+)        | `./gradlew :native-midi:assembleDebug`         |
| sensor-graph        | Accelerometer sensor visualization with OpenGL        | `./gradlew :sensor-graph:assembleDebug`        |
| camera-basic        | Camera2 NDK preview and JPEG capture                  | `./gradlew :camera-basic:assembleDebug`        |
| camera-texture-view | Camera preview with TextureView rendering             | `./gradlew :camera-texture-view:assembleDebug` |
| teapots-classic     | Utah teapot with GLES 2.0 and touch gestures          | `./gradlew :teapots-classic:assembleDebug`     |
| teapots-more        | GLES 3.0 instanced teapots rendering                  | `./gradlew :teapots-more:assembleDebug`        |
| teapots-textured    | Textured teapot with ImageDecoder (Android 11+)       | `./gradlew :teapots-textured:assembleDebug`    |
| endless-tunnel      | 3D tunnel game with scene management and GLES 2.0     | `./gradlew :endless-tunnel:assembleDebug`      |
| sanitizers          | Address/UB sanitizer demo (HWASan/ASan/UBSan)         | `./gradlew :sanitizers:assembleDebug`          |
| unit-test           | Native unit testing with GoogleTest via Prefab        | `./gradlew :unit-test:assembleDebug`           |
| vectorization       | SIMD vectorization benchmarks (matrix multiplication) | `./gradlew :vectorization:assembleDebug`       |
| orderfile           | Binary optimization with linker order files           | `./gradlew :orderfile:assembleDebug`           |
| hello-gles1         | GLES 1.x proxy-lib smoke test (`libGLESv1_CM`)        | `./gradlew :hello-gles1:assembleDebug`         |
| hello-aaudio        | AAudio proxy-lib smoke test (`libaaudio`)             | `./gradlew :hello-aaudio:assembleDebug`        |
| hello-binder-ndk    | NDK Binder proxy-lib smoke test (`libbinder_ndk`)     | `./gradlew :hello-binder-ndk:assembleDebug`    |
| hello-nnapi         | NNAPI proxy-lib smoke test (`libneuralnetworks`)      | `./gradlew :hello-nnapi:assembleDebug`         |
| hello-fp-vector     | NEON vector FP three-same regression (FMUL/FADD/FSUB/FMLA/FMLS on `.4S` and `.2D`) | `./gradlew :hello-fp-vector:assembleDebug`     |

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

**26 PASS / 0 CRASH** for the original sample modules; the new `hello-fp-vector` regression sample is PARTIAL (3/5 vector-FP ops pass — see the table below).

### Required smoke target: VulkanCapsViewer

`vulkancapsviewer/` pins Sascha Willems' [VulkanCapsViewer](https://github.com/SaschaWillems/VulkanCapsViewer) at tag 4.11 as a git submodule, with `Vulkan-Headers` v1.4.340 as its sub-submodule. `vulkancapsviewer-test/` wraps it with a smoke-test runner (`test.sh`) that installs a pre-built APK on the connected Digitalis emulator, launches the Qt activity, and watches logcat for `Undefined arm64 instruction` / `FATAL EXCEPTION` / process death.

This is a **required** end-to-end translation test, not optional — it's the cross-check that the FMUL `.4S` / PAC / interpreter-fallback fixes hold together on a real Qt+Vulkan workload rather than just the unit-style `hello-fp-vector` probe. The submodules are pulled automatically by `repo sync` (the `sample/hellodigitalis` entry in `.repo/manifests/digitalis.xml` carries `sync-s="true"`) or with `git submodule update --init --recursive` in a manual clone.

See `vulkancapsviewer-test/README.md` for the two paths to obtain the APK (upstream GitHub release, or a Qt-SDK build from the submodule).

| Module | Status | Notes |
|--------|--------|-------|
| hello-vulkan | PASS | Vulkan triangle renders correctly |
| hello-jni | PASS | Basic JNI works |
| hello-jniCallback | PASS | JNI callbacks work |
| exceptions | PASS | C++ exceptions work across JNI |
| bitmap-plasma | PASS | Plasma effect renders correctly |
| hello-gl2 | PASS | GLES 2.0 triangle renders |
| gles3jni | PASS | GLES 3.0 instanced rendering works |
| native-activity | PASS | NativeActivity with EGL/GLES works |
| native-audio | PASS | OpenSL ES audio works |
| native-codec | PASS | Media codec playback works |
| native-midi | PASS | MIDI API loads (no device on emulator) |
| sensor-graph | PASS | Accelerometer graph renders |
| camera-basic | PASS | Camera2 NDK works |
| camera-texture-view | PASS | Camera TextureView works |
| teapots-classic | PASS | GLES 2.0 teapot renders |
| teapots-more | PASS | GLES 3.0 instanced teapots render |
| teapots-textured | PASS | Textured teapot renders (with ifstream workaround) |
| endless-tunnel | PASS | 3D tunnel game runs |
| sanitizers | PASS | Sanitizer demo runs |
| unit-test | PASS | GoogleTest runs |
| vectorization | PASS | SIMD benchmarks run |
| orderfile | PASS | Order file demo runs |
| hello-gles1 | PASS | `glGetError()` resolves through `libberberis_proxy_libGLESv1_CM.so` |
| hello-aaudio | PASS | `AAudio_createStreamBuilder` resolves through `libberberis_proxy_libaaudio.so` |
| hello-binder-ndk | PASS | `AIBinder_Class_define` / `AIBinder_new` resolve through `libberberis_proxy_libbinder_ndk.so` |
| hello-nnapi | PASS | `ANeuralNetworks_getDeviceCount` resolves through `libberberis_proxy_libneuralnetworks.so` |
| hello-fp-vector | PARTIAL | `.4S`/`.2D` FMUL/FADD/FSUB pass; FMLA/FMLS still fail pending the JIT→interpreter Vd-flush fix (see translator commit notes) |

### Known Workarounds

- **teapots-textured** (`ndk_helper/JNIHelper.cpp`): `std::ifstream` construction is wrapped in `try { … } catch (...)` and falls through to `AAssetManager` on any exception. The fallback path is the standard way to read APK assets anyway, so this is the right shape long-term whether or not the translator-side issue (`std::locale` construction throwing) is later fixed.
