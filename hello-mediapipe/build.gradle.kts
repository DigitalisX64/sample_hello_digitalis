plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellomediapipe"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellomediapipe"
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

    // MediaPipe Tasks Vision — Google's on-device ML vision pipeline. The AAR
    // (from Google's Maven, already in the project repositories) ships
    // jni/arm64-v8a/libmediapipe_tasks_vision_jni.so, a bionic-linked Android
    // native that embeds TensorFlow Lite + the MediaPipe graph runtime and runs
    // under Berberis ARM64->x86_64 translation. The transitive tasks-core
    // dependency provides BaseOptions / MPImage / BitmapImageBuilder.
    implementation("com.google.mediapipe:tasks-vision:0.10.29")

    androidTestImplementation(project(":status-test-lib"))
}
