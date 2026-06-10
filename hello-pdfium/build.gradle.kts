plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellopdfium"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellopdfium"
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

    // PdfiumAndroid — a JNI wrapper around Google's PDFium C++ renderer. The AAR
    // ships libmodpdfium.so (arm64-v8a), which loads and runs under Berberis
    // ARM64->x86_64 translation. The probe opens a bundled PDF, reads page
    // metadata, and renders page 0 to a Bitmap through the native engine.
    // Exclude the legacy com.android.support transitive dep (it clashes with
    // AndroidX core); only the native .so + thin JNI wrapper are needed.
    implementation("com.github.barteksc:pdfium-android:1.9.0") {
        exclude(group = "com.android.support")
    }
    // barteksc's prebuilt bytecode references android.support.v4.util.ArrayMap.
    // AGP 9 dropped Jetifier (which used to rewrite that to AndroidX), so the
    // module ships a thin shim subclass (see src/main/java/android/support/...)
    // backed by androidx.collection.ArrayMap.
    implementation("androidx.collection:collection:1.4.5")

    androidTestImplementation(project(":status-test-lib"))
}
