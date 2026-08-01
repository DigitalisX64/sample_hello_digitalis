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

package com.example.hellobox2d

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.badlogic.gdx.math.Vector2
import com.badlogic.gdx.physics.box2d.Body
import com.badlogic.gdx.physics.box2d.BodyDef
import com.badlogic.gdx.physics.box2d.Box2D
import com.badlogic.gdx.physics.box2d.EdgeShape
import com.badlogic.gdx.physics.box2d.PolygonShape
import com.badlogic.gdx.physics.box2d.World
import com.example.hellodigitalis.bench.Bench
import com.example.hellodigitalis.hellobox2d.R

/**
 * Exercises the libGDX Box2D physics engine — a native (C++) collision-detection
 * and constraint-solver library — under Berberis ARM64->x86_64 translation. This
 * is HEADLESS: no GL, no rendering, just the native physics math. Box2D.init()
 * loads the arm64-v8a libgdx-box2d.so (plus its libgdx.so dependency); the probe
 * then builds a small world with a static ground and a dynamic box dropped from
 * y=10, steps the native solver 120 times under gravity, and self-checks that the
 * box actually fell and came to rest on the ground (didn't tunnel through),
 * driving Box2D's native broad-phase, narrow-phase, and integrator every step.
 * Logs "BOX2D OK" or "BOX2D FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runBox2DProbe()
        runBenchmarks()
    }

    private fun runBox2DProbe(): String {
        val msg = try {
            // First load the libGDX CORE native (libgdx.so) — it provides the
            // BufferUtils JNI peers that Box2D's native bindings call; Box2D.init()
            // alone only loads libgdx-box2d.so. Both are bionic-linked arm64-v8a
            // natives that run under Berberis ARM64->x86_64 translation.
            com.badlogic.gdx.utils.GdxNativesLoader.load()
            Box2D.init()

            // A world with earth-like downward gravity; allow bodies to sleep.
            val world = World(Vector2(0f, -10f), true)

            // Static ground: a horizontal edge along y=0 from x=-20 to x=+20.
            val groundDef = BodyDef().apply { type = BodyDef.BodyType.StaticBody }
            val ground: Body = world.createBody(groundDef)
            val edge = EdgeShape()
            edge.set(Vector2(-20f, 0f), Vector2(20f, 0f))
            ground.createFixture(edge, 0f)
            edge.dispose()

            // Dynamic box (0.5 x 0.5 half-extents) starting high at y=10.
            val startY = 10f
            val boxDef = BodyDef().apply {
                type = BodyDef.BodyType.DynamicBody
                position.set(0f, startY)
            }
            val box: Body = world.createBody(boxDef)
            val boxShape = PolygonShape()
            boxShape.setAsBox(0.5f, 0.5f)
            box.createFixture(boxShape, 1f)
            boxShape.dispose()

            // Step the native solver 120 frames at 60 Hz (2 simulated seconds),
            // ample time to fall ~10 units under -10 m/s^2 gravity and settle.
            val dt = 1f / 60f
            repeat(120) { world.step(dt, 6, 2) }

            val restY = box.position.y

            // It must have fallen substantially from y=10, and must have landed
            // on the ground (a 0.5 box half-height sits its center near y=0.5) —
            // a center well below ~0 would mean it tunneled through the edge.
            val fell = restY < startY - 5f
            val onGround = restY in -0.5f..2f

            world.dispose()

            if (fell && onGround) {
                "BOX2D OK (fell from $startY to $restY)"
            } else {
                "BOX2D FAIL: fell=$fell onGround=$onGround (from $startY to $restY)"
            }
        } catch (t: Throwable) {
            "BOX2D FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    /**
     * A physics step over many bodies: single-precision vector maths with a
     * branchy broad phase, stepped at a fixed timestep so every iteration does
     * identical work. This is the shape of a game's per-frame native cost.
     */
    private fun runBenchmarks() {
        val module = "hello-box2d"
        com.badlogic.gdx.utils.GdxNativesLoader.load()
        Box2D.init()
        val world = World(Vector2(0f, -10f), true)
        val groundBody = world.createBody(BodyDef().apply { type = BodyDef.BodyType.StaticBody })
        EdgeShape().apply {
            set(Vector2(-100f, 0f), Vector2(100f, 0f))
            groundBody.createFixture(this, 0f)
            dispose()
        }
        val box = PolygonShape().apply { setAsBox(0.5f, 0.5f) }
        for (i in 0 until 200) {
            val body = world.createBody(BodyDef().apply {
                type = BodyDef.BodyType.DynamicBody
                position.set((i % 20) * 1.1f - 10f, 2f + (i / 20) * 1.2f)
            })
            body.createFixture(box, 1f)
        }
        box.dispose()

        Bench.run(module, "step-200-bodies-x60", warmup = 3, iters = 15) {
            repeat(60) { world.step(1f / 60f, 8, 3) }
        }
        Bench.done(module)
    }

    companion object {
        private const val TAG = "HelloBox2d"
    }
}
