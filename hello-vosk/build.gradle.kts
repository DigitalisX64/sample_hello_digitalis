plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellovosk"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellovosk"
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

    // Vosk speech recognition — the offline Kaldi-based engine. The
    // vosk-android AAR ships jni/arm64-v8a/libvosk.so (~8.8 MB of Kaldi/
    // OpenFST/BLAS native code), a bionic-linked Android native that loads
    // and runs under Berberis ARM64->x86_64 translation. The Java binding
    // (org.vosk.LibVosk) reaches libvosk.so through JNA direct mapping
    // (Native.register), so JNA's own native, jni/arm64-v8a/libjnidispatch.so,
    // must also be present — pulled in via the jna AAR below. The "@aar"
    // classifier is what carries the Android arm64-v8a .so payloads (the
    // plain jars ship only desktop glibc/mac/win natives), and @aar suppresses
    // transitive resolution, so JNA is declared explicitly.
    implementation("com.alphacephei:vosk-android:0.3.47@aar")
    implementation("net.java.dev.jna:jna:5.13.0@aar")

    androidTestImplementation(project(":status-test-lib"))
}
