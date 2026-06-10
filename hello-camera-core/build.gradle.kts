plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellocameracore"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellocameracore"
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

    // androidx.camera:camera-core bundles jni/arm64-v8a/libimage_processing_util_jni.so,
    // a bionic-linked Android native (YUV<->RGB conversion, pixel rotation, Bitmap
    // copy helpers) that loads and runs under Berberis ARM64->x86_64 translation.
    // The probe drives ImageProcessingUtil.copyBitmapToByteBuffer /
    // copyByteBufferToBitmap, the only public native entry points that need no live
    // camera (no ImageProxy / Surface) — see NOTES.md.
    implementation("androidx.camera:camera-core:1.3.4")

    androidTestImplementation(project(":status-test-lib"))
}
