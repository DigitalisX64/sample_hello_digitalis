plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellotflite"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellotflite"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += "arm64-v8a" }
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }
    androidResources {
        // Keep the .tflite model uncompressed so the native interpreter can
        // mmap it directly from the APK.
        noCompress += "tflite"
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

    // TensorFlow Lite — its arm64-v8a libtensorflowlite_jni.so runs the model
    // under Berberis ARM64->x86_64 translation, with the XNNPACK delegate
    // exercising NEON SIMD kernels. 2.16.1 is the newest release that ships a
    // real Android AAR with native libraries (2.17.0 is a jar-only artifact).
    implementation("org.tensorflow:tensorflow-lite:2.16.1")

    androidTestImplementation(project(":status-test-lib"))
}
