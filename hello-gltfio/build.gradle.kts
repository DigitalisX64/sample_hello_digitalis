plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellogltfio"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellogltfio"
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

    // Google Filament — a native (C++) physically-based real-time renderer.
    // Both AARs ship jni/arm64-v8a/*.so (libfilament-jni.so + libgltfio-jni.so),
    // bionic-linked Android natives that load and run under Berberis
    // ARM64->x86_64 translation. gltfio-android depends on filament-android, but
    // both are listed explicitly so the Engine/EntityManager API and the
    // AssetLoader/ResourceLoader glTF parser are both on the compile classpath.
    implementation("com.google.android.filament:filament-android:1.72.0")
    implementation("com.google.android.filament:gltfio-android:1.72.0")

    androidTestImplementation(project(":status-test-lib"))
}
