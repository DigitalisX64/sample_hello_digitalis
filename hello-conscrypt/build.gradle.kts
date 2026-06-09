plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloconscrypt"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloconscrypt"
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

    // Conscrypt — Google's JCA security provider backed by native BoringSSL. The
    // arm64-v8a libconscrypt_jni.so runs SHA/AES-GCM under Berberis translation.
    implementation("org.conscrypt:conscrypt-android:2.5.2")

    androidTestImplementation(project(":status-test-lib"))
}
