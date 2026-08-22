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
package com.example.helloyoga

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.bench.Bench
import com.example.hellodigitalis.helloyoga.R
import com.facebook.yoga.YogaConstants
import com.facebook.yoga.YogaFlexDirection
import com.facebook.yoga.YogaNode
import com.facebook.yoga.YogaNodeFactory

/**
 * Exercises Facebook Yoga — the native (C/C++) flexbox layout engine
 * (libyoga.so) — under Berberis ARM64->x86_64 translation. The first YogaNode
 * call loads the arm64-v8a libyoga.so via SoLoader; the probe builds small flex
 * trees, runs the native layout solver (calculateLayout) and self-checks that
 * the computed child widths / offsets match the flexbox math, logging "YOGA OK"
 * or "YOGA FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runYogaProbe()
        runBenchmarks()
    }

    private fun runYogaProbe(): String {
        val msg = try {
            // Yoga loads libyoga.so through Facebook SoLoader, which must be
            // initialized once before the first YogaNode is created.
            com.facebook.soloader.SoLoader.init(this, false)

            // Case 1: two flexGrow=1 children in a 200-wide ROW split it evenly,
            // so each child is ~100 wide and child[1] starts at x ~100.
            val (w0a, w1a, x1a) = solveRow(grow0 = 1f, grow1 = 1f)
            val case1Ok = near(w0a, 100f) && near(w1a, 100f) && near(x1a, 100f)

            // Case 2: flexGrow 1 vs 3 splits the 200-wide row 50/150, so the
            // second child is 150 wide and starts at x ~50. Exercises the
            // solver's proportional distribution harder than the symmetric case.
            val (w0b, w1b, x1b) = solveRow(grow0 = 1f, grow1 = 3f)
            val case2Ok = near(w0b, 50f) && near(w1b, 150f) && near(x1b, 50f)

            if (case1Ok && case2Ok) {
                "YOGA OK (1:1 -> ${fmt(w0a)}/${fmt(w1a)} @${fmt(x1a)}; " +
                    "1:3 -> ${fmt(w0b)}/${fmt(w1b)} @${fmt(x1b)})"
            } else {
                "YOGA FAIL: case1Ok=$case1Ok case2Ok=$case2Ok " +
                    "1:1 -> ${fmt(w0a)}/${fmt(w1a)} @${fmt(x1a)}; " +
                    "1:3 -> ${fmt(w0b)}/${fmt(w1b)} @${fmt(x1b)}"
            }
        } catch (t: Throwable) {
            "YOGA FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    /**
     * Builds a 200x100 flex ROW with two children of the given flexGrow values,
     * runs the native layout solver, and returns
     * (child0.width, child1.width, child1.x).
     */
    private fun solveRow(grow0: Float, grow1: Float): Triple<Float, Float, Float> {
        val root = YogaNodeFactory.create()
        root.setWidth(200f)
        root.setHeight(100f)
        root.setFlexDirection(YogaFlexDirection.ROW)

        val child0 = YogaNodeFactory.create()
        child0.setFlexGrow(grow0)
        root.addChildAt(child0, 0)

        val child1 = YogaNodeFactory.create()
        child1.setFlexGrow(grow1)
        root.addChildAt(child1, 1)

        root.calculateLayout(YogaConstants.UNDEFINED, YogaConstants.UNDEFINED)

        return Triple(
            root.getChildAt(0).layoutWidth,
            root.getChildAt(1).layoutWidth,
            root.getChildAt(1).layoutX,
        )
    }

    /**
     * Timed workload, separate from the correctness probe above: lay out a
     * deeper flex tree many times. Each iteration nudges the root width so the
     * node is marked dirty and the native solver actually re-runs (Yoga
     * short-circuits calculateLayout on a clean tree), keeping the measurement
     * on the layout code path rather than a cache hit.
     */
    private fun runBenchmarks() {
        val module = "hello-yoga"
        val root = buildDeepTree()
        var w = 1000f
        Bench.run(module, "layout-deep-tree") {
            w = if (w > 1000f) 1000f else 1001f
            root.setWidth(w)
            root.calculateLayout(YogaConstants.UNDEFINED, YogaConstants.UNDEFINED)
        }
        Bench.done(module)
    }

    /**
     * A modestly deep flex tree: a ROW root with several COLUMN sections, each
     * holding a handful of flexGrow leaves. Enough nodes that the solver does
     * real proportional-distribution work per pass.
     */
    private fun buildDeepTree(): YogaNode {
        val root = YogaNodeFactory.create()
        root.setWidth(1000f)
        root.setHeight(600f)
        root.setFlexDirection(YogaFlexDirection.ROW)
        for (s in 0 until 6) {
            val section = YogaNodeFactory.create()
            section.setFlexGrow(1f)
            section.setFlexDirection(YogaFlexDirection.COLUMN)
            root.addChildAt(section, s)
            for (i in 0 until 8) {
                val leaf = YogaNodeFactory.create()
                leaf.setFlexGrow((i + 1).toFloat())
                section.addChildAt(leaf, i)
            }
        }
        return root
    }

    private fun near(actual: Float, expected: Float): Boolean =
        kotlin.math.abs(actual - expected) <= EPSILON

    private fun fmt(v: Float): String = String.format("%.1f", v)

    companion object {
        private const val TAG = "HelloYoga"
        private const val EPSILON = 0.5f
    }
}
