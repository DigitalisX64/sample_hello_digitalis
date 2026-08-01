plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "com.example.hellodigitalis.bench"
    compileSdk = 35
    defaultConfig {
        minSdk = 24
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
}
