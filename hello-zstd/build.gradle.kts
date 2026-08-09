plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellozstd"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellozstd"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        // -PnativeBaseline builds this module for the host ABI under a separate
        // application id, so the same workload can be timed running natively as
        // the baseline the translated arm64 build is measured against. Without
        // the property nothing changes: same task names, same output paths.
        val nativeBaseline = project.hasProperty("nativeBaseline")
        ndk { abiFilters += if (nativeBaseline) "x86_64" else "arm64-v8a" }
        if (nativeBaseline) applicationIdSuffix = ".native"
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

    // Zstandard (zstd) JNI bindings — a native (C) compression library. The
    // AAR (note the "@aar" classifier) ships jni/arm64-v8a/libzstd-jni-*.so,
    // a bionic-linked Android native that loads and runs under Berberis
    // ARM64->x86_64 translation. The plain jar bundles only glibc/mac/win
    // natives (linux/aarch64 links libc.so.6), so the @aar is required.
    implementation("com.github.luben:zstd-jni:1.5.7-10@aar")

    implementation(project(":bench-lib"))
    androidTestImplementation(project(":status-test-lib"))
}
