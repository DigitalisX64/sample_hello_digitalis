plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellolibyuv"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellolibyuv"
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

    // libyuv — Google's NEON-accelerated YUV<->RGB color-conversion / scaling
    // library (C/C++ with ARM64 NEON kernels). The AAR ships
    // jni/arm64-v8a/libyuv_android.so, a bionic-linked Android native that loads
    // and runs under Berberis ARM64->x86_64 translation. The Kotlin wrapper's
    // convertTo() calls drive the native ARGB<->I420 conversion routines.
    implementation("io.github.crow-misia.libyuv:libyuv-android:0.43.2")

    androidTestImplementation(project(":status-test-lib"))
}
