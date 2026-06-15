plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellomaplibre"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellomaplibre"
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

    // MapLibre Native GL Android SDK — a maps rendering engine whose core is
    // C++ shipped as jni/arm64-v8a/libmaplibre.so inside the AAR. The first
    // MapLibre.getInstance(context) call loads that bionic-linked arm64-v8a
    // native and runs its real native initialization (TileServerOptions +
    // DefaultFileSource peer) under Berberis ARM64->x86_64 translation. Full
    // map rendering needs a GL surface + network tiles, but native init and
    // the FileSource / OfflineManager JNI paths run headlessly.
    implementation("org.maplibre.gl:android-sdk:13.3.0")

    androidTestImplementation(project(":status-test-lib"))
}
