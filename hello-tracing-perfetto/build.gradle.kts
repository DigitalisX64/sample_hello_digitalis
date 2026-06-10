plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellotracingperfetto"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellotracingperfetto"
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

    // AndroidX Perfetto SDK tracing. tracing-perfetto provides the Java/Kotlin
    // API (PerfettoNative loader, Trace begin/end); tracing-perfetto-binary
    // ships the native libtracing_perfetto.so (arm64-v8a) that PerfettoNative
    // loads and that this sample exercises under Berberis ARM64->x86_64
    // translation. androidx.tracing:tracing provides Trace.beginSection/endSection.
    implementation("androidx.tracing:tracing:1.2.0")
    implementation("androidx.tracing:tracing-perfetto:1.0.1")
    implementation("androidx.tracing:tracing-perfetto-binary:1.0.1")

    androidTestImplementation(project(":status-test-lib"))
}
