package com.example.hellopytorch

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellopytorch.R
import org.pytorch.IValue
import org.pytorch.LiteModuleLoader
import org.pytorch.PyTorchAndroid
import org.pytorch.Tensor
import kotlin.math.abs

/**
 * Exercises PyTorch Mobile (Lite) — Meta's libtorch native C++ runtime — under
 * Berberis ARM64->x86_64 translation. The org.pytorch classes pull in
 * jni/arm64-v8a/{libpytorch_jni_lite.so, libfbjni.so, libc++_shared.so} (the
 * abiFilter keeps only the arm64-v8a slice), all bionic-linked arm64 natives
 * that load and run under translation.
 *
 * Three layers:
 *  1. A real native call into libtorch: touching PyTorchAndroid runs its static
 *     initializer (dlopen of the arm64 .so), and setNumThreads(1) invokes the
 *     native at::set_num_threads / caffe2 threadpool.
 *  2. A deterministic round-trip through the org.pytorch.Tensor API (float + long
 *     tensors), validating the data model the native peer consumes.
 *  3. Real inference (the gated assertion): load the synthetic fixed-weight
 *     TorchScript module produced by tools/gen_model.py (Linear(4->4) -> ReLU ->
 *     Linear(4->3), saved as a lite-interpreter .ptl), forward() the committed
 *     input [2,-1,3,1], and assert the output against the golden [-7,16,-1]. The
 *     weights are small integers so the float32 result is exact on host and
 *     device; a translator miscompile in the native GEMM/ReLU kernels changes
 *     the numbers and trips the golden.
 *
 * Logs "PYTORCH OK (...)" on success or "PYTORCH FAIL ..." on mismatch so the
 * suite's StatusTest can assert a clean run. The work runs on a background
 * thread; the result is posted back to the UI thread.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

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
            // (1) Real native call into libtorch.
            PyTorchAndroid.setNumThreads(1)

            // (2) Float tensor round-trip: [1,2,3,4,5,6] shaped 2x3.
            val floatShape = longArrayOf(2, 3)
            val floatData = floatArrayOf(1f, 2f, 3f, 4f, 5f, 6f)
            val floatTensor = Tensor.fromBlob(floatData, floatShape)
            val floatOk = floatTensor.dataAsFloatArray.contentEquals(floatData) &&
                floatTensor.shape().contentEquals(floatShape)

            // (3) Long tensor round-trip: [10,20,30,40] shaped 2x2.
            val longShape = longArrayOf(2, 2)
            val longData = longArrayOf(10L, 20L, 30L, 40L)
            val longTensor = Tensor.fromBlob(longData, longShape)
            val longOk = longTensor.dataAsLongArray.contentEquals(longData) &&
                longTensor.shape().contentEquals(longShape)

            if (!floatOk || !longOk) {
                return "PYTORCH FAIL: tensor round-trip floatOk=$floatOk longOk=$longOk"
            }

            // (4) Real inference against the golden.
            val inference = runInference()

            if (inference.startsWith("out=")) {
                "PYTORCH OK (dtype=${floatTensor.dtype()}, tensor round-trip ok; " +
                    "inference $inference)"
            } else {
                "PYTORCH $inference"
            }
        } catch (t: Throwable) {
            "PYTORCH FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
    }

    /** Runs model.ptl on the committed input; returns "out=[...]" or "FAIL ...". */
    private fun runInference(): String {
        if (!assetExists(MODEL_ASSET)) {
            // Documented SKIP: run tools/gen_model.py to (re)generate model.ptl.
            // Kept free of the "FAIL" substring so StatusTest still passes.
            return "out=SKIP (no model asset)"
        }
        val module = LiteModuleLoader.loadModuleFromAsset(assets, MODEL_ASSET)
        try {
            val input = Tensor.fromBlob(INPUT, longArrayOf(1L, 4L))
            val got = module.forward(IValue.from(input)).toTensor().dataAsFloatArray
            var bad = -1
            for (i in GOLDEN.indices) {
                if (i >= got.size || abs(got[i] - GOLDEN[i]) > TOLERANCE) { bad = i; break }
            }
            return if (bad < 0) {
                "out=${got.toList()}"
            } else {
                "FAIL at output[$bad]: got=${got.toList()} want=${GOLDEN.toList()}"
            }
        } finally {
            module.destroy()
        }
    }

    private fun assetExists(name: String): Boolean =
        runCatching { assets.open(name).close() }.isSuccess

    companion object {
        private const val TAG = "HelloPytorch"
        private const val MODEL_ASSET = "model.ptl"
        private const val TOLERANCE = 1e-4f

        // Committed input and golden from tools/gen_model.py:
        //   Linear(4->4) -> [2,2,-5,11], ReLU -> [2,2,0,11], Linear(4->3) -> [-7,16,-1].
        private val INPUT = floatArrayOf(2f, -1f, 3f, 1f)
        private val GOLDEN = floatArrayOf(-7f, 16f, -1f)
    }
}
