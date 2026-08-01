plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloopenblas"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloopenblas"
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
    // JavaCPP and each Bytedeco binding ship a plain jar and an android-arm64
    // classifier jar, and each pair carries a
    // META-INF/maven/org.bytedeco/<artifact>/pom.properties; let AGP pick the
    // first of each instead of failing the resource merge.
    packaging {
        resources {
            pickFirsts += "META-INF/maven/org.bytedeco/javacpp/pom.properties"
            excludes += "META-INF/native-image/**"
            pickFirsts += "META-INF/maven/org.bytedeco/openblas/pom.properties"
        }
    }
}
dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // JavaCPP runtime — the native pointer / off-heap-memory layer that every
    // Bytedeco binding (OpenBLAS, OpenCV, FFmpeg, ...) builds on. The plain jar
    // provides the Java API (Loader, FloatPointer, Pointer), and the
    // "android-arm64" CLASSIFIER jar bundles the bionic-linked native at
    // lib/arm64-v8a/libjnijavacpp.so. AGP merges that lib/<abi>/ path straight
    // into the APK's jniLibs, so no extra jniLibs/sourceSets wiring is needed.
    implementation("org.bytedeco:javacpp:1.5.13")
    implementation("org.bytedeco:javacpp:1.5.13:android-arm64")

    // OpenBLAS — an optimized BLAS/LAPACK implementation. The plain jar provides
    // the org.bytedeco.openblas.global.openblas Java bindings (cblas_sgemm,
    // cblas_sdot, ...) and the "android-arm64" CLASSIFIER jar bundles the
    // arm64-v8a native libraries (libopenblas.so + libjniopenblas.so). AGP
    // merges their lib/<abi>/ paths straight into the APK's jniLibs. The .so
    // files load and run under Berberis ARM64->x86_64 translation; cblas_sgemm
    // drives OpenBLAS's single-precision floating-point matrix-multiply kernels
    // (heavy NEON/SIMD).
    implementation("org.bytedeco:openblas:0.3.31-1.5.13")
    implementation("org.bytedeco:openblas:0.3.31-1.5.13:android-arm64")

    implementation(project(":bench-lib"))

    androidTestImplementation(project(":status-test-lib"))
}
