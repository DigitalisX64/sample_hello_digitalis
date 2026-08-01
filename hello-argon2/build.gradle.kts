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
