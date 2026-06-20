plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloleptonica"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloleptonica"
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
    // JavaCPP's plain jar and its android-arm64 classifier jar both ship a
    // META-INF/maven/org.bytedeco/<artifact>/pom.properties; let AGP pick the
    // first instead of failing the resource merge. Both javacpp (the runtime)
    // and leptonica (the binding) bundle one.
    packaging {
        resources {
            pickFirsts += "META-INF/maven/org.bytedeco/javacpp/pom.properties"
            excludes += "META-INF/native-image/**"
            pickFirsts += "META-INF/maven/org.bytedeco/leptonica/pom.properties"
        }
    }
}
dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // JavaCPP runtime — the native pointer / off-heap-memory layer every
    // Bytedeco binding builds on. The plain jar provides the Java API (Loader,
    // IntPointer, Pointer); the "android-arm64" CLASSIFIER jar bundles the
    // bionic-linked native at lib/arm64-v8a/libjnijavacpp.so.
    implementation("org.bytedeco:javacpp:1.5.13")
    implementation("org.bytedeco:javacpp:1.5.13:android-arm64")

    // Leptonica — the C image-processing library (Tesseract's substrate) wrapped
    // by Bytedeco. The plain jar provides the Java bindings (PIX, the leptonica
    // global functions); the "android-arm64" CLASSIFIER jar bundles the
    // bionic-linked native at lib/arm64-v8a/libjnileptonica.so plus libleptonica.
    // AGP merges those lib/<abi>/ paths straight into the APK's jniLibs, so no
    // extra jniLibs/sourceSets wiring is needed. The .so loads and runs under
    // Berberis ARM64->x86_64 translation.
    implementation("org.bytedeco:leptonica:1.87.0-1.5.13")
    implementation("org.bytedeco:leptonica:1.87.0-1.5.13:android-arm64")

    androidTestImplementation(project(":status-test-lib"))
}
