plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellolitertllm"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellolitertllm"
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

    // Google AI Edge on-device LLM (MediaPipe Tasks GenAI). The AAR ships the
    // arm64-v8a libllm_inference_engine_jni.so (LiteRT / XNNPACK + the LLM
    // runtime), which loads and runs under Berberis ARM64->x86_64 translation.
    implementation("com.google.mediapipe:tasks-genai:0.10.35")

    androidTestImplementation(project(":status-test-lib"))
}
