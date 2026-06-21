plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellomupdf"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellomupdf"
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

    // MuPDF (Artifex) fitz — a native (C) PDF/rendering engine, AGPL-3.0. The aar
    // ships jni/arm64-v8a/libmupdf_java.so (bionic-linked). Rasterizing a PDF page
    // exercises a distinct font/vector renderer (different codebase from PDFium)
    // under Berberis ARM64->x86_64 translation.
    //
    // NOTE: fitz is hosted on https://maven.ghostscript.com (NOT Maven Central),
    // so that repository must be present in settings.gradle.kts'
    // dependencyResolutionManagement.repositories for this dependency to resolve.
    implementation("com.artifex.mupdf:fitz:1.24.10")

    androidTestImplementation(project(":status-test-lib"))
}
