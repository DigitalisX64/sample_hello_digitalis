plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "com.example.hellodigitalis.screenshottest"
    compileSdk = 35
    defaultConfig {
        minSdk = 24
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
}

dependencies {
    api(libs.test.runner)
    api(libs.test.ext.junit)
    api(libs.test.uiautomator)
}
