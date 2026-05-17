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
include(":hello-ld-interleave")
include(":screenshot-test-lib")
