plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellolua"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellolua"
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

    // luajava embeds the native Lua 5.4 VM. The classified android aar bundles
    // jni/arm64-v8a/liblua54.so (bionic-linked); running Lua bytecode exercises a
    // computed-goto / indirect-branch interpreter dispatch loop under Berberis
    // ARM64->x86_64 translation — distinct from the JS engine samples.
    //
    // Per luajava's Android instructions the Java API is split from the native
    // binaries across THREE artifacts: "luajava" is the core Lua interface /
    // AbstractLua, "lua54" supplies the concrete Lua54 bridging class, and the
    // "android" aar (lua54 classifier) ships ONLY the per-ABI liblua54.so
    // (its classes.jar is empty, packaging=pom), so it is pulled in @aar /
    // runtimeOnly alongside the two compile-time jars.
    implementation("party.iroiro.luajava:luajava:4.1.0")
    implementation("party.iroiro.luajava:lua54:4.1.0")
    runtimeOnly("party.iroiro.luajava:android:4.1.0:lua54@aar")

    androidTestImplementation(project(":status-test-lib"))
}
