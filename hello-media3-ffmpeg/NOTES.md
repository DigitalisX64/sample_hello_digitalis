# hello-media3-ffmpeg — artifact status

## Summary

`androidx.media3:media3-decoder-ffmpeg` is **not** published as a prebuilt AAR
on Google Maven. It ships **source-only**: the FFmpeg audio-decoder extension
must be built from source against a locally compiled FFmpeg (the native
`libffmpegJN.so` is produced by an NDK build, not distributed). This module is
authored with the intended dependencies + probe code so that, once a locally
built AAR is dropped in, it resolves and runs without further edits. **No pass
is fabricated** — with a stock Maven build the probe reports a benign
`MEDIA3FFMPEG MISS` (not "FAIL"), because the Java classes resolve but
`FfmpegLibrary.isAvailable()` returns false (no native `.so`).

## Maven metadata checks (2026-06-10)

Google Maven mirror: `https://dl.google.com/dl/android/maven2/`

| Coordinate | URL checked | HTTP |
|---|---|---|
| `media3-decoder-ffmpeg` (metadata) | `.../androidx/media3/media3-decoder-ffmpeg/maven-metadata.xml` | **404** |
| `media3-decoder-ffmpeg:1.10.1` (pom) | `.../media3-decoder-ffmpeg/1.10.1/media3-decoder-ffmpeg-1.10.1.pom` | **404** |
| `media3-exoplayer` (metadata) | `.../media3-exoplayer/maven-metadata.xml` | 200 (latest 1.10.1) |
| `media3-exoplayer:1.10.1` (pom / aar) | `.../media3-exoplayer/1.10.1/...` | 200 / 200 |
| `media3-common:1.10.1` (pom / aar) | `.../media3-common/1.10.1/...` | 200 / 200 |
| `media3-decoder:1.10.1` (pom / aar) | `.../media3-decoder/1.10.1/...` | 200 / 200 |

So every companion media3 artifact resolves at 1.10.1; **only**
`media3-decoder-ffmpeg` is absent at every version — confirming source-only.

## Why it's source-only

The media3 FFmpeg extension README documents that the decoder is shipped as
source and the FFmpeg library it links must be compiled locally for the target
ABIs. Extension README path in the AndroidX media3 source tree:

```
libraries/decoder_ffmpeg/README.md
```

(Equivalently, in older ExoPlayer layouts: `extensions/ffmpeg/README.md`.)

## Manual build step (to produce the AAR)

The README's procedure (paraphrased — follow the upstream README for exact,
versioned commands):

1. Set up the NDK and check out the media3 source at the matching tag (1.10.1).
2. From `libraries/decoder_ffmpeg/src/main/jni/`, fetch an FFmpeg checkout and
   run the bundled `build_ffmpeg.sh`, enabling the desired decoders, e.g.:
   ```bash
   cd libraries/decoder_ffmpeg/src/main/jni
   git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg && cd ffmpeg
   git checkout <ffmpeg-release-branch>
   cd ..
   ENABLED_DECODERS=(vorbis opus flac alac pcm_mulaw pcm_alaw mp3 aac ac3 eac3 dca mlp truehd)
   ./build_ffmpeg.sh "${NDK_PATH}" "linux-x86_64" "21" "${ENABLED_DECODERS[@]}"
   ```
   This produces the per-ABI native libs (including `arm64-v8a/libffmpegJN.so`).
3. Build the extension AAR from the media3 Gradle project:
   ```bash
   ./gradlew :lib-decoder-ffmpeg:assembleRelease
   ```
4. Publish/copy the resulting AAR (which now contains
   `jni/arm64-v8a/libffmpegJN.so`) into a local Maven repo or a `flatDir`
   repository this module can resolve from, at coordinate
   `androidx.media3:media3-decoder-ffmpeg:1.10.1`.

Only `arm64-v8a` is required here — this module already pins
`ndk { abiFilters += "arm64-v8a" }`, matching the Digitalis ARM64-only target.

## What the probe does once the native lib is present

On a background thread (see `MainActivity.runFfmpegProbe`):

1. `FfmpegLibrary.isAvailable()` — dlopens `libffmpegJN.so` and resolves its JNI
   symbols. This is the load-bearing translation step under Berberis.
2. `FfmpegLibrary.getVersion()` — JNI call into native FFmpeg; logged.
3. `FfmpegLibrary.supportsFormat(MimeTypes.AUDIO_AAC)` and `AUDIO_OPUS` — native
   format-capability queries; success requires at least one.

Reported result:
- Native present + a format supported -> `MEDIA3FFMPEG OK (ffmpeg <v>, ...)`.
- Native absent (stock Maven build) -> `MEDIA3FFMPEG MISS: ...` (no "FAIL"
  marker, so the suite treats it as a benign miss, not a translator crash).
- Exception or unexpected state -> `MEDIA3FFMPEG FAIL: ...`.

`FfmpegLibrary` is `@UnstableApi`, hence the `@OptIn(UnstableApi::class)` on the
probe. `MimeTypes` is from `media3-common`.
