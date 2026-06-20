plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellofilamentrender"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellofilamentrender"
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
    // Google Filament — a real-time physically based renderer with a native
    // C++ engine (arm64-v8a/libfilament-jni.so) under a thin Java API. This
    // sample drives Filament's native render pipeline (Engine, SwapChain,
    // Renderer, View, Camera, Skybox) to an on-screen SurfaceView, proving the
    // engine's GLES/Vulkan backend and its NEON-heavy math run under Berberis
    // ARM64->x86_64 translation. The AAR bundles the arm64-v8a native straight
    // into the APK's jniLibs, so no extra jniLibs/sourceSets wiring is needed.
    implementation("com.google.android.filament:filament-android:1.72.0")
    // gltfio — Filament's native glTF loader (arm64-v8a/libgltfio-jni.so) plus its
    // ubershader MaterialProvider, so the embedded glTF cube is parsed and shaded
    // without any offline-compiled .filamat material.
    implementation("com.google.android.filament:gltfio-android:1.72.0")
    androidTestImplementation(project(":screenshot-test-lib"))
}
