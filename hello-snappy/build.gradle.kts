plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellosnappy"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellosnappy"
        // snappy-java 1.1.10.x dexes only at API 26+ (modern class-file level).
        minSdk = 26
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

// snappy-java is a plain jar that self-extracts its native by inspecting os.arch.
// Under Berberis native bridge the managed runtime is the host (os.arch=x86_64)
// while native code must be the arm64 guest, so that self-selection fails. We
// instead deliver the library's arm64 (android-aarch64) libsnappyjava.so the
// standard Android way — through the APK's jniLibs/arm64-v8a — extracted from the
// dependency jar at build time (no committed binaries), plus the arm64
// libc++_shared.so the native NEEDS (the platform does not provide it to apps),
// pulled from the in-tree NDK libc++. MainActivity then sets
// org.xerial.snappy.use.systemlib=true so snappy loads it via System.loadLibrary,
// which the guest linker resolves and Berberis translates.
val snappyNative: Configuration by configurations.creating
val snappyJniLibs = layout.buildDirectory.dir("snappyJniLibs")
val extractSnappyArm64 by tasks.registering(Copy::class) {
    val ndkCxx = fileTree("${rootDir}/../..") {
        include("prebuilts/clang/host/linux-x86/*/android_libc++/ndk/aarch64/lib/libc++_shared.so")
    }.files.minByOrNull { it.path }   // deterministic pick across clang-rNNN dirs
    from(zipTree(snappyNative.singleFile)) {
        include("**/Linux/android-aarch64/libsnappyjava.so")
        eachFile { path = "arm64-v8a/libsnappyjava.so" }
    }
    if (ndkCxx != null) from(ndkCxx) { into("arm64-v8a") }
    includeEmptyDirs = false
    into(snappyJniLibs)
}
android.sourceSets.getByName("main").jniLibs.srcDir(snappyJniLibs.get().asFile)
tasks.named("preBuild") { dependsOn(extractSnappyArm64) }

dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // Snappy (Google's Snappy compression) via snappy-java's JNI bindings — a
    // native (C++) library. The jar provides the Java API; its arm64 native is
    // delivered via jniLibs (see the extract task above).
    implementation("org.xerial.snappy:snappy-java:1.1.10.8")
    snappyNative("org.xerial.snappy:snappy-java:1.1.10.8")

    androidTestImplementation(project(":status-test-lib"))
}
