plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloyoga"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloyoga"
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

    // Facebook Yoga — the native (C/C++) flexbox layout engine (libyoga.so).
    // The release-variant AAR ships jni/arm64-v8a/libyoga.so, a bionic-linked
    // Android native loaded and run under Berberis ARM64->x86_64 translation.
    // Yoga pulls SoLoader transitively, which locates and loads the .so; the
    // Java YogaNode API drives the native layout solver via JNI.
    implementation("com.facebook.yoga:yoga:3.2.1")
    // Yoga loads libyoga.so via SoLoader; yoga declares it only as an
    // optional runtime dep, so pin it explicitly on the compile classpath.
    implementation("com.facebook.soloader:soloader:0.10.5")

    implementation(project(":bench-lib"))
    androidTestImplementation(project(":status-test-lib"))
}
