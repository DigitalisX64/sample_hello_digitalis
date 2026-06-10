plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellopytorch"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellopytorch"
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

    // PyTorch Mobile (Lite) — libtorch native C++ via JNI. The AAR ships
    // jni/arm64-v8a/{libpytorch_jni_lite.so, libfbjni.so, libc++_shared.so},
    // bionic-linked arm64 natives that load and run under Berberis
    // ARM64->x86_64 translation. The abiFilter above keeps only the arm64-v8a
    // slice (the AAR also bundles x86/x86_64), so the app is genuinely
    // ARM64-only and exercises the translator rather than running natively.
    // The Java org.pytorch.Tensor API (fromBlob / dataAsFloatArray / shape)
    // drives real at::Tensor allocation + copy through libtorch without
    // needing a TorchScript model asset.
    implementation("org.pytorch:pytorch_android_lite:1.13.1")

    androidTestImplementation(project(":status-test-lib"))
}
