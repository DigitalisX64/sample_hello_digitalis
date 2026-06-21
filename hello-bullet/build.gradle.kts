plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellobullet"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellobullet"
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

    // libGDX Bullet bindings — the native rigid-body physics engine (Bullet, C++).
    // The arm64-v8a natives (libgdx.so, libgdx-bullet.so) are vendored under
    // src/main/jniLibs/arm64-v8a (the gdx platform jars place the .so at the jar
    // root, so they can't auto-extract from an aar). Stepping a dynamics world runs
    // Bullet's collision + constraint-solver FP/SIMD math under Berberis translation.
    implementation("com.badlogicgames.gdx:gdx:1.13.1")
    implementation("com.badlogicgames.gdx:gdx-bullet:1.13.1")

    androidTestImplementation(project(":status-test-lib"))
}
