plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellosentryndk"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellosentryndk"
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

    // Sentry Android NDK integration — the aar ships jni/arm64-v8a/libsentry.so +
    // libsentry-android.so (bionic-linked). Initializing it loads the native crash
    // backend and installs a signal-handler-based stack unwinder (.eh_frame), a
    // distinct native subsystem to validate under Berberis ARM64->x86_64 translation.
    implementation("io.sentry:sentry-android-ndk:7.14.0")

    androidTestImplementation(project(":status-test-lib"))
}
