plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellosqlcipher"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellosqlcipher"
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

    // SQLCipher — encrypted SQLite. The arm64-v8a libsqlcipher.so bundles SQLite
    // + the encryption codec (AES via its crypto provider), exercised under
    // Berberis ARM64->x86_64 translation.
    implementation("net.zetetic:sqlcipher-android:4.16.0")
    implementation("androidx.sqlite:sqlite:2.4.0")

    androidTestImplementation(project(":status-test-lib"))
}
