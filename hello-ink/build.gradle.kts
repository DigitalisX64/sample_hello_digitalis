plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloink"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloink"
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

    // Jetpack Ink — native (C++) stroke geometry & tessellation. The
    // ink-nativeloader transitive dependency ships the arm64-v8a libink.so,
    // which loads and runs under Berberis ARM64->x86_64 translation. Building a
    // Stroke tessellates its outline in native code; the probe self-checks the
    // computed PartitionedMesh bounding box / vertex counts.
    implementation("androidx.ink:ink-geometry:1.0.0")
    implementation("androidx.ink:ink-strokes:1.0.0")
    implementation("androidx.ink:ink-brush:1.0.0")
    implementation("androidx.ink:ink-authoring:1.0.0")

    androidTestImplementation(project(":status-test-lib"))
}
