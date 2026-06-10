plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloappsearch"
    // appsearch 1.1.0 requires compiling against API 36 or later.
    compileSdk = 36
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloappsearch"
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

    // AndroidX AppSearch — an on-device structured search engine. The
    // appsearch-local-storage AAR ships the arm64-v8a native Icing engine
    // (jni/arm64-v8a/libicing.so), which loads and runs under Berberis
    // ARM64->x86_64 translation. This sample uses the schema-less
    // GenericDocument API, so no annotation processor (kapt) is needed: the
    // schema, document, and query are built directly against the runtime.
    implementation("androidx.appsearch:appsearch:1.1.0")
    implementation("androidx.appsearch:appsearch-local-storage:1.1.0")

    androidTestImplementation(project(":status-test-lib"))
}
