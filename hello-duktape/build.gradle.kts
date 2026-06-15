plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloduktape"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloduktape"
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

    // Duktape (Square's Android bindings) — an embeddable JavaScript engine
    // written in C. The AAR ships jni/arm64-v8a/libduktape.so, a bionic-linked
    // Android native that loads and runs under Berberis ARM64->x86_64
    // translation. Duktape's bytecode interpreter is a computed-goto dispatch
    // loop full of pointer chasing, so driving JS through it exercises the
    // translator's interpreter/JIT paths broadly.
    implementation("com.squareup.duktape:duktape-android:1.4.0")

    androidTestImplementation(project(":status-test-lib"))
}
