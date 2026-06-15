plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellojna"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellojna"
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

    // Java Native Access (JNA) — runtime FFI: it loads native shared libraries
    // and invokes their C functions via libffi (no per-call JNI stub). The AAR
    // (note the "@aar" classifier) bundles jni/arm64-v8a/libjnidispatch.so, the
    // bionic-linked Android dispatcher that loads and runs under Berberis
    // ARM64->x86_64 translation. The plain jar ships only desktop natives
    // (glibc/mac/win), so the @aar is required to get the arm64-v8a native.
    implementation("net.java.dev.jna:jna:5.16.0@aar")

    androidTestImplementation(project(":status-test-lib"))
}
