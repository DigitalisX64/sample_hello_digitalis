// NOT in settings.gradle.kts — this module cannot build under the project's
// AGP 9.0 / built-in Kotlin toolchain. See NOTES.md for the full diagnosis.
// In short: io.realm.kotlin ships a Kotlin *compiler* plugin whose binary ABI
// must match the Kotlin compiler exactly. The latest (and final, since Realm
// Kotlin is sunset) release 3.0.0 targets an older Kotlin and crashes AGP 9's
// bundled compiler with NoSuchMethodError FirResolvedTypeRef.getType() from
// io.realm.kotlin.compiler. There is no Java-entity escape hatch (unlike a
// JSR-269 processor), and realm-java's Gradle plugin uses the Transform API
// that AGP 8+ removed. The config below is preserved as the furthest-reaching
// attempt; it configures and resolves but fails in compileDebugKotlin.
plugins {
    alias(libs.plugins.android.application)
    id("io.realm.kotlin") version "3.0.0"
}
android {
    namespace = "com.example.hellodigitalis.hellorealm"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellorealm"
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

    // Realm Kotlin — an on-device object database. library-base transitively
    // pulls cinterop-android, whose AAR ships jni/arm64-v8a/librealmc.so — the
    // native Realm Core store that runs under Berberis ARM64->x86_64 translation.
    implementation("io.realm.kotlin:library-base:2.3.0")

    androidTestImplementation(project(":status-test-lib"))
}
