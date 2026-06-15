plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloavif"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloavif"
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

    // libavif Android bindings — the AOMedia AVIF image decoder (libavif on top
    // of the dav1d/aom AV1 codec). The AAR ships jni/arm64-v8a/libavif_android.so,
    // a bionic-linked Android native that loads and runs under Berberis
    // ARM64->x86_64 translation. AvifDecoder.getInfo()/decode() drive the full
    // AV1 still-image decode, whose inverse-transform, motion-compensation and
    // loop-filter inner loops are heavily NEON/SIMD — a strong translator test.
    implementation("org.aomedia.avif.android:avif:1.3.0.841110fd")

    androidTestImplementation(project(":status-test-lib"))
}
