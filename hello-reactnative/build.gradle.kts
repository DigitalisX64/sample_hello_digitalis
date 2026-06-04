plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloreactnative"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloreactnative"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        // ARM64-only APK: exercises React Native + Hermes under Berberis translation.
        ndk { abiFilters += "arm64-v8a" }
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    buildTypes { release { isMinifyEnabled = false } }
    packaging { jniLibs { useLegacyPackaging = true } }
}
dependencies {
    implementation(libs.androidx.appcompat)
    // React Native runtime + Hermes JS engine (ARM64 native libs run under
    // Berberis translation). Old (bridge) architecture, matching the RN setup
    // NetEase Cloud Music ships. The JS bundle is pre-compiled to Hermes
    // bytecode under src/main/assets/index.android.bundle.
    implementation("com.facebook.react:react-android:0.79.7")
    implementation("com.facebook.react:hermes-android:0.79.7")
    androidTestImplementation(project(":status-test-lib"))
}
