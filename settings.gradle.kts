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
include(":hello-lrcpc")
include(":hello-lse")
include(":hello-pac-ret")
include(":screenshot-test-lib")
