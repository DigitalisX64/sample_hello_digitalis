plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellomedia3ffmpeg"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellomedia3ffmpeg"
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

    // Media3's native FFmpeg audio-decoder extension. media3-decoder-ffmpeg
    // wraps a JNI bridge (libffmpegJN.so) over a from-source FFmpeg build; the
    // arm64-v8a .so it carries is a bionic-linked Android native that runs under
    // Berberis ARM64->x86_64 translation. media3-exoplayer/-common supply the
    // MimeTypes constants and the Decoder/SimpleDecoder base classes the probe
    // touches.
    //
    // IMPORTANT: androidx.media3:media3-decoder-ffmpeg is NOT published on
    // Google Maven as a prebuilt AAR — it is source-only and must be NDK-built
    // locally (see NOTES.md for the metadata check + the manual build step).
    // The dependency is declared at the intended coordinate so that, once the
    // locally built AAR is dropped into a flatDir/local Maven repo, the module
    // resolves and links without further edits.
    implementation("androidx.media3:media3-decoder-ffmpeg:1.10.1")
    implementation("androidx.media3:media3-exoplayer:1.10.1")
    implementation("androidx.media3:media3-common:1.10.1")

    androidTestImplementation(project(":status-test-lib"))
}
