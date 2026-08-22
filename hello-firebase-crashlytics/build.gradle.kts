plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellofirebasecrashlytics"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellofirebasecrashlytics"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        // -PnativeBaseline builds this module for the host ABI under a separate
        // application id, so the same workload can be timed running natively as
        // the baseline the translated arm64 build is measured against. Without
        // the property nothing changes: same task names, same output paths.
        val nativeBaseline = project.hasProperty("nativeBaseline")
        ndk { abiFilters += if (nativeBaseline) "x86_64" else "arm64-v8a" }
        if (nativeBaseline) applicationIdSuffix = ".native"
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

    // Firebase Crashlytics NDK — the single most common native crash-reporting
    // SDK across real Android apps. firebase-crashlytics-ndk ships
    // jni/arm64-v8a/libcrashlytics.so plus the libcrashlytics-common /
    // -handler / -trampoline natives (Breakpad-based), all bionic-linked
    // Android natives that load and install their signal handler under
    // Berberis ARM64->x86_64 translation. The BoM aligns the versions and
    // Google's Maven ("google()" in settings.gradle.kts) serves them.
    implementation(platform("com.google.firebase:firebase-bom:33.7.0"))
    implementation("com.google.firebase:firebase-crashlytics")
    implementation("com.google.firebase:firebase-crashlytics-ndk")
    implementation("com.google.firebase:firebase-analytics")

    androidTestImplementation(project(":status-test-lib"))
}
