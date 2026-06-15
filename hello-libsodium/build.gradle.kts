plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellolibsodium"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellolibsodium"
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

    // libsodium via Lazysodium-Android — a native (C) crypto library driven
    // through JNA. The "@aar" classifier on lazysodium-android ships
    // jni/arm64-v8a/libsodium.so (the actual NaCl/libsodium native), and the
    // "@aar" on JNA ships jni/arm64-v8a/libjnidispatch.so (JNA's foreign
    // function-call dispatch native). Both are bionic-linked Android arm64
    // .so files that load and run under Berberis ARM64->x86_64 translation;
    // every crypto call is JNA-marshalled into native libsodium code. The
    // "@aar" form is required because @aar deps skip POM transitives, so JNA
    // (and core-ktx, used by SodiumAndroid) are declared explicitly.
    implementation("com.goterl:lazysodium-android:5.2.0@aar")
    implementation("net.java.dev.jna:jna:5.17.0@aar")
    implementation("androidx.core:core-ktx:1.16.0")

    androidTestImplementation(project(":status-test-lib"))
}
