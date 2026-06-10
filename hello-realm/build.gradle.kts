// STANDALONE build (see settings.gradle.kts in this directory and NOTES.md):
// pins the toolchain Realm Kotlin 2.3.0 is ABI-compatible with — AGP 8.7.3 +
// the external Kotlin Android plugin 2.0.20 + the io.realm.kotlin compiler
// plugin. The suite's AGP 9.0 built-in Kotlin cannot load Realm's compiler
// plugin (NoSuchMethodError on a removed FIR internal), so this module builds
// via ./build-apk.sh instead of the suite's gradlew.
plugins {
    id("com.android.application") version "8.7.3"
    kotlin("android") version "2.0.20"
    id("io.realm.kotlin") version "2.3.0"
}
android {
    namespace = "com.example.hellodigitalis.hellorealm"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellorealm"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += "arm64-v8a" }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    kotlinOptions { jvmTarget = "1.8" }
    buildTypes { release { isMinifyEnabled = false } }
}
dependencies {
    // Explicit coordinates: the standalone build has no access to the suite's
    // version catalog (gradle/libs.versions.toml lives in the suite root).
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")

    // Realm Kotlin — an on-device object database. library-base transitively
    // pulls cinterop-android, whose AAR ships jni/arm64-v8a/librealmc.so — the
    // native Realm Core store that runs under Berberis ARM64->x86_64 translation.
    implementation("io.realm.kotlin:library-base:2.3.0")
}
