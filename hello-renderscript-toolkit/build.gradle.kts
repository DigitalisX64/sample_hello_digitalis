plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellorenderscripttoolkit"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellorenderscripttoolkit"
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

    // Google's RenderScript Intrinsics Replacement Toolkit — native (C++) image
    // ops (blur, colorMatrix, histogram, …) that use Neon/AdvSimd on Arm. It
    // builds an arm64-v8a librenderscript-toolkit.so, which loads and runs under
    // Berberis ARM64->x86_64 translation.
    //
    // Google does NOT publish this Toolkit to any Maven repository, so it is
    // vendored into the suite as the :renderscript-toolkit library module
    // (built from the upstream source) and consumed as a project dependency.
    implementation(project(":renderscript-toolkit"))

    androidTestImplementation(project(":status-test-lib"))
}
