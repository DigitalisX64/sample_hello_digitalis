plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellogif"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellogif"
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

    // android-gif-drawable — its arm64-v8a libpl_droidsonroids_gif.so decodes
    // GIFs natively, exercised here under Berberis ARM64->x86_64 translation.
    implementation("pl.droidsonroids.gif:android-gif-drawable:1.2.31")

    androidTestImplementation(project(":status-test-lib"))
}
