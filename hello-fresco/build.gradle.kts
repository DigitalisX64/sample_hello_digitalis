plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellofresco"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellofresco"
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

    // Facebook/Meta Fresco — a native image-loading pipeline. The AARs ship
    // arm64-v8a native libs (libimagepipeline.so, libnative-imagetranscoder.so),
    // which load and run under Berberis ARM64->x86_64 translation. The probe
    // decodes a bundled JPEG asset through Fresco's native image pipeline.
    // Defensively exclude the legacy com.android.support group in case any
    // transitive dependency drags it in (it would collide with androidx).
    implementation("com.facebook.fresco:fresco:3.6.0") {
        exclude(group = "com.android.support")
    }
    // CloseableImage's supertype HasExtraData lives in fresco:middleware, which
    // the main fresco artifact pulls only at runtime — name it explicitly so it
    // is on the compile classpath.
    implementation("com.facebook.fresco:middleware:3.6.0")

    androidTestImplementation(project(":status-test-lib"))
}
