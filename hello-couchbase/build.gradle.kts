plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellocouchbase"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellocouchbase"
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

    // Couchbase Lite for Android — an embedded NoSQL document database whose
    // storage engine is the native (C++) LiteCore library. The AAR ships
    // jni/arm64-v8a/libLiteCore.so (the LiteCore engine) and
    // jni/arm64-v8a/libLiteCoreJNI.so (its JNI bridge), both bionic-linked
    // Android natives that load and run under Berberis ARM64->x86_64
    // translation. Every Database/Collection CRUD call crosses the JNI bridge
    // into LiteCore, so a successful save/read round-trip drives the native
    // storage engine end-to-end.
    implementation("com.couchbase.lite:couchbase-lite-android:4.0.4")

    androidTestImplementation(project(":status-test-lib"))
}
