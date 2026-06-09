plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellolibvlc"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellolibvlc"
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

    // VideoLAN libVLC for Android — the LibVLC/MediaPlayer Java bindings plus
    // the native (C/C++) libvlc + libvlcjni stack. The AAR ships arm64-v8a
    // .so files (libvlc, libvlcjni, plus the VLC plugin modules), which load
    // and run under Berberis ARM64->x86_64 translation.
    implementation("org.videolan.android:libvlc-all:3.7.2")

    androidTestImplementation(project(":status-test-lib"))
}
