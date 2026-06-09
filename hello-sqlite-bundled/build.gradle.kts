plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellosqlitebundled"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellosqlitebundled"
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

    // androidx.sqlite KMP API + the bundled driver. sqlite-bundled ships its OWN
    // arm64-v8a native SQLite (jni/arm64-v8a/libsqliteJni.so) rather than using
    // the platform's libsqlite, so BundledSQLiteDriver loads and runs that .so
    // under Berberis ARM64->x86_64 translation.
    implementation("androidx.sqlite:sqlite:2.5.1")
    implementation("androidx.sqlite:sqlite-bundled:2.5.1")

    androidTestImplementation(project(":status-test-lib"))
}
