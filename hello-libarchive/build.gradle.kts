plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellolibarchive"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellolibarchive"
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

    // libarchive via me.zhanghai libarchive — the aar ships
    // jni/arm64-v8a/libarchive-jni.so (libarchive statically linked). Reading an
    // archive exercises libarchive's format-detection state machines + entropy
    // decoders under Berberis ARM64->x86_64 translation.
    implementation("me.zhanghai.android.libarchive:library:1.1.6")

    androidTestImplementation(project(":status-test-lib"))
}
