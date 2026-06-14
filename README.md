# Hello Digitalis Samples

ARM64-only sample apps for testing Digitalis ARM64→x86_64 binary translation.
Every module builds a single-ABI (`arm64-v8a`) APK; on the Digitalis x86_64
emulator all of its native code runs through NativeBridge translation
(`libberberis_arm64.so`). The samples are the spec: when one fails, the bug is
in the translator, not the sample.

## Credits

The NDK-port modules are ported from Google's
[Android NDK Samples](https://github.com/android/ndk-samples), licensed under
the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0), modified
to build as ARM64-only modules. `renderscript-toolkit` vendors AOSP's
[RenderScript intrinsics replacement toolkit](https://github.com/android/renderscript-intrinsics-replacement-toolkit)
source (Apache 2.0). The third-party-library samples pull each library's
published arm64-v8a artifact from Maven (or a public mirror); those libraries
remain under their own licenses. Everything else was written for the Digitalis
project.

## Module catalog (85 samples)

83 modules build inside this Gradle project; `hello-qt` and `hello-realm`
build standalone (see [Standalone builds](#standalone-builds)). Build any
suite module with `./gradlew :<module>:assembleDebug`.

### NDK-samples ports (21)

| Module | Exercises |
|--------|-----------|
| hello-jni | Basic JNI — calls C code from a Kotlin Activity |
| hello-jniCallback | JNI callbacks — native code calls Java methods |
| exceptions | C++ exception handling across the JNI boundary |
| bitmap-plasma | Plasma effect rendered into an Android Bitmap via JNI |
| hello-gl2 | OpenGL ES 2.0 triangle via JNI |
| gles3jni | OpenGL ES 3.0 instanced rendering |
| native-activity | Pure C++ NativeActivity with EGL/GLES |
| native-audio | OpenSL ES playback and recording |
| native-codec | Video playback through the NDK media codec API |
| native-midi | Android native MIDI API |
| sensor-graph | Accelerometer visualization with OpenGL |
| camera-basic | Camera2 NDK preview and JPEG capture |
| camera-texture-view | Camera preview into a TextureView |
| teapots-classic | Utah teapot, GLES 2.0 + touch gestures |
| teapots-more | GLES 3.0 instanced teapots |
| teapots-textured | Textured teapot with ImageDecoder |
| endless-tunnel | 3D tunnel game (scene management, GLES 2.0) |
| sanitizers | HWASan/ASan/UBSan demo |
| unit-test | Native GoogleTest via Prefab |
| vectorization | SIMD vectorization benchmarks |
| orderfile | Linker order-file optimization |

### Graphics & proxy-library smoke tests (8)

| Module | Exercises |
|--------|-----------|
| hello-vulkan | Vulkan triangle renderer (the original Digitalis sample) |
| hello-gles1 | GLES 1.x calls through `libberberis_proxy_libGLESv1_CM` |
| hello-gles3 | OpenGL ES 3.2 EGL context (granted as host-max ES 3.1), deterministic pattern |
| hello-msaa | 1x/2x/4x/8x multisample FBO grid, resolved into one frame |
| hello-aaudio | AAudio stream builder through `libberberis_proxy_libaaudio` |
| hello-binder-ndk | NDK binder define/new + host-thread callback round-trip |
| hello-nnapi | NNAPI device enumeration through `libberberis_proxy_libneuralnetworks` |
| hello-webview-functor | `libwebviewchromium_plat_support` WebView hardware-accel draw-functor registration (RegisterDrawFunctor/RegisterDrawGLFunctor/RegisterGraphicsUtils) |

### ARM extension & ABI probes (21)

| Module | Exercises |
|--------|-----------|
| hello-neon | NEON intrinsics battery: arithmetic, permutes (EXT, REV32, DUP), CRC32/CRC32C, URECPE/URSQRTE |
| hello-fp-vector | Vector FP three-same: FMUL/FADD/FSUB/FMLA/FMLS on `.4S` and `.2D` |
| hello-fp16 | Armv8.2-FP16 half-precision ops, scalar FCVTAS/FCVTAU |
| hello-bf16 | Armv8.6-BF16 |
| hello-dotprod | Armv8.4-DotProd (SDOT/UDOT) |
| hello-jscvt | Armv8.3-JSCVT (FJCVTZS) |
| hello-complex | Armv8.3-FCMA (FCMLA/FCADD) |
| hello-lse | Armv8.1-LSE atomics (CAS/SWP/LDADD family) |
| hello-lrcpc | Armv8.3-LRCPC and Armv8.1-LOR (LDAPR/LDLAR family) |
| hello-ldxp | Exclusive pairs: LDXP/LDAXP/STXP/STLXP |
| hello-barriers | Memory/synchronization barriers (DMB/DSB/ISB) |
| hello-aes | AES crypto extension (AESE/AESD/AESMC/AESIMC) |
| hello-sha-crypto | SHA-1/SHA-2 and SM3 crypto extensions |
| hello-widemul | Widening multiplies (SMULL/UMULL/PMULL/PMULL2, high halves) |
| hello-cntvct | Generic-timer system registers via MRS (CNTFRQ/CNTVCT/CNTPCT_EL0) |
| hello-bti | Armv8.5-BTI branch-target identification |
| hello-pac-ret | Pointer-authentication return-address signing |
| hello-ld-interleave | NEON multi-structure LD1/ST1 and interleaved LD2/ST2 |
| hello-superpack-regress | Regression probes for JIT bugs once hit by Facebook/WhatsApp (LDP base aliasing and friends) |
| hello-libc-libm | Digitalis extra libc/libm fast-path trampolines |
| hello-sigaction | sigaction install/readback, SIGSEGV delivery + siglongjmp recovery |

### UI engines (3)

| Module | Exercises |
|--------|-----------|
| hello-reactnative | React Native + Hermes, prebuilt bytecode bundle |
| hello-lynx | Lynx (ReactLynx + PrimJS), prebuilt `.lynx.bundle` |
| hello-qt | Qt 6 widgets (standalone build) |

### Third-party native libraries (32)

Media:

| Module | Exercises |
|--------|-----------|
| hello-ijkplayer | bilibili ijkplayer (FFmpeg) playback |
| hello-libvlc | libVLC playback |
| hello-ffmpeg-kit | FFmpegKit — FFprobe media information over a bundled WAV |
| hello-oboe | Oboe — open/start/write/stop an audio stream |

Imaging:

| Module | Exercises |
|--------|-----------|
| hello-fresco | Fresco native image pipeline decode |
| hello-gpuimage | GPUImage native filter |
| hello-libpag | Tencent libpag PAG render |
| hello-gif | android-gif-drawable native GIF decode |
| hello-pdfium | PDFium page render |
| hello-renderscript-toolkit | RenderScript replacement Toolkit intrinsics (blur, histogram, …) via the vendored `renderscript-toolkit` module |

Vision & ML:

| Module | Exercises |
|--------|-----------|
| hello-opencv | OpenCV NEON computer vision |
| hello-tflite | TensorFlow Lite inference |
| hello-litert-llm | LiteRT-LM on-device LLM runtime |
| hello-pytorch | PyTorch Mobile inference |
| hello-ncnn | Tencent ncnn CPU inference (fp32 pinned) |
| hello-zxing | ZXing barcode decode |
| hello-tesseract | Tesseract OCR |

Crypto, storage & runtimes:

| Module | Exercises |
|--------|-----------|
| hello-sqlcipher | SQLCipher encrypted SQLite |
| hello-conscrypt | Conscrypt TLS/crypto provider |
| hello-libsignal | Signal Protocol session encrypt/decrypt |
| hello-realm | Realm Kotlin native store (standalone build) |
| hello-objectbox | ObjectBox store round-trip |
| hello-mmkv | Tencent MMKV mmap-backed key-value store |
| hello-zstd | zstd compression round-trip |
| hello-quickjs | QuickJS JavaScript engine eval |
| hello-cronet | Cronet TLS handshake |

AndroidX native:

| Module | Exercises |
|--------|-----------|
| hello-sqlite-bundled | androidx.sqlite bundled SQLite |
| hello-graphics-path | androidx.graphics.path iteration |
| hello-camera-core | CameraX native image-util |
| hello-tracing-perfetto | Perfetto SDK tracing |
| hello-appsearch | AppSearch/Icing (schema-less GenericDocument probe) |
| hello-ink | androidx.ink stroke geometry |

### Helper modules (not samples)

- `status-test-lib` / `screenshot-test-lib` — the instrumentation harnesses
  the samples share (see [Testing](#testing)).
- `renderscript-toolkit` — vendored library consumed by
  `hello-renderscript-toolkit`.

## Standalone builds

Two samples need toolchains the suite's AGP version cannot host, so they are
self-contained Gradle projects with their own `settings.gradle.kts`, pinned
wrapper, and `build-apk.sh`; they are intentionally absent from the suite's
`settings.gradle.kts`:

- **hello-qt** — Qt 6 widgets; verified by launch.
- **hello-realm** — Realm Kotlin's compiler plugin requires Kotlin ≤ 2.0.x, so
  it pins Gradle 8.9 / AGP 8.7.3 / Kotlin 2.0.20; verified by launch
  (`REALM OK` in logcat). See `hello-realm/NOTES.md`.

Two more module directories exist but are excluded from the build entirely:
**hello-media3-ffmpeg** and **hello-media3-av1**. Google publishes no prebuilt
AAR for the Media3 FFmpeg/AV1 decoder extensions (source-only artifacts), so
there is nothing to build against; each module's `NOTES.md` documents the gap
and the unblock step.

## Prerequisites

- JDK 21
- Android SDK with NDK and CMake 3.22.1+
- `glslangValidator` for GLSL-to-SPIR-V shader compilation (in the NDK's
  `shader-tools/`, or via the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home),
  or `apt install glslang-tools`)

## Build

```bash
./gradlew assembleDebug                       # all suite modules
./gradlew assembleDebug assembleAndroidTest   # what CI builds on every push
(cd hello-qt && ./build-apk.sh)               # standalone
(cd hello-realm && ./build-apk.sh)            # standalone
```

## Testing

Every suite module carries a deterministic instrumented test: a `StatusTest`
that runs the module's core native op and asserts a known result, or — for
rendering modules — a `ScreenshotTest` that compares the rendered frame
against a committed reference image. On a running Digitalis emulator:

```bash
./gradlew :hello-jni:connectedDebugAndroidTest
```

In the Digitalis AOSP tree the whole suite is driven by
`.claude/scripts/test-samples.sh`, which installs each APK, runs its
instrumentation, and watches logcat for `Fatal signal` /
`Undefined arm64 instruction` / `FATAL EXCEPTION`. The standalone modules are
verified by launch + logcat instead (their pinned toolchains can't consume
`status-test-lib`).

## Digitalis compatibility

As of 2026-06-15, **all 84 suite-tested modules PASS** on the Digitalis
emulator (`sdk_phone64_x86_64_digitalis`, ANGLE GLES + gfxstream Vulkan),
verified across all three harness modes: liveness (84/84), `StatusTest`
assertions (70/70), and `ScreenshotTest` pixel-compares (13/13) — no crashes,
every status assertion meets its expected result, every rendered frame matches
its reference. The two standalone modules pass their launch verification too:
`hello-realm` (`REALM OK`) and `hello-qt` (Qt window up, no fatal signal).

Samples are written against the full ARM64 API surface without regard to what
the translator implements yet. A sample that crashes with
`Undefined arm64 instruction` is a translator gap: the fix goes into
`frameworks/libs/binary_translation/` (decoder + interpreter, plus JIT when
applicable) — never into the sample.
