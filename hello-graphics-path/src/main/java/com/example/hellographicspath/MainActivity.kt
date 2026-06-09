package com.example.hellographicspath

import android.graphics.Path
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.graphics.path.PathIterator
import androidx.graphics.path.PathSegment
import com.example.hellodigitalis.hellographicspath.R

/**
 * Exercises AndroidX graphics-path — its arm64-v8a libandroidx.graphics.path.so
 * reads a Path's native segment data (and converts conics) — under Berberis
 * ARM64->x86_64 translation. The probe iterates a known triangle (expecting
 * Move,Line,Line,Close) and a circle (expecting several conic/curve segments)
 * and asserts the segment types/counts, logging "GRAPHICSPATH OK" / "FAIL".
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        return try {
            val triangle = Path().apply {
                moveTo(0f, 0f)
                lineTo(100f, 0f)
                lineTo(100f, 100f)
                close()
            }
            val types = PathIterator(triangle).asSequence().map { it.type }.toList()
            val triangleOk = types == listOf(
                PathSegment.Type.Move,
                PathSegment.Type.Line,
                PathSegment.Type.Line,
                PathSegment.Type.Close,
            )

            val circle = Path().apply { addCircle(50f, 50f, 40f, Path.Direction.CW) }
            val circleSegs = PathIterator(circle).asSequence().count()
            val circleOk = circleSegs >= 4

            val checks = listOf(
                "triangle-segments" to triangleOk,
                "circle-segments" to circleOk,
            )
            val failed = checks.filterNot { it.second }.map { it.first }
            val msg = if (failed.isEmpty()) {
                "GRAPHICSPATH OK (triangle=${types.map { it.name }}, circleSegs=$circleSegs)"
            } else {
                "GRAPHICSPATH FAIL: ${failed.joinToString(",")} " +
                    "(triangle=${types.map { it.name }}, circleSegs=$circleSegs)"
            }
            Log.i(TAG, msg)
            msg
        } catch (t: Throwable) {
            val msg = "GRAPHICSPATH FAIL: exception ${t.javaClass.simpleName}: ${t.message}"
            Log.e(TAG, msg, t)
            msg
        }
    }

    companion object {
        private const val TAG = "HelloGraphicsPath"
    }
}
