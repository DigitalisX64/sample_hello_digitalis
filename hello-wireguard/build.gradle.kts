plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellowireguard"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellowireguard"
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
// KNOWN GAP (build toolchain, not a translator issue): the tunnel AAR contains a
// java.lang.Record (com.wireguard.android.backend.Statistics$PeerStats, present
// in every published version). Under the pinned AGP 9.0.0, the per-library
// no-classpath dexing transform cannot desugar records — it fails with
// "Attempt to create a global synthetic for 'Record desugaring' without a
// global-synthetics consumer" — and AGP 9.0.0 exposes no working knob to route
// around it (enableGlobalSynthetics / enableApiModeling no longer feed this
// transform, core-library desugaring does not add the consumer, and the legacy
// enableDexingArtifactTransform escape hatch was removed). The .so never reaches
// the translator. This module is therefore left out of the suite registration
// (settings.gradle.kts / test-samples.sh) until the toolchain can dex it; the
// sample source itself is correct and will build once that is resolved.
dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // WireGuard Android tunnel — the official WireGuard backend. The AAR (the
    // "@aar" classifier forces the native-bundling artifact) ships
    // jni/arm64-v8a/libwg-go.so: the wireguard-go userspace implementation
    // compiled as a bionic-linked arm64 native that embeds the full Go runtime
    // (goroutine scheduler, Go signal handling, cgo bridge). It loads and runs
    // under Berberis ARM64->x86_64 translation. GoBackend.wgVersion() is the
    // native entry point that returns the wireguard-go version string.
    implementation("com.wireguard.android:tunnel:1.0.20260102@aar")

    androidTestImplementation(project(":status-test-lib"))
}
