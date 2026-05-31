plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.sanitizers"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.sanitizers"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += "arm64-v8a" }
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }
    buildTypes {
        debug {
            matchingFallbacks += listOf("debug")
        }
        release { isMinifyEnabled = false }
        create("hwasan") {
            initWith(getByName("debug"))
            isDebuggable = true
            packaging {
                jniLibs {
                    useLegacyPackaging = true
                }
            }
            externalNativeBuild {
                cmake {
                    arguments += listOf("-DANDROID_STL=c++_shared", "-DSANITIZE=hwasan")
                }
            }
        }
        create("asan") {
            initWith(getByName("debug"))
            isDebuggable = true
            packaging {
                jniLibs {
                    useLegacyPackaging = true
                }
            }
            externalNativeBuild {
                cmake {
                    arguments += listOf("-DANDROID_ARM_MODE=arm", "-DANDROID_STL=c++_shared", "-DSANITIZE=asan")
                }
            }
        }
        create("ubsan") {
            initWith(getByName("debug"))
            externalNativeBuild {
                cmake {
                    arguments += "-DSANITIZE=ubsan"
                }
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" }
    }
    buildFeatures {
        viewBinding = true
    }
}
dependencies {
    implementation(libs.androidx.appcompat)
    implementation(libs.material)
    implementation(libs.androidx.constraintlayout)
    androidTestImplementation(project(":status-test-lib"))
}
