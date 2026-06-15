plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloj2v8"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloj2v8"
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

    // J2V8 — JNI bindings to Google's V8 JavaScript engine. The AAR ships
    // jni/arm64-v8a/libj2v8.so, a bionic-linked Android native that embeds a
    // full V8 (~32 MB) and runs under Berberis ARM64->x86_64 translation.
    // Executing JS drives V8's optimizing JIT: V8 generates ARM64 machine code
    // at runtime and signals self-modified code via IC IVAU, so this sample
    // exercises the translator's self-modifying-code / IC IVAU cache
    // invalidation path (re-translation of regenerated guest code).
    //
    // The "@aar" extension override is required: the Maven Central POM for
    // this artifact declares a malformed <packaging>aar.asc</packaging>, so
    // without it Gradle fetches the GPG signature file instead of the AAR and
    // the V8 classes never reach the compile classpath. "@aar" pins the
    // resolved artifact to the real .aar.
    implementation("com.eclipsesource.j2v8:j2v8:6.2.1@aar")

    androidTestImplementation(project(":status-test-lib"))
}
