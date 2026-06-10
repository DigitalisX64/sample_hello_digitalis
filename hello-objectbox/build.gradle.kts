// ObjectBox specifics for this module (vs. the hello-mmkv template it mirrors):
//
//  * The ObjectBox annotation processor that generates `MyObjectBox` is a
//    classic JSR-269 processor (objectbox-processor ships
//    META-INF/services/javax.annotation.processing.Processor — no KSP). The
//    @Entity (Note) is therefore a plain Java class so it runs through the
//    standard javac `annotationProcessor` path. AGP 9.0's built-in Kotlin
//    support has no kapt, and a Kotlin @Entity would need kapt — keeping the
//    entity in Java sidesteps that entirely.
//
//  * The ObjectBox Gradle plugin is NOT on the Gradle Plugin Portal, so it
//    cannot be applied through the `plugins { id("io.objectbox") }` DSL. Per
//    ObjectBox's own docs it is pulled onto the buildscript classpath from
//    Maven Central and applied the legacy way. With no kotlin-kapt plugin
//    present it wires the processor through `annotationProcessor`.
buildscript {
    repositories {
        google()
        mavenCentral()
    }
    dependencies {
        classpath("io.objectbox:objectbox-gradle-plugin:5.4.1")
    }
}

plugins {
    alias(libs.plugins.android.application)
}

// Registers the ObjectBox annotation processor (generates MyObjectBox and the
// per-entity Cursor/Properties) and adds the native runtime dependency.
apply(plugin = "io.objectbox")

android {
    namespace = "com.example.hellodigitalis.helloobjectbox"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloobjectbox"
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

    // ObjectBox — an on-device NoSQL object database. The AAR ships
    // libobjectbox-jni.so (arm64-v8a), the native store that loads and runs
    // under Berberis ARM64->x86_64 translation. The io.objectbox plugin adds
    // the matching native runtime and the MyObjectBox-generating processor.
    implementation("io.objectbox:objectbox-android:5.4.1")

    androidTestImplementation(project(":status-test-lib"))
}
