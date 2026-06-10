plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloffmpegkit"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloffmpegkit"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += "arm64-v8a" }
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    buildTypes { release { isMinifyEnabled = false } }
}
dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // FFmpegKit — the arthenica FFmpeg wrapper. Upstream retired the project on
    // 2025-01-06 and its native binaries were pulled from Maven Central, npm,
    // and CocoaPods on 2025-04-01, so the original com.arthenica:ffmpeg-kit-*
    // coordinates now 404. This is the community 16KB-page-size republish
    // (com.moizhassan.ffmpeg:ffmpeg-kit-16kb), an LGPL-3.0 AAR built from the
    // arthenica source with NDK r27d; its jni/arm64-v8a/ holds the full native
    // FFmpeg stack — libavcodec/libavfilter/libavformat/libavutil/libswresample/
    // libswscale + libffmpegkit.so — bionic-linked Androids that load and run
    // under Berberis ARM64->x86_64 translation. The Java API is the unchanged
    // com.arthenica.ffmpegkit.* surface (FFmpegKit/FFprobeKit/ReturnCode). FFmpeg
    // n6.0; sine lavfi source and the PCM/WAV muxer are compiled in, which the
    // probe relies on.
    implementation("com.moizhassan.ffmpeg:ffmpeg-kit-16kb:6.0.0")

    androidTestImplementation(project(":status-test-lib"))
}
