plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellosecp256k1"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellosecp256k1"
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

    // libsecp256k1 — Bitcoin Core's optimized C library for EC operations on the
    // secp256k1 curve — via the acinq secp256k1-kmp Kotlin bindings. The android
    // AAR ships jni/arm64-v8a/libsecp256k1-jni.so (a bionic-linked native that
    // bundles both the JNI glue and the full libsecp256k1 C implementation), which
    // loads and runs under Berberis ARM64->x86_64 translation. The companion
    // secp256k1-kmp artifact carries the Secp256k1 entrypoint class (the android
    // jni artifact only ships the native loader), so both are required. ECDSA
    // sign/verify and ECDH drive 256-bit field/scalar arithmetic (UMULH-heavy).
    implementation("fr.acinq.secp256k1:secp256k1-kmp-jni-android:0.23.0")
    implementation("fr.acinq.secp256k1:secp256k1-kmp:0.23.0")

    implementation(project(":bench-lib"))

    androidTestImplementation(project(":status-test-lib"))
}
