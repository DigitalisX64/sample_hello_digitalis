plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellowcdb"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellowcdb"
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

    // Tencent WCDB (WeChat Database) — an encrypted SQLite engine. The AAR ships
    // jni/arm64-v8a/libwcdb.so, a bionic-linked Android native that statically
    // links SQLCipher (AES) on top of SQLite; SQLiteGlobal's static initializer
    // System.loadLibrary("wcdb")s it, so the first SQLiteDatabase touch loads and
    // runs it under Berberis ARM64->x86_64 translation. This is the WCDB 1.x API
    // generation (com.tencent.wcdb.database.SQLiteDatabase, modelled on Android's
    // android.database.sqlite.SQLiteDatabase), the newest published to Maven
    // Central; the 2.x ORM API is not on Maven Central.
    implementation("com.tencent.wcdb:wcdb-android:1.1-19")

    androidTestImplementation(project(":status-test-lib"))
}
