# vulkancapsviewer-test — Digitalis smoke test wrapper for Vulkan Caps Viewer

A custom test target that exercises Sascha Willems' upstream **Vulkan Caps
Viewer** (https://github.com/SaschaWillems/VulkanCapsViewer) under
Digitalis binary translation, without modifying any upstream source.

The upstream sits at `../vulkancapsviewer/`, pinned to tag **4.11**
(commit `65b603f`), brought in as a git submodule. Its own submodule
(`Vulkan-Headers` v1.4.340) is also pulled. See `../.gitmodules` and the
top-level `.repo/manifests/digitalis.xml` for the submodule wiring.

## Why this is a wrapper and not a normal Android Studio module

VulkanCapsViewer is a **Qt for Android** app. Building it requires a full
Qt 6 SDK + Qt for Android + the `androiddeployqt` tool — none of which
the rest of the `sample/hellodigitalis/` tree has access to. Building the
sources here would either need to mirror that whole toolchain into the
Digitalis build, or fork upstream to drop the Qt dependency. The user's
constraint is "do not modify upstream", so neither is acceptable.

Instead this wrapper:

1. Ships **no buildable source of its own**.
2. Defines test-side tasks that install a pre-built VkCapsViewer APK
   onto a running Digitalis emulator and check it launches without
   immediately crashing.
3. Documents the manual build path so anyone with a Qt SDK can produce
   the APK themselves from the submodule.

## Getting a prebuilt APK

Two options:

1. **Download from upstream's GitHub releases.** Each VkCapsViewer tag
   has a matching `vulkanCapsViewer-android.apk` artifact. For 4.11:
   https://github.com/SaschaWillems/VulkanCapsViewer/releases/tag/4.11
   Save it as `vulkancapsviewer-test/prebuilt/vulkancapsviewer-4.11.apk`.

2. **Build from the submodule with a Qt SDK.** From the repo root:

   ```bash
   # Requires Qt 6.x with Qt for Android, NDK, ANDROID_HOME / ANDROID_NDK_ROOT.
   cd sample/hellodigitalis/vulkancapsviewer
   mkdir build && cd build
   $QT_HOST_PATH/bin/qt-cmake -S .. -B . -DANDROID_PLATFORM=android-26
   cmake --build . --target apk
   # Copy the produced APK out:
   cp android-build/build/outputs/apk/release/android-build-release.apk \
      ../../vulkancapsviewer-test/prebuilt/vulkancapsviewer-4.11.apk
   ```

   Upstream's full build instructions live in
   `../vulkancapsviewer/README.md` and
   `../vulkancapsviewer/android/build.gradle`.

## Running the smoke test

With a Digitalis emulator booted and `prebuilt/vulkancapsviewer-4.11.apk`
in place:

```bash
./test.sh
```

The script:

1. Uninstalls any prior `de.saschawillems.vulkancapsviewer` install.
2. Installs the bundled APK.
3. Launches `de.saschawillems.vulkancapsviewer/.QtActivity`.
4. Watches `adb logcat` for **(a)** Berberis "Undefined arm64
   instruction" lines, **(b)** native tombstones / `FATAL EXCEPTION`s
   referencing the package, and **(c)** the process disappearing within
   the launch window.
5. Reports `PASS` if none of the above fire in the first 10 seconds,
   otherwise `FAIL` plus the offending log lines.

`PASS` means "VkCapsViewer reached its main activity under Digitalis
translation without an immediate crash" — a useful regression signal for
the FMUL `.4S` / FMLA / PAC fixes landed against
[DigitalisX64/platform_frameworks_libs_binary_translation#1](https://github.com/DigitalisX64/platform_frameworks_libs_binary_translation/issues/1).
It is **not** a functional test of the Vulkan enumeration itself.
