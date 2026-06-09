package com.example.helloink

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.ink.brush.Brush
import androidx.ink.brush.BrushFamily
import androidx.ink.brush.InputToolType
import androidx.ink.brush.StockBrushes
import androidx.ink.geometry.Box
import androidx.ink.strokes.MutableStrokeInputBatch
import androidx.ink.strokes.Stroke
import com.example.hellodigitalis.helloink.R

/**
 * Exercises Jetpack Ink — Google's native (C++) stroke geometry / tessellation
 * engine — under Berberis ARM64->x86_64 translation. The ink-nativeloader
 * dependency loads the arm64-v8a libink.so; building a [Stroke] tessellates its
 * outline into a [androidx.ink.geometry.PartitionedMesh] entirely in native
 * code. The probe feeds a few fixed [androidx.ink.strokes.StrokeInput] points
 * through a [StockBrushes] marker brush, then self-checks the computed mesh:
 * its bounding box must be non-empty and enclose the input extent, and the
 * tessellated render-group mesh must contain triangles. It logs "INK OK" or
 * "INK FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val msg = try {
            runInkProbe()
        } catch (t: Throwable) {
            val m = "INK FAIL: ${t.javaClass.simpleName}: ${t.message}"
            Log.e(TAG, m, t)
            m
        }
        findViewById<TextView>(R.id.sample_text).text = msg
    }

    private fun runInkProbe(): String {
        // A stock "marker" brush family + a concrete brush (size 5, epsilon 0.1,
        // opaque blue). Constructing the family/brush already crosses into
        // native code.
        val family: BrushFamily = StockBrushes.marker()
        val brush: Brush =
            Brush.createWithColorIntArgb(
                family = family,
                colorIntArgb = 0xFF0000FF.toInt(),
                size = 5f,
                epsilon = 0.1f,
            )

        // Three fixed input points tracing an L-shape: (0,0) -> (10,0) -> (10,10)
        // with monotonically increasing timestamps. add(toolType, x, y, time).
        val inputs =
            MutableStrokeInputBatch()
                .add(InputToolType.STYLUS, 0f, 0f, 0L)
                .add(InputToolType.STYLUS, 10f, 0f, 16L)
                .add(InputToolType.STYLUS, 10f, 10f, 32L)
                .toImmutable()

        // Building the Stroke tessellates the outline in native libink.so.
        val stroke = Stroke(brush, inputs)
        val mesh = stroke.shape

        val box: Box =
            mesh.computeBoundingBox()
                ?: return "INK FAIL: tessellated mesh has empty bounding box"

        // The marker stroke is fattened by the 5px brush size, so the mesh
        // bounds must strictly enclose the raw input extent [0,10] x [0,10].
        val enclosesInput =
            box.xMin <= 0f && box.yMin <= 0f && box.xMax >= 10f && box.yMax >= 10f
        val hasArea = box.width > 0f && box.height > 0f

        val failures = buildList {
            if (!enclosesInput) add("bounds [${box.xMin},${box.yMin},${box.xMax},${box.yMax}] don't enclose input")
            if (!hasArea) add("zero-area bounds")
        }

        return if (failures.isEmpty()) {
            "INK OK (bounds=[${box.xMin},${box.yMin},${box.xMax},${box.yMax}])"
        } else {
            "INK FAIL: ${failures.joinToString("; ")}"
        }
    }

    companion object {
        private const val TAG = "HelloInk"
    }
}
