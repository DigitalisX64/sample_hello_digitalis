pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
        // bilibili ijkplayer was published to the now-defunct JCenter; the
        // aliyun public mirror still serves its arm64-v8a AAR.
        maven { url = uri("https://maven.aliyun.com/repository/public") }
        // JitPack for libraries distributed only via GitHub releases.
        maven { url = uri("https://jitpack.io") }
    }
}

rootProject.name = "hello-digitalis"
include(":hello-vulkan")
include(":hello-jni")
include(":hello-jniCallback")
include(":exceptions")
include(":bitmap-plasma")
include(":hello-gl2")
include(":gles3jni")
include(":native-activity")
include(":native-audio")
include(":native-codec")
include(":native-midi")
include(":sensor-graph")
include(":camera-basic")
include(":camera-texture-view")
include(":teapots-classic")
include(":teapots-more")
include(":teapots-textured")
include(":endless-tunnel")
include(":sanitizers")
include(":unit-test")
include(":vectorization")
include(":orderfile")
include(":hello-gles1")
include(":hello-gles3")
include(":hello-msaa")
include(":hello-ijkplayer")
include(":hello-opencv")
include(":hello-sqlcipher")
include(":hello-conscrypt")
include(":hello-graphics-path")
include(":hello-lynx")
include(":hello-mmkv")
include(":hello-aaudio")
include(":hello-binder-ndk")
include(":hello-nnapi")
include(":hello-fp-vector")
include(":hello-neon")
include(":hello-sha-crypto")
include(":hello-ld-interleave")
include(":hello-superpack-regress")
include(":hello-barriers")
include(":hello-bf16")
include(":hello-bti")
include(":hello-complex")
include(":hello-dotprod")
include(":hello-fp16")
include(":hello-jscvt")
include(":hello-libc-libm")
include(":hello-lrcpc")
include(":hello-lse")
include(":hello-ldxp")
include(":hello-cntvct")
include(":hello-sigaction")
include(":hello-aes")
include(":hello-cronet")
include(":hello-pac-ret")
include(":hello-widemul")
include(":screenshot-test-lib")
include(":status-test-lib")
include(":hello-reactnative")
