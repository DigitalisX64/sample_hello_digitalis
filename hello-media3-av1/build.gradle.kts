plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellomedia3av1"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellomedia3av1"
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

    // Media3's AV1 software-decoder extension. This wraps Google's libgav1 AV1
    // decoder (C/C++) behind a JNI layer (libgav1JNI.so); Gav1Library.isAvailable()
    // dlopens that arm64-v8a native and runs it under Berberis ARM64->x86_64
    // translation, and Gav1Decoder feeds AV1 OBU frames into it. media3-exoplayer
    // provides the Decoder/SimpleDecoder/OutputBuffer base classes the extension
    // builds on.
    //
    // NOTE: androidx.media3:media3-decoder-av1 is NOT published as a prebuilt AAR
    // on Google Maven (verified 404 — see NOTES.md). The AV1 extension is
    // source-only and its arm64-v8a libgav1JNI.so must be NDK-built from the
    // media3 source tree, then dropped in as a local module or AAR. These
    // coordinates are the intended deps once that local artifact is supplied.
    implementation("androidx.media3:media3-decoder-av1:1.10.1")
    implementation("androidx.media3:media3-exoplayer:1.10.1")

    androidTestImplementation(project(":status-test-lib"))
}
