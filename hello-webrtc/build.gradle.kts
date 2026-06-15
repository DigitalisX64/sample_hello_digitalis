plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellowebrtc"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellowebrtc"
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

    // WebRTC (webrtc-sdk Android build of Google's libwebrtc). The AAR ships
    // jni/arm64-v8a/libjingle_peerconnection_so.so — the full native WebRTC
    // stack (PeerConnection, SDP/codec negotiation, SCTP DataChannel, plus the
    // libsrtp/usrsctp/abseil-cpp/boringssl C++ runtime), a bionic-linked arm64
    // native that loads and runs under Berberis ARM64->x86_64 translation. The
    // probe drives native PeerConnectionFactory init and headless DataChannel
    // SDP-offer generation (no camera/mic/network).
    implementation("io.github.webrtc-sdk:android:125.6422.07")

    androidTestImplementation(project(":status-test-lib"))
}
