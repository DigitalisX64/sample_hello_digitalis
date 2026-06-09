plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloquickjs"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloquickjs"
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

    // Cash App's QuickJS — a native (C) JavaScript engine. The AAR ships
    // libquickjs.so (arm64-v8a), which loads and runs the QuickJS bytecode
    // interpreter under Berberis ARM64->x86_64 translation.
    implementation("app.cash.quickjs:quickjs-android:0.9.2")

    androidTestImplementation(project(":status-test-lib"))
}
