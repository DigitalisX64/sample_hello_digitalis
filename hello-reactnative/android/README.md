# hello-reactnative/android — New-Architecture render repro (WIP, standalone)

This is a **standalone** React Native 0.79 New-Architecture (Fabric) build of the
hello-reactnative sample, separate from the suite's flat `../` module. It exists
because making React Native *render* under Berberis needs the React Native Gradle
plugin (Fabric codegen + autolinking), and that plugin **does not compile under
the suite's Gradle 9.1 / Kotlin 2.2** (it targets Gradle 8.x). So this project
carries its **own** Gradle 8.14.1 wrapper + AGP 8.7.2 and is NOT part of the root
multi-module build (it is not in `../../settings.gradle.kts`).

## Build / run (needs `npm install` in `../` first)

    cd ..                      # sample/hellodigitalis/hello-reactnative
    npm install
    cd android
    ./gradlew :app:assembleDebug
    adb install -r -g app/build/outputs/apk/debug/app-debug.apk
    adb shell am start -n com.example.hellodigitalis.helloreactnative/com.example.helloreactnative.MainActivity

## FINDING (the reason this exists)

New-arch is genuinely enabled (generated `BuildConfig.IS_NEW_ARCHITECTURE_ENABLED
= true`, `PackageList` generated, Fabric/Yoga libs load). Under Berberis the app
builds, **runs the Hermes JS** (`ReactNativeJS: Running "hello-reactnative"`),
does **not crash and does not hang** (dispatch counts settle low) — yet **Fabric
mounts ZERO native views**: the view tree has only the empty root containers, the
screen is blank white. So React Native's **Fabric C++ rendering/mounting layer
(in libreactnative.so) mistranslates under Berberis** — a translator bug, and a
controllable reproduction (full JS source, no obfuscation).

This is DISTINCT from NetEase Cloud Music's blank, which is a `strtoumax` JIT hang
(see digitalis memory). It is, however, the likely class of bug behind blank RN
UIs in general (possibly Facebook). A screenshot test for this sample is blocked
until the Fabric-mount translation bug is fixed. Next: trace the Fabric mounting
path (`SurfaceMountingManager` / C++ `MountingManager` in libreactnative.so) under
`berberis.tracing`, the same way the strtoumax hang was localized.
