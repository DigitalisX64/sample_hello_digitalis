plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloshadowhook"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloshadowhook"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        // -PnativeBaseline builds this module for the host ABI under a separate
        // application id so the same workload can be exercised natively. Without
        // the property nothing changes: same task names, same output paths.
        // (ShadowHook ships arm64/armv7 only — a native x86_64 baseline can't
        // link libshadowhook.so — but the switch is kept for suite uniformity.)
        val nativeBaseline = project.hasProperty("nativeBaseline")
        ndk { abiFilters += if (nativeBaseline) "x86_64" else "arm64-v8a" }
        if (nativeBaseline) applicationIdSuffix = ".native"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild {
            // libshadowhook.so is built with STL "none", but our companion
            // library uses the C++ standard library (std::string), so build the
            // hybrid with the shared STL.
            cmake { arguments += "-DANDROID_STL=c++_shared" }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    // Consume ShadowHook's prefab module (shadowhook.h + libshadowhook.so) from
    // its AAR so the companion native library can link shadowhook::shadowhook.
    buildFeatures { prefab = true }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" }
    }
    buildTypes { release { isMinifyEnabled = false } }
}
dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // ByteDance ShadowHook — a native inline / PLT hooking engine for Android
    // (the hooking core inside Shadow / bhook). The AAR ships prefab (module
    // "shadowhook": shadowhook.h + arm64-v8a/libshadowhook.so) plus the Java
    // facade com.bytedance.shadowhook.ShadowHook (init/getInitErrno). Its
    // native code rewrites ARM64 instructions at runtime to install
    // trampolines, so it runs — and patches — real translated code under
    // Berberis ARM64->x86_64 translation.
    implementation("com.bytedance.android:shadowhook:2.0.0")

    androidTestImplementation(project(":status-test-lib"))
}
