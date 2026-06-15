plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloonnxruntime"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloonnxruntime"
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

    // ONNX Runtime for Android — Microsoft's native (C/C++) inference engine.
    // This Maven Central AAR bundles jni/arm64-v8a/libonnxruntime.so (the core
    // engine) and jni/arm64-v8a/libonnxruntime4j_jni.so (the Java<->C bridge),
    // both bionic-linked Android natives that load and run under Berberis
    // ARM64->x86_64 translation. The first OrtEnvironment call System.loadLibrary's
    // them; tensor allocation then exercises the engine's native memory path.
    implementation("com.microsoft.onnxruntime:onnxruntime-android:1.22.0")

    androidTestImplementation(project(":status-test-lib"))
}
