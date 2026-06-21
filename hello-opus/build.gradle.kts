plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloopus"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloopus"
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

    // Opus audio codec via techery/opus_android (libopus statically linked) — the
    // aar ships jni/arm64-v8a/libopustool.so. Encoding+decoding PCM exercises
    // Opus's SILK/CELT fixed-point MDCT + entropy loops under Berberis
    // ARM64->x86_64 translation (a standalone audio codec, not a player).
    // Pinned to commit 020c02094b (this fork's master is broken); the jitpack
    // repo it resolves through is configured in settings.gradle.kts. The native
    // OpusTool bridge (top.oply.opuslib) is file-based: encode() reads a WAV
    // file and writes an Opus file, decode() reverses it.
    implementation("com.github.techery:opus_android:020c02094b")

    androidTestImplementation(project(":status-test-lib"))
}
