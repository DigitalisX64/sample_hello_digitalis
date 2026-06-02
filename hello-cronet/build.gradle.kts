plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellocronet"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellocronet"
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
    // Cronet (Chromium net stack) shipped as an embedded native library. Its
    // ARM64 .so runs under Berberis translation, so an HTTPS request through it
    // exercises the same guest-side URL parsing / TLS path that fails inside
    // full apps (e.g. com.netease.cloudmusic) -- a small, fast reproduction.
    // cronet ships api/common/embedded as separate AARs that all declare the
    // org.chromium.net namespace; AGP 9 rejects a namespace shared across
    // multiple merged libraries. Take the public API classes as a plain JAR
    // (no Android manifest, so no namespace merge) and keep only cronet-embedded
    // as an AAR (it carries the native .so + impl classes).
    implementation(files("libs/cronet-api.jar"))
    implementation(files("libs/cronet-common.jar"))
    implementation("org.chromium.net:cronet-embedded:76.3809.111") {
        exclude(group = "org.chromium.net", module = "cronet-api")
        exclude(group = "org.chromium.net", module = "cronet-common")
    }
    androidTestImplementation(project(":status-test-lib"))
}
