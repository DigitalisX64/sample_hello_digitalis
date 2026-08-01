plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellofftw"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellofftw"
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
    // JavaCPP and the FFTW binding each ship their own
    // META-INF/maven/org.bytedeco/<artifact>/pom.properties in both the plain
    // and the android-arm64 classifier jar; let AGP pick the first instead of
    // failing the resource merge.
    packaging {
        resources {
            pickFirsts += "META-INF/maven/org.bytedeco/javacpp/pom.properties"
            excludes += "META-INF/native-image/**"
            pickFirsts += "META-INF/maven/org.bytedeco/fftw/pom.properties"
        }
    }
}
dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // FFTW (Fastest Fourier Transform in the West) via Bytedeco/JavaCPP — the
    // classic complex-FP DFT library. JavaCPP is the native pointer / off-heap
    // layer every Bytedeco binding builds on (Loader, DoublePointer); the fftw
    // binding adds the Java API (fftw3.fftw_plan_dft_1d, fftw_execute, ...).
    // Four Maven Central artifacts are required: each library's plain jar
    // provides the Java API, and its "android-arm64" CLASSIFIER jar bundles the
    // bionic-linked native (lib/arm64-v8a/libjnijavacpp.so and the FFTW
    // libfftw3.so + its JNI peer libjnifftw3.so). AGP merges those lib/<abi>/
    // paths straight into the APK's jniLibs, so no extra jniLibs/sourceSets
    // wiring is needed; only the pom.properties resource collisions (resolved
    // above) have to be handled. The .so files load and run under Berberis
    // ARM64->x86_64 translation, driving FFTW's complex-FP butterfly kernels.
    implementation("org.bytedeco:javacpp:1.5.13")
    implementation("org.bytedeco:javacpp:1.5.13:android-arm64")
    implementation("org.bytedeco:fftw:3.3.10-1.5.13")
    implementation("org.bytedeco:fftw:3.3.10-1.5.13:android-arm64")

    implementation(project(":bench-lib"))

    androidTestImplementation(project(":status-test-lib"))
}
