plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellolibsignal"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellolibsignal"
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
        isCoreLibraryDesugaringEnabled = true
    }
    buildTypes {
        release { isMinifyEnabled = false }
        // libsignal-client ships Java record classes. AGP 9's per-dependency
        // dexing transform desugars them but has no global-synthetics consumer
        // (the enableGlobalSynthetics flag was removed). Routing the debug
        // build through R8 dexes the whole program together, which provides
        // that consumer. proguard-rules.pro disables shrink/obfuscate/optimize
        // so R8 is a pass-through dexer (nothing stripped) that still emits the
        // record global synthetic.
        debug {
            isMinifyEnabled = true
            isShrinkResources = false
            proguardFiles("proguard-rules.pro")
        }
    }
}
dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // Signal's libsignal — Signal Protocol cryptography. The Android AAR ships
    // libsignal_jni.so (arm64-v8a), a Rust implementation of curve25519/Ed25519
    // and other primitives that loads and runs under Berberis ARM64->x86_64
    // translation. The Java/Kotlin API arrives transitively via libsignal-client.
    implementation("org.signal:libsignal-android:0.86.5")

    coreLibraryDesugaring("com.android.tools:desugar_jdk_libs:2.1.4")

    androidTestImplementation(project(":status-test-lib"))
}
