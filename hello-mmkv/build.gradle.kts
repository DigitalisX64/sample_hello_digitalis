plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellommkv"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellommkv"
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

    // Tencent MMKV — a native (C++) key-value store. The AAR ships libmmkv.so
    // (arm64-v8a), which loads and runs under Berberis ARM64->x86_64 translation.
    implementation("com.tencent:mmkv:2.4.0")

    androidTestImplementation(project(":status-test-lib"))
}
