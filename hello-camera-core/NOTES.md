# hello-camera-core — native coverage notes

## Artifact
`androidx.camera:camera-core:1.3.4` (verified HTTP 200 on Google Maven:
`https://dl.google.com/android/maven2/androidx/camera/camera-core/1.3.4/camera-core-1.3.4.aar`).
The AAR bundles `jni/arm64-v8a/libimage_processing_util_jni.so`
(`System.loadLibrary("image_processing_util_jni")`).

## Exported JNI symbols (from `llvm-objdump -T jni/arm64-v8a/libimage_processing_util_jni.so`)
All on class `androidx.camera.core.ImageProcessingUtil`:

- `Java_..._nativeCopyBetweenByteBufferAndBitmap`
- `Java_..._nativeShiftPixel`
- `Java_..._nativeWriteJpegToSurface`
- `Java_..._nativeConvertAndroid420ToABGR`
- `Java_..._nativeConvertAndroid420ToBitmap`
- `Java_..._nativeRotateYUV`

The `native` methods themselves are `private static`. Reachability from app
code (no live camera) was the deciding factor:

| native method | reachable headlessly? | why |
|---|---|---|
| `nativeRotateYUV`, `nativeConvertAndroid420ToABGR`, `nativeConvertAndroid420ToBitmap`, `nativeShiftPixel` | NO | only called by `convertYUVToRGB` / `rotateYUV` / `convertYUVToBitmap`, all of which require a live `ImageProxy` (YUV_420_888 planes from a camera) and/or a `Surface` / `ImageWriter`. Not constructible without a camera. |
| `nativeWriteJpegToSurface` | NO | needs a `Surface`. |
| `nativeCopyBetweenByteBufferAndBitmap` | **YES** | wrapped by the **public static** helpers `copyBitmapToByteBuffer(Bitmap, ByteBuffer, int)` and `copyByteBufferToBitmap(Bitmap, ByteBuffer, int)`, which take only a `Bitmap` + a direct `ByteBuffer` + a row stride. No camera. |

## Chosen probe strategy
Drive the public `copyBitmapToByteBuffer` / `copyByteBufferToBitmap` helpers
(via reflection, to dodge the class's `@RestrictTo(LIBRARY_GROUP)` lint — these
are genuinely `public static` at the bytecode level, so reflection is purely to
keep the cross-module build clean):

1. `Class.forName("androidx.camera.core.ImageProcessingUtil")` runs the
   `<clinit>` that calls `System.loadLibrary("image_processing_util_jni")`, so
   the arm64 `.so` is dlopened and its `JNI_OnLoad` runs under translation.
   The probe then confirms the `.so` is mapped via `/proc/self/maps`.
2. A known `8x4` ARGB_8888 `Bitmap` (with a sentinel pixel at (3,2)) is copied
   into a direct `ByteBuffer` via the native `copyBitmapToByteBuffer`, then that
   buffer is copied back into a fresh `Bitmap` via the native
   `copyByteBufferToBitmap`. The probe asserts the sentinel pixel and the corner
   pixel survived the native round-trip.

### Coverage level
This is a **real native execution**, not just a load check. Disassembly of
`nativeCopyBetweenByteBufferAndBitmap` confirms it locks the bitmap pixels
(`AndroidBitmap_lockPixels`, NDK jnigraphics), reads the direct-buffer pointer
(`GetDirectBufferAddress`), and calls an internal row-stride copy helper
(`.text+0x54`, at `0x2990`) selecting src/dst order by the `isCopyBufferToBitmap`
flag — a deterministic per-row byte copy. So a clean, pixel-exact round-trip
means arm64 native pixel-copy code (plus jnigraphics) executed correctly under
Berberis translation. The full YUV<->RGB / rotation conversion kernels remain
camera-gated and are not exercised by this headless probe.

Success marker: `CAMERACORE OK (...)`. Failure paths log `CAMERACORE ERROR` /
`CAMERACORE MISMATCH` (neither contains the suite's crash sentinels).
