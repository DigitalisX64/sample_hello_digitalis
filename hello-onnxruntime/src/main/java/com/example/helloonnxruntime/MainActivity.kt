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

package com.example.helloonnxruntime

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloonnxruntime.R
import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OrtEnvironment
import java.nio.FloatBuffer

/**
 * Exercises ONNX Runtime for Android — Microsoft's native (C/C++) inference
 * engine — under Berberis ARM64->x86_64 translation. The first
 * OrtEnvironment.getEnvironment() call System.loadLibrary's the arm64-v8a
 * libonnxruntime.so + libonnxruntime4j_jni.so. The probe then exercises the
 * native memory path without needing a model asset: it queries the available
 * execution providers (must contain CPU), allocates an OnnxTensor of shape
 * [2,2] from a known FloatArray through the JNI bridge into native ORT memory,
 * reads the values back out and self-checks both the value round-trip and the
 * reported tensor shape, logging "ONNX OK" or "ONNX FAIL" so the suite's
 * StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // getEnvironment() loads the native ORT libraries on first call.
            val env = OrtEnvironment.getEnvironment()
            try {
                val providers = OrtEnvironment.getAvailableProviders()
                // The CPU execution provider is always present in a working build.
                val hasCpu = providers.any { it.name == "CPU" }

                // A known 2x2 float matrix pushed through the JNI bridge into a
                // native ORT tensor, then read back.
                val data = floatArrayOf(1f, 2f, 3f, 4f)
                val shape = longArrayOf(2L, 2L)
                val tensor = OnnxTensor.createTensor(env, FloatBuffer.wrap(data), shape)
                try {
                    val readBack = FloatArray(data.size)
                    tensor.floatBuffer.get(readBack)
                    val outShape = tensor.info.shape

                    val valuesOk = readBack.contentEquals(data)
                    val shapeOk = outShape.contentEquals(shape)

                    if (hasCpu && valuesOk && shapeOk) {
                        "ONNX OK (providers=${providers.size}, tensor ${shape[0]}x${shape[1]} " +
                            "round-trip [${readBack.joinToString(",")}])"
                    } else {
                        "ONNX " + "FAIL: hasCpu=$hasCpu valuesOk=$valuesOk shapeOk=$shapeOk " +
                            "(providers=${providers.size}, shape=${outShape.joinToString("x")})"
                    }
                } finally {
                    tensor.close()
                }
            } finally {
                env.close()
            }
        } catch (t: Throwable) {
            "ONNX " + "FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloOnnxRuntime"
    }
}
