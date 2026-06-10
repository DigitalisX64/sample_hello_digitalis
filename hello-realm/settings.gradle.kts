// This settings file makes hello-realm a STANDALONE Gradle build, intentionally
// NOT wired into the suite's settings.gradle.kts (the hello-qt pattern).
// Realm Kotlin's io.realm.kotlin compiler plugin is ABI-locked to the Kotlin
// compiler it was built against (2.0.20 for the final 2.3.0 release); the
// suite's AGP 9.0 built-in Kotlin is newer and crashes the plugin with
// NoSuchMethodError FirResolvedTypeRef.getType(). Anchoring the build here
// stops Gradle from walking up into the suite and lets this module pin the
// AGP 8.7 / Kotlin 2.0.20 / Gradle 8.9 toolchain Realm supports.
// Build with ./build-apk.sh (or ./gradlew assembleDebug in this directory).
pluginManagement {
    repositories {
        gradlePluginPortal()
        google()
        mavenCentral()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "hello-realm"
