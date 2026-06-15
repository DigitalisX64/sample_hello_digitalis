plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellojavet"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellojavet"
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

    // Javet (Java + V8) — embeds the Google V8 JavaScript engine in Java via
    // a JNI native bridge. The V8-flavor Android artifact ships
    // jni/arm64-v8a/libjavet-v8-android.v.<ver>.so (the JNI bridge) plus the
    // bundled V8 native runtime, both bionic-linked arm64 natives that load
    // and run under Berberis ARM64->x86_64 translation. The plain "javet"
    // jar bundles only desktop (glibc/mac/win) natives, so the *-v8-android
    // artifact is required for the on-device arm64 .so. V8's optimizing
    // compiler (TurboFan/Sparkplug) generates and re-patches arm64 machine
    // code at runtime, exercising self-modifying code / IC IVAU handling.
    implementation("com.caoccao.javet:javet-v8-android:5.0.8")

    androidTestImplementation(project(":status-test-lib"))
}
