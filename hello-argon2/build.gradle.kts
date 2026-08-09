plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloargon2"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloargon2"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        // -PnativeBaseline builds this module for the host ABI under a separate
        // application id, so the same workload can be timed running natively as
        // the baseline the translated arm64 build is measured against. Without
        // the property nothing changes: same task names, same output paths.
        val nativeBaseline = project.hasProperty("nativeBaseline")
        ndk { abiFilters += if (nativeBaseline) "x86_64" else "arm64-v8a" }
        if (nativeBaseline) applicationIdSuffix = ".native"
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

    // Argon2Kt — Kotlin bindings for the native Argon2 password-hashing library
    // (winner of the Password Hashing Competition). The AAR ships
    // jni/arm64-v8a/libargon2jni.so (the JNI shim) and
    // jni/arm64-v8a/libargon2native.so (the Argon2 reference C implementation),
    // both bionic-linked Android natives that load and run under Berberis
    // ARM64->x86_64 translation. Resolved from Maven Central (the library's
    // own publishing group; the GitHub tags do not build on JitPack because
    // their release publication requires GPG signing).
    implementation("com.lambdapioneer.argon2kt:argon2kt:1.6.0")

    implementation(project(":bench-lib"))

    androidTestImplementation(project(":status-test-lib"))
}
