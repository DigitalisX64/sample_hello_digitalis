plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellohardwarebuffer"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellohardwarebuffer"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += "arm64-v8a" }
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_static"
                // minSdk 26 but the probe calls API 29/31 AHardwareBuffer entry
                // points; weak API defs let __builtin_available guard them at
                // runtime instead of raising the minSdk.
                arguments += "-DANDROID_WEAK_API_DEFS=ON"
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    buildTypes { release { isMinifyEnabled = false } }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" }
    }
}
dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)
    androidTestImplementation(project(":status-test-lib"))
}
