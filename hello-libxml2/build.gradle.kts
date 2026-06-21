/*
 * Copyright (C) 2026 utzcoz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellolibxml2"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellolibxml2"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += "arm64-v8a" }
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild {
            // The -ndk26-shared prefab modules are built against the shared STL,
            // so the consuming JNI bridge must use c++_shared as well.
            cmake { arguments += "-DANDROID_STL=c++_shared" }
        }
    }
    // libxml2 ships as a prefab AAR (prebuilt arm64-v8a libxml2.so + headers,
    // but no JNI wrapper), so the consumer adds its own CMake + C JNI bridge.
    buildFeatures {
        prefab = true
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

    // libxml2 (the GNOME XML C parser) as a prefab AAR. The AAR ships
    // prefab/modules/xml2/libs/android.arm64-v8a/libxml2.so (a bionic-linked
    // Android native: NEEDED libz/libm/libdl/libc, no libiconv at runtime)
    // plus headers under include/libxml2/libxml/*.h, but no JNI wrapper — so
    // this module adds a CMake + C JNI bridge (see src/main/cpp/). prefab's
    // find_package(libxml2 CONFIG) exposes the imported target libxml2::xml2,
    // whose INTERFACE_INCLUDE_DIRECTORIES make <libxml/parser.h> resolvable.
    // The xml2 prefab module export_libraries //libiconv:iconv, resolved by
    // the POM's transitive libiconv-ndk26-static dependency (a static prefab,
    // pulled in automatically by Gradle). The arm64-v8a libxml2.so loads and
    // runs under Berberis ARM64->x86_64 translation.
    implementation("com.viliussutkus89.ndk.thirdparty:libxml2-ndk26-shared:2.12.3-beta-2")

    androidTestImplementation(project(":status-test-lib"))
}
