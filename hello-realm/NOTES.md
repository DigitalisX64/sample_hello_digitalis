# hello-realm — blocked by the AGP 9 / Kotlin toolchain (not the translator)

This module exercises Realm Kotlin's native store (`librealmc.so`, Realm Core
C++). The translator is not implicated: the module cannot be **compiled** under
this project's build toolchain, so it is intentionally left out of
`settings.gradle.kts` and `test-samples.sh`.

## Root cause

Realm Kotlin works via a Kotlin **compiler plugin** (`io.realm.kotlin`) that
generates the schema glue for each `RealmObject` during Kotlin compilation. A
Kotlin compiler plugin links against Kotlin compiler internals, so its binary
ABI must match the exact Kotlin compiler version in use.

- The project builds with **AGP 9.0.0** and its **built-in Kotlin** (a Kotlin
  2.1+/2.2 compiler). AGP 9 puts Kotlin on the classpath itself, so applying an
  external `org.jetbrains.kotlin.android` at a pinned version is rejected:
  *"plugin is already on the classpath with an unknown version."*
- Realm Kotlin's latest **and final** release is **3.0.0** (MongoDB sunset the
  project). Its compiler plugin targets an older Kotlin. Loaded into AGP 9's
  compiler it crashes during `compileDebugKotlin`:

  ```
  e: java.lang.NoSuchMethodError:
     'org.jetbrains.kotlin.fir.types.ConeKotlinType
      org.jetbrains.kotlin.fir.types.FirResolvedTypeRef.getType()'
        at io.realm.kotlin.compiler.IrUtilsKt.isBaseRealmObject(IrUtils.kt:225)
        at io.realm.kotlin.compiler.fir.model.ObjectExtension.getCallableNamesForClass
  ```

  i.e. the plugin calls a FIR compiler-internal method that the newer Kotlin
  removed/changed.

## Why there is no in-surface fix

- **No schema-less API.** Unlike AppSearch (GenericDocument) there is no Realm
  API that avoids the compiler plugin — `RealmObject` codegen is mandatory.
- **No Java-entity escape hatch.** It is a Kotlin *compiler* plugin, not a
  JSR-269 annotation processor, so the ObjectBox-style "write the entity in
  Java + `annotationProcessor`" trick does not apply.
- **realm-java is also dead on AGP 9.** The older `io.realm:realm-android`
  variant relies on a Gradle **Transform API** bytecode weaver that AGP 8
  removed entirely.
- **Downgrading is not module-local.** Pinning the whole project to Kotlin
  2.0.20 to satisfy Realm would break the 50+ other sample modules that rely on
  AGP 9's built-in Kotlin.

## What would unblock it

A Realm Kotlin release whose compiler plugin is built against the Kotlin
compiler AGP 9 bundles. Since the project is sunset, that release is unlikely to
appear. If Realm support is required, the alternative is a separate build
environment pinned to AGP/Kotlin versions Realm 3.0.0 supports — out of scope
for this in-tree sample suite.
