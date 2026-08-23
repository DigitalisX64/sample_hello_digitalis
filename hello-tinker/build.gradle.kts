plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellotinker"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellotinker"
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

    // Tencent Tinker (com.tencent.tinker) — the Android hot-fix framework used by
    // real apps (e.g. NetEase Cloud Music) to ship dex/so/resource patches without
    // a reinstall. This probe drives Tinker's runtime binary-patch primitive,
    // BSDiff/BSPatch (com.tencent.tinker.bsdiff), which is exactly the code path an
    // app runs when applying a downloaded .so or resource patch: diff old->new,
    // then reconstruct new from old+diff and self-check the round-trip.
    //
    // NOTE on the coordinate: BSDiff/BSPatch live in the `bsdiff-util` artifact.
    // The app-facing umbrella `com.tencent.tinker:tinker-android-lib` reaches the
    // same classes transitively (tinker-android-lib -> tinker-commons ->
    // bsdiff-util), but it also merges ~60 hot-plug stub activities and :patch
    // services into the manifest that are irrelevant to this smoke test, so we
    // depend directly on the primitive's own artifact. Tinker's runtime patch
    // code is pure Java (no jni/arm64-v8a/*.so is published by ANY Tinker Maven
    // artifact — the native C bsdiff/dexdiff exists only in the build-time patch
    // tooling), so this exercises Tinker's real patch algorithm running in the
    // Digitalis environment rather than driving Berberis translation directly.
    implementation("com.tencent.tinker:bsdiff-util:1.9.15")

    androidTestImplementation(project(":status-test-lib"))
}
