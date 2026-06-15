plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellojavacpp"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellojavacpp"
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
    // META-INF/maven/org.bytedeco/javacpp/pom.properties; let AGP pick the
    // first instead of failing the resource merge.
    packaging {
        resources {
            pickFirsts += "META-INF/maven/org.bytedeco/javacpp/pom.properties"
        }
    }
}
dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // JavaCPP runtime — the native pointer / off-heap-memory layer that every
    // Bytedeco binding (OpenCV, FFmpeg, etc.) builds on. Two Maven Central
    // artifacts are required: the plain jar provides the Java API (Loader,
    // BytePointer, IntPointer, Pointer), and the "android-arm64" CLASSIFIER jar
    // bundles the bionic-linked native at lib/arm64-v8a/libjnijavacpp.so. AGP
    // merges that lib/<abi>/ path straight into the APK's jniLibs, so no extra
    // jniLibs/sourceSets wiring is needed; only the pom.properties resource
    // collision (resolved above) has to be handled. The .so loads and runs
    // under Berberis ARM64->x86_64 translation.
    implementation("org.bytedeco:javacpp:1.5.13")
    implementation("org.bytedeco:javacpp:1.5.13:android-arm64")

    androidTestImplementation(project(":status-test-lib"))
}
