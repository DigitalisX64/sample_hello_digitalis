plugins {
    alias(libs.plugins.android.application)
}
android {
    namespace = "com.example.hellodigitalis.hellobox2d"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.example.hellodigitalis.hellobox2d"
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
    // The two libGDX classifier jars below pack their .so FLAT at the jar root
    // (libgdx.so / libgdx-box2d.so), NOT under jni/arm64-v8a/, so AGP does not
    // auto-merge them into the APK. The extractGdxNatives task (defined after
    // this block) unpacks them into build/jniLibs/arm64-v8a/; wire that dir as
    // an extra jniLibs source set so AGP packages the .so into lib/arm64-v8a/.
    sourceSets["main"].jniLibs.srcDir(layout.buildDirectory.dir("jniLibs").get().asFile)
}

// A separate configuration holds the natives-classifier jars so they reach the
// extraction task WITHOUT landing on the compile/runtime classpath (their flat
// .so payload must not leak into mergeJavaRes).
val nativesArm64: Configuration by configurations.creating

dependencies {
    implementation(libs.material)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)

    // libGDX core + its Box2D module. gdx-box2d is the Java API over the native
    // Box2D physics engine (collision detection + constraint solver); the engine
    // is bionic-linked arm64 native code that loads and runs under Berberis
    // ARM64->x86_64 translation. gdx-box2d transitively pulls in gdx, which pulls
    // in gdx-jnigen-loader's SharedLibraryLoader (used by Box2D.init()).
    implementation("com.badlogicgames.gdx:gdx:1.14.2")
    implementation("com.badlogicgames.gdx:gdx-box2d:1.14.2")

    // The arm64-v8a natives. These classifier jars carry the bionic-linked
    // libgdx.so (gdx-platform: SharedLibraryLoader/Vector2 JNI peers, and the
    // DT_NEEDED of libgdx-box2d.so) and libgdx-box2d.so (the Box2D engine). On
    // Android, SharedLibraryLoader.load("gdx-box2d") maps to a bare
    // System.loadLibrary("gdx-box2d"), which resolves libgdx-box2d.so from the
    // APK's lib/arm64-v8a/ — so BOTH .so must be present there (libgdx.so to
    // satisfy libgdx-box2d.so's NEEDED entry). extractGdxNatives copies them in.
    nativesArm64("com.badlogicgames.gdx:gdx-platform:1.14.2:natives-arm64-v8a")
    nativesArm64("com.badlogicgames.gdx:gdx-box2d-platform:1.14.2:natives-arm64-v8a")

    androidTestImplementation(project(":status-test-lib"))
}

// Unpack every *.so from the flat natives jars into build/jniLibs/arm64-v8a/,
// where the jniLibs.srcDir wired above hands them to AGP for APK packaging.
val extractGdxNatives by tasks.registering(Copy::class) {
    from(nativesArm64.elements.map { jars -> jars.map { zipTree(it) } }) {
        include("*.so")
    }
    into(layout.buildDirectory.dir("jniLibs/arm64-v8a"))
}

tasks.named("preBuild").configure { dependsOn(extractGdxNatives) }
