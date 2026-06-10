# hello-realm — standalone build (the hello-qt pattern)

This module exercises Realm Kotlin's native store (`librealmc.so`, Realm Core
C++) under Berberis ARM64→x86_64 translation. It builds and runs — but **not
inside the suite's Gradle build**. Like `hello-qt`, it is a standalone Gradle
project anchored by its own `settings.gradle.kts`, intentionally left out of
the suite's `settings.gradle.kts` and `test-samples.sh`.

## Why it can't build in the suite

Realm Kotlin works via a Kotlin **compiler plugin** (`io.realm.kotlin`) that
generates schema glue for each `RealmObject` during compilation. A compiler
plugin links against Kotlin compiler internals, so its ABI must match the
compiler exactly.

- The suite builds with **AGP 9.0** and its **built-in Kotlin** (2.1+). AGP 9
  puts Kotlin on the classpath itself, so an external pinned
  `org.jetbrains.kotlin.android` is rejected ("plugin is already on the
  classpath with an unknown version").
- Realm Kotlin's latest **and final** release is **3.0.0 / 2.3.0** (MongoDB
  sunset the project); its compiler plugin targets Kotlin ≤2.0.x. Loaded into
  AGP 9's compiler it crashes `compileDebugKotlin`:

  ```
  e: java.lang.NoSuchMethodError:
     'org.jetbrains.kotlin.fir.types.ConeKotlinType
      org.jetbrains.kotlin.fir.types.FirResolvedTypeRef.getType()'
        at io.realm.kotlin.compiler.IrUtilsKt.isBaseRealmObject(IrUtils.kt:225)
  ```

- There is no schema-less API (codegen is mandatory), no Java-entity escape
  hatch (it's a compiler plugin, not a JSR-269 processor), and realm-java's
  Gradle plugin uses the Transform API that AGP 8 removed.

## The standalone solution

`settings.gradle.kts` here anchors a self-contained build that pins the
toolchain Realm 2.3.0 supports:

| Component | Version |
|-----------|---------|
| Gradle (own wrapper) | 8.9 |
| Android Gradle Plugin | 8.7.3 |
| Kotlin Android plugin | 2.0.20 |
| io.realm.kotlin | 2.3.0 |

Build and verify:

```bash
./build-apk.sh                      # produces hello-realm-debug.apk
adb install -r hello-realm-debug.apk
adb shell am start -n com.example.hellodigitalis.hellorealm/com.example.hellorealm.MainActivity
adb logcat -d -s HelloRealm:*       # expect "REALM OK (…)"
```

The probe opens a Realm (loads arm64-v8a `librealmc.so`), writes an `Item`,
queries it back, and logs `REALM OK` / `REALM FAIL`. There is no
instrumentation `StatusTest` — the suite's `status-test-lib` lives in the
AGP 9 build and can't be consumed from this pinned toolchain, so verification
is launch + logcat (exactly how `hello-qt` is verified).

Verified on the Digitalis emulator: `REALM OK (schemaVersion=0, count=1,
name=realm-ⓦ)` — Realm Core's native store works under translation; the
incompatibility was always the host-side build toolchain, never the
translator.
