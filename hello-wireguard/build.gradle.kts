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
dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // WireGuard Android tunnel — the official WireGuard backend. We depend on a
    // *trimmed* copy of its classes (libs/wireguard-tunnel-*-norecord.jar) plus
    // its native lib packaged directly as a jniLib
    // (src/main/jniLibs/arm64-v8a/libwg-go.so): the wireguard-go userspace
    // implementation, a bionic-linked arm64 native embedding the full Go runtime
    // (goroutine scheduler, Go signal handling, cgo bridge) that loads and runs
    // under Berberis ARM64->x86_64 translation. GoBackend.wgVersion() is the
    // native entry point the sample calls (reflectively) to return the
    // wireguard-go version string.
    //
    // Why trimmed: the upstream AAR ships one java.lang.Record
    // (com.wireguard.android.backend.Statistics$PeerStats). D8 only desugars
    // records with a global-synthetics consumer, which no AGP available here
    // (8.2.2 / 8.7.3 / 9.0.0) enables for library dexing — every variant fails
    // with "Attempt to create a global synthetic for 'Record desugaring'
    // without a global-synthetics consumer." The sample never touches
    // Statistics, so vendor-tunnel.sh drops that record (and its sole user) from
    // the classes; with no record present the dependency dexes cleanly.
    // Regenerate the two vendored artifacts with ./vendor-tunnel.sh.
    implementation(files("libs/wireguard-tunnel-1.0.20260102-norecord.jar"))

    androidTestImplementation(project(":status-test-lib"))
}
