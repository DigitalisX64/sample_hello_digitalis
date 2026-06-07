# hello-qt

A minimal **Qt 6 (Widgets)** sample (ARM64-only) for the Digitalis sample
suite. It brings up a `QApplication` with a `QLabel` + `QPushButton` shown
full-screen.

## What it exercises

This sample drives the **Qt-on-Android native stack under Berberis ARM64→x86_64
translation** — the same path the **VulkanCapsViewer** Qt 6 prebuilt regression
target uses. Specifically, on startup the in-APK Qt shared libraries are loaded
and translated:

- `libQt6Core_arm64-v8a.so` — object/event system, the Qt event dispatcher.
- `libQt6Gui_arm64-v8a.so` — windowing, EGL/GL surface, the platform abstraction.
- `libQt6Widgets_arm64-v8a.so` — the widget tree (`QLabel`, `QPushButton`).
- **`libplugins_platforms_qtforandroid_arm64-v8a.so`** — the Qt **Android
  platform plugin** that bridges Qt's event loop and rendering to the Android
  `Activity` / `SurfaceView`. This plugin (loaded by the `QtActivity` Java
  loader at startup) is the crux: reaching a running, visible event loop means
  the entire Qt platform integration loaded and translated correctly under
  Berberis.

The launcher activity is Qt's `org.qtproject.qt.android.bindings.QtActivity`,
which `System.loadLibrary`s the Qt libs and then the app lib
`libhello_qt_arm64-v8a.so` (whose `main()` is in `src/main.cpp`).

## Why this module is STANDALONE (not in the suite Gradle build)

Qt 6.7's `androiddeployqt` generates a Gradle project pinned to **Gradle 8.3 /
Android Gradle Plugin 7.4.1**. The Digitalis sample suite runs **Gradle 9.1 /
AGP 9.0 / Kotlin 2.1** (see `../gradle/wrapper/gradle-wrapper.properties` and
`../gradle/libs.versions.toml`). These are incompatible, and — exactly like
`hello-reactnative` avoided wiring a heavy plugin into the shared
`settings.gradle.kts` — wiring Qt's plugin into the suite would endanger the
~40-module suite build.

So `hello-qt` is **not** added to `../settings.gradle.kts`; it is built on its
own with `qt-cmake` + `androiddeployqt`, which emit a self-contained Gradle
build under `build-android/`. The committed `android/settings.gradle` anchors
that generated build to its own output dir so its Gradle never escapes upward
and gets captured by the suite's `settings.gradle.kts` (which otherwise fails
with *"project directory … is not part of the build defined by settings file"*).

`hello-qt` is therefore **special-cased**: it is NOT registered in
`.claude/scripts/test-samples.sh` (that script builds the suite via
`./gradlew`, which does not and must not know about this module). Build it with
the command below; install/launch it manually for regression like any
`sample/prebuilts/` APK.

## Layout

```
hello-qt/
├── CMakeLists.txt            # qt_add_executable; arm64-only; package metadata
├── src/main.cpp              # QApplication + QLabel/QPushButton, showFullScreen
├── android/
│   ├── AndroidManifest.xml   # package=com.example.hellodigitalis.helloqt
│   ├── settings.gradle       # anchors the generated Gradle build (see above)
│   └── res/xml/qtprovider_paths.xml
├── build-apk.sh              # one-shot reproducible build (gitignored output)
└── hello-qt-debug.apk        # built artifact (gitignored by default; see below)
```

The Qt SDK itself lives **outside the repo** (multi-GB), installed via
`aqtinstall` into a directory of your choice. Point `$QT_ROOT` at it (the
per-version dir that contains `gcc_64` and `android_arm64_v8a`). Nothing from
the Qt install is committed.

## Build (reproducible)

### One-time: install Qt 6.7.3 (host tools + Android arm64) outside the repo

`aqtinstall` fetches Qt headlessly from `download.qt.io`. Pick an install
directory and export it (a venv is recommended if your system Python is too new
for `aqt`'s deps):

```sh
export QT_INSTALL_DIR=<your-qt-dir>      # any location outside the repo
python3 -m pip install aqtinstall        # or: pip install in a venv

# (a) HOST desktop Qt — provides qt-cmake / androiddeployqt / rcc / moc:
python3 -m aqt install-qt linux desktop 6.7.3 linux_gcc_64 --outputdir "$QT_INSTALL_DIR"

# (b) Qt for Android arm64_v8a — the target .so libs + platform plugin:
python3 -m aqt install-qt linux android 6.7.3 android_arm64_v8a --outputdir "$QT_INSTALL_DIR"

export QT_ROOT="$QT_INSTALL_DIR/6.7.3"   # used by build-apk.sh
```

This lands `$QT_ROOT/gcc_64` (host) and `$QT_ROOT/android_arm64_v8a` (target).
Qt 6.7.3 pairs with **NDK r26**; build-apk.sh picks the newest NDK under
`$ANDROID_SDK_ROOT/ndk` (override with `$ANDROID_NDK_ROOT`).

### Build the APK

```sh
cd sample/hellodigitalis/hello-qt
./build-apk.sh
```

`build-apk.sh` runs four steps (override any path via env vars at the top):

1. **Configure** with `qt-cmake -DQT_ANDROID_ABIS=arm64-v8a` (arm64 only).
2. **Build** the native module `libhello_qt_arm64-v8a.so`.
3. **Pin `sdkBuildToolsRevision` to 34.0.0** in the generated
   `android-…-deployment-settings.json`. (Qt 6.7's CMake helper leaves it empty
   unless it auto-detects, and auto-detect grabs the newest installed
   build-tools — a release-candidate here — so we pin a stable revision.)
4. **Package** with `androiddeployqt --android-platform android-34 --gradle`.

`android-34` + `build-tools 34.0.0` are deliberate: they are the newest the
Qt-generated **AGP 7.4.1** toolchain's bundled `aapt2` can parse. Compiling
against `android-35` trips the old `aapt2` with
*"RES_TABLE_TYPE_TYPE entry offsets overlap"* on `android-35/android.jar`.

Output APK (debug-signed with the Android debug key):

```
build-android/android-build/build/outputs/apk/debug/hello-qt-debug.apk
```

The script also copies it to `hello-qt/hello-qt-debug.apk` for convenience.

### Manual equivalent (what build-apk.sh automates)

```sh
# Required env (set to your install locations — no hardcoded paths):
#   ANDROID_SDK_ROOT (or ANDROID_HOME), JAVA_HOME, QT_ROOT (see above).
export ANDROID_NDK_ROOT="$(ls -d "$ANDROID_SDK_ROOT"/ndk/* | sort -V | tail -1)"

"$QT_ROOT/android_arm64_v8a/bin/qt-cmake" -S . -B build-android -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DQT_ANDROID_ABIS=arm64-v8a -DANDROID_ABI=arm64-v8a \
    -DQT_HOST_PATH="$QT_ROOT/gcc_64" \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT"
cmake --build build-android --target hello_qt
# patch sdkBuildToolsRevision -> 34.0.0 in build-android/android-*-deployment-settings.json
mkdir -p build-android/android-build/libs/arm64-v8a
cp build-android/libhello_qt_arm64-v8a.so build-android/android-build/libs/arm64-v8a/
"$QT_ROOT/gcc_64/bin/androiddeployqt" \
    --input build-android/android-hello_qt-deployment-settings.json \
    --output build-android/android-build --android-platform android-34 --gradle
```

## Verify the APK

```sh
AAPT2=$(ls "$ANDROID_SDK_ROOT"/build-tools/*/aapt2 | sort -V | tail -1)
"$AAPT2" dump badging hello-qt-debug.apk | grep -E 'package|native-code'
#   package: name='com.example.hellodigitalis.helloqt' ...
#   native-code: 'arm64-v8a'
unzip -l hello-qt-debug.apk | grep qtforandroid
#   lib/arm64-v8a/libplugins_platforms_qtforandroid_arm64-v8a.so
```

The APK contains **only** `lib/arm64-v8a/` native code (no non-arm64 libs):
the app lib plus `libQt6Core/Gui/Widgets`, the qtforandroid platform plugin,
and the image-format/style plugins.

## Install / run (manual)

`hello-qt` is not in `test-samples.sh`. Install and launch it by hand:

```sh
adb install -r hello-qt-debug.apk
adb shell am start -n \
    com.example.hellodigitalis.helloqt/org.qtproject.qt.android.bindings.QtActivity
```

A clean run shows the **hello-qt** label and a green
*"Qt platform plugin OK under Berberis"* line, with the button incrementing the
label on tap — confirming the Qt event loop is live under translation.

## Note on the committed APK

The suite's `../.gitignore` ignores `*.apk`, and this repo LFS-tracks only
reference screenshots (`../.gitattributes`), not APKs. The ~15 MB
`hello-qt-debug.apk` is therefore gitignored by default (see `.gitignore`).
Reproduce it with `./build-apk.sh`; if you want to stage it as a prebuilt-APK
regression target, drop it under `sample/prebuilts/` (the suite's drop-in spot)
rather than committing it here.
