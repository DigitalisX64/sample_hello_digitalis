plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellohiddenapibypass"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellohiddenapibypass"
        // Hidden-API restrictions only exist on Android P (API 28) and above,
        // so there is nothing for HiddenApiBypass to exempt below 28.
        minSdk = 28
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

    // LSPosed HiddenApiBypass — a pure-Java/Kotlin library (the AAR ships no
    // native .so). It uses sun.misc.Unsafe plus a reflected VMRuntime call to
    // install hidden-API exemptions, letting an app reflectively reach @hide
    // framework APIs that the runtime would otherwise deny. Real ARM64 apps
    // (e.g. NetEase Cloud Music) depend on it, so it must work under Berberis
    // translation. Version 4.3 exposes addHiddenApiExemptions / invoke /
    // getDeclaredMethod. Declared directly (not via the version catalog) so
    // this module stays self-contained.
    implementation("org.lsposed.hiddenapibypass:hiddenapibypass:4.3")

    androidTestImplementation(project(":status-test-lib"))
}
