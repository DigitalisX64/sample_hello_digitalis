plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellolibwebp"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellolibwebp"
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

    // libwebp (Google) via aureusapps webp-android — the aar ships
    // jni/arm64-v8a/libwebp.so + a JNI bridge. Encoding/decoding WebP exercises
    // VP8/VP8L Huffman + predictor/color-transform decode loops under Berberis
    // ARM64->x86_64 translation (a codec path not otherwise in the suite).
    implementation("com.aureusapps.android:webp-android:1.1.2")

    androidTestImplementation(project(":status-test-lib"))
}
