pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
        // bilibili ijkplayer was published to the now-defunct JCenter; the
        // aliyun public mirror still serves its arm64-v8a AAR.
        maven { url = uri("https://maven.aliyun.com/repository/public") }
        // JitPack for libraries distributed only via GitHub releases.
        maven { url = uri("https://jitpack.io") }
    }
}

rootProject.name = "hello-digitalis"
include(":hello-vulkan")
include(":hello-jni")
include(":hello-jniCallback")
include(":exceptions")
include(":bitmap-plasma")
include(":hello-gl2")
include(":gles3jni")
include(":native-activity")
include(":native-audio")
include(":native-codec")
include(":native-midi")
include(":sensor-graph")
include(":camera-basic")
include(":camera-texture-view")
include(":teapots-classic")
include(":teapots-more")
include(":teapots-textured")
include(":endless-tunnel")
include(":sanitizers")
include(":unit-test")
include(":vectorization")
include(":orderfile")
include(":hello-gles1")
include(":hello-gles3")
include(":hello-msaa")
include(":hello-ijkplayer")
include(":hello-opencv")
include(":hello-sqlcipher")
include(":hello-conscrypt")
include(":hello-graphics-path")
include(":hello-gif")
include(":hello-zxing")
include(":hello-quickjs")
include(":hello-sqlite-bundled")
include(":hello-tflite")
include(":hello-litert-llm")
include(":hello-libpag")
include(":hello-zstd")
include(":hello-libvlc")
include(":hello-lynx")
include(":hello-mmkv")
include(":hello-aaudio")
include(":hello-binder-ndk")
include(":hello-webview-functor")
include(":hello-nnapi")
include(":hello-fp-vector")
include(":hello-neon")
include(":hello-sha-crypto")
include(":hello-ld-interleave")
include(":hello-superpack-regress")
include(":hello-barriers")
include(":hello-bf16")
include(":hello-bti")
include(":hello-complex")
include(":hello-dotprod")
include(":hello-fp16")
include(":hello-jscvt")
include(":hello-libc-libm")
include(":hello-lrcpc")
include(":hello-lse")
include(":hello-ldxp")
include(":hello-cntvct")
include(":hello-sigaction")
include(":hello-aes")
include(":hello-cronet")
include(":hello-pac-ret")
include(":hello-widemul")
include(":screenshot-test-lib")
include(":status-test-lib")
include(":hello-reactnative")
include(":hello-ink")
include(":hello-appsearch")
include(":hello-libsignal")
include(":hello-fresco")
include(":hello-objectbox")
include(":hello-pdfium")
include(":hello-tracing-perfetto")
include(":renderscript-toolkit")
include(":hello-renderscript-toolkit")
include(":hello-pytorch")
include(":hello-gpuimage")
include(":hello-camera-core")
include(":hello-tesseract")
include(":hello-oboe")
include(":hello-ffmpeg-kit")
include(":hello-ncnn")
include(":hello-onnxruntime")
include(":hello-jna")
include(":hello-libsodium")
include(":hello-j2v8")
include(":hello-couchbase")
include(":hello-avif")
include(":hello-themis")
include(":hello-wcdb")
include(":hello-vosk")
include(":hello-mediapipe")
include(":hello-argon2")
include(":hello-webrtc")
include(":hello-duktape")
include(":hello-wireguard")
// The following three sample modules are present on disk but intentionally NOT
// registered in the suite — each is a documented known gap (the module's
// MainActivity/build.gradle.kts carries the detail), not a verified-passing
// sample:
//   hello-javet      — modern V8 5.0.8 SIGABRTs under translation (the older
//                       V8 in hello-j2v8 covers the V8-JIT-under-translation
//                       test and passes).
//   hello-maplibre   — libmaplibre native init throws std::wstring_convert
//                       "from_bytes error".
//   hello-mlkit-barcode — ML Kit's barcode pipeline requires Google Play
//                       Services, absent on the AOSP Digitalis emulator
//                       (the barhopper .so itself loads fine under translation).
include(":hello-fbjni")
include(":hello-libtorrent4j")
include(":hello-javacpp")
