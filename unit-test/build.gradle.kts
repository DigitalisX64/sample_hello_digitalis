plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.unittest"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.unittest"
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
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" }
    }
    buildFeatures {
        viewBinding = true
        prefab = true
    }
}
dependencies {
    implementation(libs.androidx.appcompat)
    implementation(libs.material)
    implementation(libs.androidx.constraintlayout)
    implementation(libs.ndk.googletest)
    androidTestImplementation(libs.test.ext.junit)
    androidTestImplementation(libs.test.ext.junit.gtest)
    androidTestImplementation(libs.test.espresso.core)
    androidTestImplementation(project(":status-test-lib"))
}
