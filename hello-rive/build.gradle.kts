plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellorive"
    // Rive 11.6.1 pulls androidx.core:1.17.0, which requires compileSdk 36.
    compileSdk = 36
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellorive"
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

    // Rive — a real-time vector-animation runtime. The AAR ships
    // jni/arm64-v8a/librive-android.so, a bionic-linked arm64 native (the C++
    // Rive runtime) that loads and runs under Berberis ARM64->x86_64
    // translation. Fully on-device, no Google Play Services. The sample parses a
    // bundled .riv document natively (no GL surface) and inspects the resulting
    // artboard/animation model, exercising the native binary-format parser and
    // document builder. Rive's transitive AndroidX startup/coroutines deps come
    // from the AAR's POM.
    implementation("app.rive:rive-android:11.6.1")

    androidTestImplementation(project(":status-test-lib"))
}
