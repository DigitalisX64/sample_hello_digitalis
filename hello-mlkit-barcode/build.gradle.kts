plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellomlkitbarcode"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellomlkitbarcode"
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

    // ML Kit Barcode Scanning, bundled-model variant (Google Maven). The
    // barcode-scanning-17.3.0 AAR ships jni/arm64-v8a/libbarhopper_v3.so — the
    // fully on-device "barhopper" barcode detector with its model baked into the
    // native library (no model download at runtime). The first scanner.process()
    // call loads that arm64-v8a .so and runs the heavy image-processing detector
    // under Berberis ARM64->x86_64 translation. Pulls in play-services-mlkit /
    // vision-common as transitive Google-Maven deps.
    implementation("com.google.mlkit:barcode-scanning:17.3.0")

    androidTestImplementation(project(":status-test-lib"))
}
