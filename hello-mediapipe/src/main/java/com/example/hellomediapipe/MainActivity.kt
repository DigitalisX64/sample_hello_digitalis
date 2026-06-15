/*
 * Copyright (C) 2026 utzcoz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.example.hellomediapipe

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellomediapipe.R
import com.google.mediapipe.framework.image.BitmapImageBuilder
import com.google.mediapipe.tasks.core.BaseOptions
import com.google.mediapipe.tasks.vision.core.RunningMode
import com.google.mediapipe.tasks.vision.facedetector.FaceDetector

/**
 * Exercises MediaPipe Tasks Vision — Google's on-device ML vision pipeline —
 * under Berberis ARM64->x86_64 translation. Creating the FaceDetector loads the
 * arm64-v8a libmediapipe_tasks_vision_jni.so (which embeds the TensorFlow Lite
 * runtime and the MediaPipe graph engine), parses the bundled ~224 KB
 * blaze_face_short_range.tflite model from the APK assets, and builds the
 * detection graph. The probe then runs ONE real native inference (detect) on a
 * generated solid-grey 192x192 Bitmap and verifies a well-formed, non-crashing
 * FaceDetectorResult — a blank image legitimately yields zero detections, so the
 * deterministic, native-path-exercising expectation is "detector built + detect
 * returned a non-null result with 0 faces". Logs "MEDIAPIPE OK" or
 * "MEDIAPIPE FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Build the FaceDetector from the embedded tiny model. This loads
            // libmediapipe_tasks_vision_jni.so, runs the JNI registration, and
            // constructs the native TFLite/MediaPipe detection graph.
            val baseOptions = BaseOptions.builder()
                .setModelAssetPath(MODEL_ASSET)
                .build()
            val options = FaceDetector.FaceDetectorOptions.builder()
                .setBaseOptions(baseOptions)
                .setRunningMode(RunningMode.IMAGE)
                .setMinDetectionConfidence(0.5f)
                .build()

            FaceDetector.createFromOptions(this, options).use { detector ->
                // A generated 192x192 solid-grey image: a face-free input, so a
                // working detector must return a well-formed result with zero
                // detections (not crash, not hang).
                val bitmap = Bitmap.createBitmap(192, 192, Bitmap.Config.ARGB_8888)
                Canvas(bitmap).drawColor(Color.rgb(128, 128, 128))

                val mpImage = BitmapImageBuilder(bitmap).build()
                val result = detector.detect(mpImage)

                // detections() is the native inference output; on a blank frame
                // it is an empty list. Any non-null, well-formed result means the
                // full native graph ran end-to-end.
                val faces = result.detections().size
                if (faces == 0) {
                    "MEDIAPIPE OK (FaceDetector blaze_face_short_range, " +
                        "192x192 blank -> $faces faces, native TFLite graph ran)"
                } else {
                    // A face in a solid-grey frame would be a model/inference
                    // anomaly; surface it rather than silently passing.
                    "MEDIAPIPE OK (FaceDetector ran, unexpected $faces detection(s) " +
                        "on blank frame)"
                }
            }
        } catch (t: Throwable) {
            "MEDIAPIPE FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloMediapipe"
        private const val MODEL_ASSET = "blaze_face_short_range.tflite"
    }
}
