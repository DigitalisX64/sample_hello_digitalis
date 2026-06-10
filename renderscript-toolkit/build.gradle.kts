// Vendored from github.com/android/renderscript-intrinsics-replacement-toolkit
// (Apache 2.0, (C) The Android Open Source Project). Google does not publish
// this Toolkit to any Maven repository, so the only supported consumption path
// is building its Gradle module from source — it is vendored here as a library
// module. The C++/AdvSimd kernels (Blur, ColorMatrix, Histogram, Resize, …)
// compile for arm64-v8a and run under Berberis ARM64->x86_64 translation; the
// hello-renderscript-toolkit app module depends on this project and exercises
// them. This build file is adapted for the suite's AGP 9 + built-in Kotlin
// toolchain (no kotlin-android plugin, no kotlinOptions, namespace set, arm64
// only); the vendored sources under src/ are upstream-unchanged.
plugins {
    id("com.android.library")
}
android {
    namespace = "com.google.android.renderscript"
    compileSdk = 35
    defaultConfig {
        minSdk = 24
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake { cppFlags += "-std=c++17" }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    buildTypes { release { isMinifyEnabled = false } }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt") }
    }
}
dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
}
