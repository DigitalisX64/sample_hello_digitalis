plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellogsl"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellogsl"
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
    // JavaCPP and the GSL binding each ship a plain jar and an android-arm64
    // classifier jar, and every one of those carries its own
    // META-INF/maven/<group>/<artifact>/pom.properties; let AGP pick the first
    // of each instead of failing the resource merge.
    packaging {
        resources {
            pickFirsts += "META-INF/maven/org.bytedeco/javacpp/pom.properties"
            excludes += "META-INF/native-image/**"
            pickFirsts += "META-INF/maven/org.bytedeco/gsl/pom.properties"
            pickFirsts += "META-INF/maven/org.bytedeco/openblas/pom.properties"
        }
    }
}
dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // JavaCPP runtime — the native pointer / off-heap-memory layer every
    // Bytedeco binding builds on. The plain jar provides the Java API (Loader,
    // Pointer); the "android-arm64" CLASSIFIER jar bundles the bionic-linked
    // lib/arm64-v8a/libjnijavacpp.so.
    implementation("org.bytedeco:javacpp:1.5.13")
    implementation("org.bytedeco:javacpp:1.5.13:android-arm64")

    // GNU Scientific Library (GSL 2.8) Bytedeco binding. The plain jar provides
    // the org.bytedeco.gsl.global.gsl Java API (the double-precision special
    // functions: gsl_sf_bessel_J0, gsl_sf_gamma, gsl_sf_erf, ...); the
    // "android-arm64" CLASSIFIER jar bundles the arm64-v8a natives
    // (lib/arm64-v8a/libgsl.so + libjnigsl.so). AGP merges lib/<abi>/ straight
    // into the APK's jniLibs, so no extra wiring is needed. The .so loads and
    // runs under Berberis ARM64->x86_64 translation, exercising GSL's
    // transcendental / special-function math.
    implementation("org.bytedeco:gsl:2.8-1.5.13")
    implementation("org.bytedeco:gsl:2.8-1.5.13:android-arm64")

    // libgsl.so / libjnigsl.so are linked against OpenBLAS (NEEDED libopenblas.so)
    // for their BLAS routines; the gsl classifier jar does not bundle it and
    // classifier jars are not transitive, so add the openblas android-arm64
    // native explicitly to deliver libopenblas.so into the APK's jniLibs.
    implementation("org.bytedeco:openblas:0.3.31-1.5.13")
    implementation("org.bytedeco:openblas:0.3.31-1.5.13:android-arm64")

    androidTestImplementation(project(":status-test-lib"))
}
