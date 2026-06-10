# hello-ncnn artifact notes

## Why there is no Maven/prefab dependency line

Tencent ncnn does **not** publish a gradle-resolvable Android artifact. Verified
on 2026-06-10:

| Source | Coordinate tried | Result |
|--------|------------------|--------|
| Maven Central | `com.tencent.ncnn:ncnn` (`repo1.maven.org/.../com/tencent/ncnn/ncnn/maven-metadata.xml`) | 404 |
| Maven Central search | `q=ncnn` | only third-party `io.github.mymonstercat:rapidocr-ncnn-*` wrappers, no official ncnn |
| Google Maven | `com.tencent.ncnn:ncnn` (`dl.google.com/dl/android/maven2/...`) | 404 |
| JitPack | `com.github.Tencent:ncnn:<tag>` | every tag returns build `Error` / `gitError`; `maven-metadata.xml` 404 |

ncnn's official Android distribution is the GitHub release zip:

```
https://github.com/Tencent/ncnn/releases/download/<DATE>/ncnn-<DATE>-android.zip
# e.g. https://github.com/Tencent/ncnn/releases/download/20260526/ncnn-20260526-android.zip
```

That zip is **not** a prefab AAR. It is a per-ABI CMake package:

```
ncnn-<DATE>-android/<abi>/
  include/ncnn/*.h
  lib/libncnn.a                       # static, CPU-only (NCNN_VULKAN OFF)
  lib/cmake/ncnn/ncnnConfig.cmake     # find_package(ncnn CONFIG) entry point
```

So the module does not use `buildFeatures { prefab = true }`. Instead it wires
`find_package(ncnn REQUIRED CONFIG)` against the vendored package by passing
`-Dncnn_DIR=<module>/src/main/cpp/ncnn-prefab/arm64-v8a/lib/cmake/ncnn` from
`build.gradle.kts`. The imported `ncnn` target is fully self-contained
(`INTERFACE_LINK_LIBRARIES "-fopenmp;-static-openmp;Threads::Threads;android;jnigraphics;log"`),
so `target_link_libraries(helloncnn PRIVATE ncnn log)` is all the CMake needs.

## What is vendored in this module

Only the `arm64-v8a` subtree of `ncnn-20260526-android.zip` is checked in under
`src/main/cpp/ncnn-prefab/arm64-v8a/` (this module is arm64-v8a-only). That is
the static `libncnn.a` (~7 MB), the 29 public headers, and the four CMake config
files. The `_IMPORT_PREFIX` in `ncnn.cmake` is computed relative to the config
file location, so the directory layout under `arm64-v8a/` must be preserved
verbatim for `find_package` to locate `lib/libncnn.a`.

## Re-vendoring / bumping the ncnn version

```bash
curl -L -o /tmp/ncnn-android.zip \
  https://github.com/Tencent/ncnn/releases/download/<DATE>/ncnn-<DATE>-android.zip
rm -rf src/main/cpp/ncnn-prefab
unzip -q /tmp/ncnn-android.zip "ncnn-<DATE>-android/arm64-v8a/*" -d /tmp/ncnn-extract
mkdir -p src/main/cpp/ncnn-prefab
mv /tmp/ncnn-extract/ncnn-<DATE>-android/arm64-v8a src/main/cpp/ncnn-prefab/arm64-v8a
```

No `build.gradle.kts` / `CMakeLists.txt` change is needed across versions — the
path is version-independent. The `ncnn_version()` string in the probe report is
read at runtime from the vendored `libncnn.a`, so it always reflects the
checked-in version.
