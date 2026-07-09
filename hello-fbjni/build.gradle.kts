plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellofbjni"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellofbjni"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += "arm64-v8a" }
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild {
            // fbjni is built against libc++_shared (its AAR ships that .so), so
            // our hybrid library must use the shared STL too.
            cmake { arguments += "-DANDROID_STL=c++_shared" }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    // Consume fbjni's prefab module (headers + libfbjni.so) from its AAR so the
    // native hybrid class can link against fbjni::fbjni.
    buildFeatures { prefab = true }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" }
    }
    buildTypes { release { isMinifyEnabled = false } }
}
dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // Facebook fbjni — the JNI helper runtime that backs React Native, PyTorch
    // Mobile, Hermes, Yoga, etc. The "@aar" classifier pulls the Android
    // artifact, which ships jni/arm64-v8a/libfbjni.so (a bionic-linked native
    // that runs its own JNI_OnLoad under Berberis ARM64->x86_64 translation).
    implementation("com.facebook.fbjni:fbjni:0.7.0@aar")
    // fbjni loads its native library through SoLoader's NativeLoader
    // abstraction (its class static initializers call
    // NativeLoader.loadLibrary("fbjni")). The lightweight "nativeloader"
    // artifact provides NativeLoader + the SystemDelegate that forwards to
    // System.loadLibrary; the probe installs that delegate before touching
    // fbjni so libfbjni.so actually loads.
    implementation("com.facebook.soloader:nativeloader:0.10.5")

    androidTestImplementation(project(":status-test-lib"))
}
