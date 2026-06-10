plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloncnn"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloncnn"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += "arm64-v8a" }
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild {
            cmake {
                // ncnn's Android package ships a static libncnn.a built against
                // c++_shared, so the app must bundle the shared STL too.
                arguments += "-DANDROID_STL=c++_shared"
                // Point find_package(ncnn CONFIG) at the vendored arm64-v8a CMake
                // package. ncnn has no Maven/prefab coordinate (see NOTES.md);
                // the package is unzipped under src/main/cpp/ncnn-prefab/. This
                // module is arm64-v8a-only, so the ABI path is fixed.
                arguments += "-Dncnn_DIR=${file("src/main/cpp/ncnn-prefab/arm64-v8a/lib/cmake/ncnn").absolutePath}"
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
