plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloxcrash"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloxcrash"
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

    // iQIYI xCrash — a native crash-capture library. The AAR ships
    // jni/arm64-v8a/libxcrash.so + libxcrash_dumper.so, bionic-linked Android
    // natives that load and run under Berberis ARM64->x86_64 translation.
    // XCrash.init() loads both .so files and installs native signal/ANR
    // handlers, exercising the real native path without triggering a crash.
    implementation("com.iqiyi.xcrash:xcrash-android-lib:3.1.0")

    androidTestImplementation(project(":status-test-lib"))
}
