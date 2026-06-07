# hello-lynx

A minimal **Lynx** sample (ARM64-only) for the Digitalis sample suite. It hosts
a single `LynxView` that renders a prebuilt ReactLynx page, exercising the
[Lynx](https://github.com/lynx-family) native UI engine and its **PrimJS**
JavaScript runtime under Berberis ARM64→x86_64 translation.

## What it exercises

On launch, `HelloLynxApplication` calls `LynxEnv.init`, which loads the arm64-v8a
Lynx native libraries (`liblynx.so`, `liblynxbase.so`, `libquick.so` = PrimJS,
`libwasm.so`, …) and starts the shared JS runtime. `MainActivity` builds a
`LynxView` and renders `assets/main.lynx.bundle`. Reaching a rendered page means
the Lynx engine parsed the bundle, PrimJS executed the ReactLynx JS, and the
native layout/paint pipeline ran — all translated by Berberis.

The page (`frontend/src/App.tsx`) is deliberately static (no animation, no remote
images) so the suite's `ScreenshotTest` can validate it deterministically.

## Layout

```
hello-lynx/
├── build.gradle.kts                 # Android module; pulls org.lynxsdk.lynx:lynx
├── src/main/assets/main.lynx.bundle # PREBUILT Lynx template (committed artifact)
├── src/main/java/.../HelloLynxApplication.kt  # LynxEnv.init
├── src/main/java/.../MainActivity.kt          # LynxView + renderTemplateUrl
├── src/main/java/.../AssetsTemplateProvider.kt# resolves the bundle from assets
├── src/androidTest/.../ScreenshotTest.kt
└── frontend/                        # the ReactLynx project that builds the bundle
    ├── package.json, lynx.config.ts
    └── src/{index,App}.tsx, App.css
```

## Native SDK / toolchain versions

- Native engine: `org.lynxsdk.lynx:lynx:3.8.1` (transitively pulls `lynx-base`,
  `lynx-trace`, `lynx-jssdk`, `primjs`, `primjsWasm`) from Maven Central.
- Bundle toolchain: `@lynx-js/react` + `@lynx-js/rspeedy` (`@lynx-js/types` 3.7);
  the 3.8.x engine reads the 3.7-targeted bundle.

## Rebuilding the bundle

The committed `src/main/assets/main.lynx.bundle` is the build artifact; the
`frontend/` `node_modules/` and `dist/` are gitignored. To regenerate it after
editing `frontend/src`:

```sh
cd frontend
npm install
npm run build           # emits dist/main.lynx.bundle
cp dist/main.lynx.bundle ../src/main/assets/main.lynx.bundle
```

Then rebuild the APK with `./gradlew :hello-lynx:assembleDebug`. After changing
the rendered output, refresh the reference image with
`.claude/scripts/test-samples.sh --update-references hello-lynx`.
