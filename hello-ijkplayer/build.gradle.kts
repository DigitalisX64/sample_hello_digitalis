plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloijkplayer"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloijkplayer"
        // SurfaceTexture(singleBufferMode) detached constructor is API 26+.
        minSdk = 26
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

    // bilibili ijkplayer — an FFmpeg-based media player. The arm64-v8a AAR ships
    // libijkffmpeg.so / libijksdl.so / libijkplayer.so, which decode media under
    // Berberis ARM64->x86_64 translation (heavy FFmpeg/NEON exercise).
    implementation("tv.danmaku.ijk.media:ijkplayer-java:0.8.8")
    implementation("tv.danmaku.ijk.media:ijkplayer-arm64:0.8.8")

    androidTestImplementation(project(":status-test-lib"))
}
