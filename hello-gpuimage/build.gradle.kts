plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellogpuimage"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellogpuimage"
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

    // GPUImage — GPU-accelerated image filtering via OpenGL ES shaders, with a
    // native (C) libyuv decoder. The AAR ships jni/arm64-v8a/libyuv-decoder.so,
    // a bionic-linked Android native that loads and runs under Berberis
    // ARM64->x86_64 translation; the GLES filter pass drives the emulator GPU
    // through ANGLE. The probe runs GPUImage's synchronous offscreen path
    // (PixelBuffer / EGL pbuffer), so no Activity surface is required.
    implementation("jp.co.cyberagent.android:gpuimage:2.1.0")

    androidTestImplementation(project(":status-test-lib"))
}
