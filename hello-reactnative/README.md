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

The suite's `StatusTest` asserts the app launches and runs **without crashing**
under translation (it is registered as a `StatusTest`, not a screenshot test).

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
New Architecture (Fabric + TurboModules), whose view rendering needs codegen via
the React Native Gradle plugin. To keep this module self-contained (no shared
`settings.gradle` plugin wiring), it runs on the **classic bridge** with the
bridgeless feature flag turned off. In that mode Hermes runs the JS and the full
native stack loads and translates, but the legacy Paper view renderer is largely
gone in 0.79, so the JS components do not mount to visible native views — the
screen stays blank. That does **not** affect what this sample is for: exercising
and regressing the React Native + Hermes **native translation** path (a crash
there fails the `StatusTest`). Making the UI render would require enabling the
New Architecture + the React Native Gradle plugin (codegen).
