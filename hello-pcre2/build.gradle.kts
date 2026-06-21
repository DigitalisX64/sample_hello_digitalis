plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellopcre2"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellopcre2"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += "arm64-v8a" }
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild {
            // The pcre2 prefab module is built against the shared STL, so the
            // consuming JNI bridge must use c++_shared too (AGP rejects mixing a
            // static STL with a shared-STL prefab dependency).
            cmake { arguments += "-DANDROID_STL=c++_shared" }
        }
    }
    // Consume the PCRE2 prefab AAR (prebuilt libpcre2-8.so + headers). Prefab
    // exposes the "pcre2" CMake package to find_package(pcre2 CONFIG); the JNI
    // bridge below links pcre2::pcre2-8.
    buildFeatures { prefab = true }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    buildTypes { release { isMinifyEnabled = false } }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" }
    }
}
dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // PCRE2 (Perl-Compatible Regular Expressions) with its sljit-based JIT.
    // The prefab AAR ships a prebuilt arm64-v8a libpcre2-8.so + headers (no JNI
    // wrapper), so a small CMake + C JNI bridge wires it up. PCRE2's JIT
    // generates ARM64 machine code at runtime and signals self-modified code
    // via IC IVAU, directly exercising the translator's SMC-invalidation path
    // under Berberis ARM64->x86_64 translation.
    implementation("com.viliussutkus89.ndk.thirdparty:pcre2-ndk26-shared:10.42-beta-4")

    androidTestImplementation(project(":status-test-lib"))
}
