plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellolibtorrent4j"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellolibtorrent4j"
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

    // libtorrent4j — Java/JNI bindings over libtorrent (with statically linked
    // Boost.Asio and OpenSSL). The android-arm64 artifact is a plain jar that
    // bundles lib/arm64-v8a/libtorrent4j.so (~15 MB of native C++); AGP's
    // native-lib merge extracts it into the APK's lib/arm64-v8a/, where the
    // swig static initializer's System.loadLibrary("torrent4j") finds it and
    // runs it under Berberis ARM64->x86_64 translation. It transitively pulls
    // the pure-Java core jar (LibTorrent, SessionManager, Sha1Hash). The plain
    // libtorrent4j jar ships no Android native, so the android-arm64 artifact
    // is required.
    implementation("org.libtorrent4j:libtorrent4j-android-arm64:2.1.0-39")

    androidTestImplementation(project(":status-test-lib"))
}
