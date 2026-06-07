plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellolynx"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellolynx"
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
    implementation(libs.androidx.appcompat)

    // Lynx engine. The `lynx` artifact transitively pulls lynx-base, lynx-trace,
    // lynx-jssdk, primjs and primjsWasm (see its POM); they are listed explicitly
    // so the arm64-v8a native libraries are unambiguous. The bundle in
    // src/main/assets is built by the frontend/ ReactLynx project (types 3.7),
    // which the 3.8.x engine reads.
    implementation("org.lynxsdk.lynx:lynx:3.8.1")
    implementation("org.lynxsdk.lynx:lynx-jssdk:3.8.1")
    implementation("org.lynxsdk.lynx:lynx-trace:3.8.1")
    implementation("org.lynxsdk.lynx:primjs:3.8.0")

    androidTestImplementation(project(":screenshot-test-lib"))
}
