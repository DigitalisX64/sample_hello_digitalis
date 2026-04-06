plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.example.hellodigitalis"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.example.hellodigitalis"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"

        // ARM64 ONLY — this app will not run on x86_64 without a binary translator
        ndk {
            abiFilters += "arm64-v8a"
        }

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_static"
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
dependencies {
    androidTestImplementation(project(":screenshot-test-lib"))
}
