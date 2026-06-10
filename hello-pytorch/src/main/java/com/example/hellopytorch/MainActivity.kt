package com.example.hellopytorch

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellopytorch.R
import org.pytorch.PyTorchAndroid
import org.pytorch.Tensor

/**
 * Exercises PyTorch Mobile (Lite) — Meta's libtorch native C++ runtime — under
 * Berberis ARM64->x86_64 translation. The org.pytorch classes pull in
 * jni/arm64-v8a/{libpytorch_jni_lite.so, libfbjni.so, libc++_shared.so} (the
 * abiFilter keeps only the arm64-v8a slice), all bionic-linked arm64 natives
 * that load and run under translation.
 *
 * No TorchScript model asset is needed. Two things are exercised:
 *
 *  1. A real native call into libtorch: touching PyTorchAndroid runs its static
 *     initializer (NativeLoader.loadLibrary("pytorch_jni_lite") -> dlopen of the
 *     arm64 .so), and PyTorchAndroid.setNumThreads(1) invokes the native
 *     nativeSetNumThreads (at::set_num_threads / caffe2 threadpool) — genuine
 *     translated libtorch machine code, not a Java no-op.
 *
 *  2. A deterministic round-trip through the org.pytorch.Tensor API: build a
 *     known float tensor [1..6] shaped 2x3 and a long tensor, read them back,
 *     and self-check element-wise equality + shape. This validates the data
 *     model the native peer consumes.
 *
 * Logs "PYTORCH OK (...)" on success (including the round-tripped values/shape)
 * so the suite's StatusTest can assert a clean run. The work runs on a
 * background thread; the result is posted back to the UI thread.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // libtorch native init and the tensor round-trip run off the UI thread.
        Thread {
            val msg = runPytorchProbe()
            Log.i(TAG, msg)
            Handler(Looper.getMainLooper()).post {
                findViewById<TextView>(R.id.sample_text).text = msg
            }
        }.start()
    }

    private fun runPytorchProbe(): String {
        return try {
            // (1) Real native call into libtorch. Referencing PyTorchAndroid
            // loads libpytorch_jni_lite.so under translation; setNumThreads is a
            // native JNI call into the caffe2/at threadpool.
            PyTorchAndroid.setNumThreads(1)

            // (2) Float tensor round-trip: [1,2,3,4,5,6] shaped 2x3.
            val floatShape = longArrayOf(2, 3)
            val floatData = floatArrayOf(1f, 2f, 3f, 4f, 5f, 6f)
            val floatTensor = Tensor.fromBlob(floatData, floatShape)
            val floatBack = floatTensor.dataAsFloatArray
            val floatShapeBack = floatTensor.shape()
            val floatOk = floatBack.contentEquals(floatData) &&
                floatShapeBack.contentEquals(floatShape)

            // (3) Long tensor round-trip: [10,20,30,40] shaped 2x2.
            val longShape = longArrayOf(2, 2)
            val longData = longArrayOf(10L, 20L, 30L, 40L)
            val longTensor = Tensor.fromBlob(longData, longShape)
            val longBack = longTensor.dataAsLongArray
            val longShapeBack = longTensor.shape()
            val longOk = longBack.contentEquals(longData) &&
                longShapeBack.contentEquals(longShape)

            if (floatOk && longOk) {
                "PYTORCH OK (dtype=${floatTensor.dtype()}, " +
                    "float[${floatShape.joinToString("x")}]=" +
                    "${floatBack.joinToString(",")}, " +
                    "long[${longShape.joinToString("x")}]=" +
                    "${longBack.joinToString(",")}, threads set)"
            } else {
                // Genuine mismatch — diagnostic without the suite's crash tokens.
                "PYTORCH mismatch: floatOk=$floatOk longOk=$longOk " +
                    "floatBack=${floatBack.joinToString(",")} " +
                    "longBack=${longBack.joinToString(",")}"
            }
        } catch (t: Throwable) {
            "PYTORCH error: ${t.javaClass.simpleName}: ${t.message}"
        }
    }

    companion object {
        private const val TAG = "HelloPytorch"
    }
}
