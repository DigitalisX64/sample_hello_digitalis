package com.example.helloopencv

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloopencv.R
import org.opencv.android.OpenCVLoader
import org.opencv.core.Core
import org.opencv.core.CvType
import org.opencv.core.Mat
import org.opencv.core.Scalar
import org.opencv.core.Size
import org.opencv.imgproc.Imgproc

/**
 * Exercises OpenCV — its arm64-v8a libopencv_java4.so runs NEON-optimized image
 * kernels — under Berberis ARM64->x86_64 translation. Each check uses an input
 * with an analytically known result (no reference image needed), so a wrong
 * answer means the translator mishandled a NEON/SIMD path. Logs "OPENCV OK" or
 * "OPENCV FAIL" for the suite's StatusTest.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        return try {
            if (!OpenCVLoader.initLocal()) {
                val msg = "OPENCV FAIL: native load (initLocal) returned false"
                Log.e(TAG, msg)
                return msg
            }

            // 1. Element-wise multiply by scalar, then sum: 5*2 over 100x100 = 100000.
            val ones = Mat(100, 100, CvType.CV_8UC1, Scalar(5.0))
            val doubled = Mat()
            Core.multiply(ones, Scalar(2.0), doubled)
            val mulSum = Core.sumElems(doubled).`val`[0]

            // 2. RGBA->GRAY of (R=255,G=128,B=0): Y = round(0.299*255+0.587*128) = 151.
            val rgba = Mat(16, 16, CvType.CV_8UC4, Scalar(255.0, 128.0, 0.0, 255.0))
            val gray = Mat()
            Imgproc.cvtColor(rgba, gray, Imgproc.COLOR_RGBA2GRAY)
            val grayVal = gray.get(8, 8)[0]

            // 3. Gaussian blur of a constant image (replicate border) stays constant.
            val flat = Mat(64, 64, CvType.CV_8UC1, Scalar(100.0))
            val blurred = Mat()
            Imgproc.GaussianBlur(flat, blurred, Size(5.0, 5.0), 0.0, 0.0, Core.BORDER_REPLICATE)
            val mm = Core.minMaxLoc(blurred)

            // 4. Binary threshold: 200 > 100 -> all 255, countNonZero = 50*50.
            val src = Mat(50, 50, CvType.CV_8UC1, Scalar(200.0))
            val thr = Mat()
            Imgproc.threshold(src, thr, 100.0, 255.0, Imgproc.THRESH_BINARY)
            val nonZero = Core.countNonZero(thr)

            val checks = listOf(
                "multiply+sum" to (mulSum == 100000.0),
                "cvtColor-gray" to (grayVal >= 150.0 && grayVal <= 152.0),
                "gaussianBlur-const" to (mm.minVal >= 99.0 && mm.maxVal <= 101.0),
                "threshold-count" to (nonZero == 2500),
            )
            listOf(ones, doubled, rgba, gray, flat, blurred, src, thr).forEach { it.release() }

            val failed = checks.filterNot { it.second }.map { it.first }
            val msg = if (failed.isEmpty()) {
                "OPENCV OK (v${Core.VERSION}, mulSum=$mulSum, gray=$grayVal, " +
                    "blur=[${mm.minVal},${mm.maxVal}], nz=$nonZero)"
            } else {
                "OPENCV FAIL: ${failed.joinToString(",")} " +
                    "(mulSum=$mulSum, gray=$grayVal, blur=[${mm.minVal},${mm.maxVal}], nz=$nonZero)"
            }
            Log.i(TAG, msg)
            msg
        } catch (t: Throwable) {
            val msg = "OPENCV FAIL: exception ${t.javaClass.simpleName}: ${t.message}"
            Log.e(TAG, msg, t)
            msg
        }
    }

    companion object {
        private const val TAG = "HelloOpenCV"
    }
}
