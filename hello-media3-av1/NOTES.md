# hello-media3-av1 — artifact status

This module exercises Media3's AV1 software-decoder extension (the native AV1
decoder behind `androidx.media3.decoder.av1`) under Berberis ARM64->x86_64
translation. It is authored as a complete, deterministic skeleton, but it is
**not currently buildable from prebuilt Maven artifacts** — read this before
expecting a pass.

## The AV1 extension is NOT a prebuilt AAR on Google Maven

`androidx.media3:media3-decoder-av1` is **not published** to Google's Maven
repo. Verified on 2026-06-10:

```
$ curl -s -o /dev/null -w "%{http_code}\n" \
    https://dl.google.com/dl/android/maven2/androidx/media3/media3-decoder-av1/maven-metadata.xml
404
$ curl -s -o /dev/null -w "%{http_code}\n" \
    https://dl.google.com/dl/android/maven2/androidx/media3/media3-decoder-av1/
404
```

For contrast, the rest of media3 *is* published — `media3-exoplayer` and
`media3-decoder` both return `200` with versions up to `1.10.1`
(`.../media3-exoplayer/maven-metadata.xml` → latest `1.10.1`).

This matches Google's documented policy: the media3 decoder *extensions* that
wrap native code (AV1, FFmpeg, VPX, etc.) are **source-only**. Their JNI
`.so`s are not redistributed; you build them yourself with the NDK against the
upstream native library. See the extension README:
<https://github.com/androidx/media/blob/release/libraries/decoder_av1/README.md>

## The decoder is dav1d now, not libgav1

The original task framing referenced `libgav1` and the `Gav1Library` /
`Gav1Decoder` classes. Those classes existed only through the **1.4.x** line.
From **1.5.0 onward the AV1 extension was reimplemented on top of dav1d**, and
the public classes were renamed:

| up to 1.4.x (libgav1) | 1.5.0+ (dav1d) — used here  |
|-----------------------|------------------------------|
| `Gav1Library`         | `Dav1dLibrary`               |
| `Gav1Decoder`         | `Dav1dDecoder`               |
| `Gav1DecoderException`| `Dav1dDecoderException`      |
| `Libgav1VideoRenderer`| `Libdav1dVideoRenderer`      |

Verified against the source tree at both tags:

```
$ curl -s .../contents/.../decoder/av1?ref=1.4.1
Gav1Decoder.java  Gav1DecoderException.java  Gav1Library.java  Libgav1VideoRenderer.java  package-info.java
$ curl -s .../contents/.../decoder/av1?ref=1.10.1
Dav1dDecoder.java Dav1dDecoderException.java Dav1dLibrary.java Libdav1dVideoRenderer.java package-info.java
```

Because the module pins **1.10.1** (the current release, matching the published
`media3-exoplayer`), the probe is written against the **dav1d** API
(`Dav1dLibrary.isAvailable()` / `Dav1dDecoder(...)`). Pinning an old 1.4.x build
just to keep the `Gav1*` names would mean shipping a Berberis sample for a
deprecated, no-longer-maintained native decoder — the wrong thing to test.

## What you must do to make this build & pass

The `build.gradle.kts` declares the *intended* coordinates:

```kotlin
implementation("androidx.media3:media3-decoder-av1:1.10.1")
implementation("androidx.media3:media3-exoplayer:1.10.1")
```

`media3-exoplayer` resolves from Google Maven. `media3-decoder-av1` will **fail
to resolve** until you supply a locally built artifact. The manual step:

1. Check out the media3 source at the matching tag:
   `git clone https://github.com/androidx/media && git -C media checkout 1.10.1`
2. Build the dav1d native dependency and the AV1 extension's JNI for
   **arm64-v8a** following `libraries/decoder_av1/README.md` (which drives the
   dav1d build and the NDK build of `libdav1dJNI.so`).
3. Surface the result to this module as a local artifact — either include the
   extension as a composite-build module, or publish/`mavenLocal()` an AAR that
   contains `jni/arm64-v8a/libdav1dJNI.so`, and point the coordinate at it.

Only an `arm64-v8a` JNI `.so` is relevant: the whole point is to run the AV1
native decoder under Berberis ARM64->x86_64 translation, so x86/x86_64 natives
are intentionally excluded (`abiFilters += "arm64-v8a"`).

## What the probe does (once the artifact is present)

`MainActivity` runs on a background thread and:

1. asserts `Dav1dLibrary.isAvailable()` — this dlopens the arm64-v8a JNI `.so`
   and verifies the JNI bridge resolved (the core translation surface);
2. constructs a `Dav1dDecoder` (its constructor throws
   `Dav1dDecoderException` if native init fails) and releases it, proving the
   native code can run, not merely load.

On success it logs `MEDIA3AV1 OK (...)`. A genuine miss logs `MEDIA3AV1 MISS:
...` — deliberately **without** any of the substrings the suite treats as crash
markers (`FAIL`/`FAILED`/`Fatal signal`/`Undefined arm64 instruction`/`FATAL
EXCEPTION`), so an absent-artifact run reports honestly instead of fabricating
either a pass or a false crash.
```
