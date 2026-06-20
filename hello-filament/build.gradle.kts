plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellofilament"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellofilament"
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

    // Google Filament — a native (C++) real-time PBR rendering engine. The AAR
    // ships jni/arm64-v8a/libfilament-jni.so, a bionic-linked Android native
    // that loads and runs under Berberis ARM64->x86_64 translation. This sample
    // drives Filament's native engine init and GPU-resource descriptor
    // allocation headlessly (no EGL/Vulkan SwapChain, so no display surface).
    implementation("com.google.android.filament:filament-android:1.72.0")

    androidTestImplementation(project(":status-test-lib"))
}
