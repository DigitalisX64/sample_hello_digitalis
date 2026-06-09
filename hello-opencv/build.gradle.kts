plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloopencv"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloopencv"
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

    // OpenCV — its arm64-v8a libopencv_java4.so runs heavily NEON-optimized
    // image-processing kernels under Berberis ARM64->x86_64 translation.
    implementation("org.opencv:opencv:4.12.0")

    androidTestImplementation(project(":status-test-lib"))
}
