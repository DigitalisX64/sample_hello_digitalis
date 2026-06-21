plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.helloleveldb"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.helloleveldb"
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

    // LevelDB (Google) via kotlin-leveldb — the android aar ships
    // jni/arm64-v8a/libleveldb.so (+ libc++_shared.so). Put/get/delete exercises
    // LevelDB's log-structured-merge engine: skiplist memtable, SST writes, CRC32C
    // and varint coding under Berberis ARM64->x86_64 translation (an LSM store,
    // distinct from the suite's SQL/B-tree/ORM DBs).
    implementation("com.github.lamba92:kotlin-leveldb-android:1.0.2")

    androidTestImplementation(project(":status-test-lib"))
}
