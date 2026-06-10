plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellotesseract"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellotesseract"
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

    // Tesseract4Android — JNI bindings around the native (C/C++) Tesseract OCR
    // engine and its Leptonica image library. The AAR ships
    // jni/arm64-v8a/{libtesseract,libleptonica,libjpeg,libpngx}.so, bionic-linked
    // arm64 natives that load and run under Berberis ARM64->x86_64 translation.
    // Published only via JitPack (the upstream repo has no Maven Central release);
    // the JitPack repo is already declared in settings.gradle.kts. The
    // "tesseract4android" variant is single-threaded; "-openmp" adds OpenMP.
    implementation("com.github.adaptech-cz.Tesseract4Android:tesseract4android:4.7.0")

    androidTestImplementation(project(":status-test-lib"))
}
