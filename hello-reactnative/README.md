# hello-reactnative

A minimal **React Native + Hermes** sample (ARM64-only) for the Digitalis sample
suite. NetEase Cloud Music's login screen runs on React Native / Hermes under
Berberis translation, so this sample exercises that native stack end-to-end:
`libhermes.so`, `libreactnative.so` (merged megalib), `libjsi`, `libfbjni`,
`libfabricjni`, `libuimanagerjni`, SoLoader's merged-SO loading, and Hermes
bytecode execution. On startup the JS (`index.js`) drives the number/string
parsing surface — `parseInt` / `Number` / `JSON.parse` / `RegExp` — which Hermes
routes through bionic libc (`strtoull` / `strtoumax` / `strtod`), the path a
NetEase worker thread was observed hanging on.

The suite's `ScreenshotTest` launches the app and pixel-compares the rendered
screen against a committed reference (`src/androidTest/assets/reference/
screenshot_default.png`) — the title, the deterministic parse result
(`acc=13142540… matches=4 json=123`), and the green **"RN+Hermes parsing OK"**
line. This guards both the native translation path and correct rendering.

## Build

The APK is built by Gradle like every other sample
(`./gradlew :hello-reactnative:assembleDebug`). It depends only on the
`com.facebook.react:react-android` / `hermes-android` Maven artifacts plus the
**pre-bundled** Hermes bytecode committed at
`src/main/assets/index.android.bundle` — so **no Node/Metro step is needed to
build the APK**, and `node_modules/` is git-ignored.

To re-bundle after editing `index.js` (needs `npm install` first):

```sh
npm install
node node_modules/@react-native-community/cli/build/bin.js bundle \
    --platform android --dev false --entry-file index.js \
    --bundle-output src/main/assets/index.android.bundle --assets-dest src/main/res
# then compile JS -> Hermes bytecode in place:
node_modules/react-native/sdks/hermesc/linux64-bin/hermesc \
    -emit-binary -out src/main/assets/index.android.bundle.hbc \
    src/main/assets/index.android.bundle
mv src/main/assets/index.android.bundle.hbc src/main/assets/index.android.bundle
```

## Architecture note

React Native 0.79 (the only series published to Maven Central) defaults to the
New Architecture (Fabric + TurboModules). To keep this module self-contained (no
shared `settings.gradle` plugin wiring), it runs on the **classic bridge** with
the bridgeless feature flag turned off, depending only on the Maven AARs plus the
pre-bundled Hermes bytecode. Hermes runs the JS and the full native stack loads
and translates, and the UI renders to visible native views.

This sample previously rendered blank under Berberis, which was misattributed to
an RN-0.79 classic-bridge limitation. The real cause was a translator bug: the
interpreter's FCSEL handler corrupted `fcsel Dd, Dn, Dm` when `rd == rn`, which
made Hermes `parseInt` / `Number` (and bionic `strtod`) return 0, collapsing the
layout to a blank screen. With that fixed in the binary translator, the screen
renders correctly — the sample is the spec; the translator caught up to it.
