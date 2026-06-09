plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellozxing"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellozxing"
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

    // zxing-cpp — a native (C++) barcode decoder. The AAR ships
    // libzxingcpp_android.so (arm64-v8a), which loads and runs under Berberis
    // ARM64->x86_64 translation. zxingcpp.BarcodeReader.read(Bitmap) calls into
    // the native decoder.
    implementation("io.github.zxing-cpp:android:3.0.2")

    // com.google.zxing:core — pure-Java QR *encoder*. Used only to synthesize the
    // test bitmap (the zxing-cpp 3.0.2 artifact ships a reader but no writer);
    // the native zxing-cpp decode is what this sample actually exercises.
    implementation("com.google.zxing:core:3.5.4")

    androidTestImplementation(project(":status-test-lib"))
}
