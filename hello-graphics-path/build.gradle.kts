plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellographicspath"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellographicspath"
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

    // AndroidX graphics-path — its arm64-v8a libandroidx.graphics.path.so reads
    // a Path's native segment data (incl. conic->cubic conversion) and is
    // exercised here under Berberis ARM64->x86_64 translation.
    implementation("androidx.graphics:graphics-path:1.1.0")

    androidTestImplementation(project(":status-test-lib"))
}
