plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellothemis"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellothemis"
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

    // Themis crypto (Cossack Labs) — a native (C) cryptography library built on
    // BoringSSL, exposed to Java via JNI. The group id is literally
    // "com.cossacklabs.com". The AAR (note the "@aar" classifier) ships
    // jni/arm64-v8a/libthemis_jni.so, a self-contained bionic-linked Android
    // native (libthemis + libsoter + BoringSSL statically linked, NEEDED only
    // libc/libm/libdl) that loads and runs the AES/HMAC crypto rounds under
    // Berberis ARM64->x86_64 translation.
    implementation("com.cossacklabs.com:themis:0.15.5@aar")

    androidTestImplementation(project(":status-test-lib"))
}
