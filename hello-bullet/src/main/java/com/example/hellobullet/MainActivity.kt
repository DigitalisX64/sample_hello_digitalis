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

package com.example.hellobullet

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.badlogic.gdx.math.Matrix4
import com.badlogic.gdx.math.Vector3
import com.badlogic.gdx.physics.bullet.Bullet
import com.badlogic.gdx.physics.bullet.collision.btCollisionDispatcher
import com.badlogic.gdx.physics.bullet.collision.btDbvtBroadphase
import com.badlogic.gdx.physics.bullet.collision.btDefaultCollisionConfiguration
import com.badlogic.gdx.physics.bullet.collision.btSphereShape
import com.badlogic.gdx.physics.bullet.dynamics.btDiscreteDynamicsWorld
import com.badlogic.gdx.physics.bullet.dynamics.btRigidBody
import com.badlogic.gdx.physics.bullet.dynamics.btSequentialImpulseConstraintSolver
import com.badlogic.gdx.physics.bullet.linearmath.btDefaultMotionState
import com.example.hellodigitalis.hellobullet.R

/**
 * Exercises the Bullet rigid-body physics engine (a native C++ library) via
 * libGDX's JNI bindings under Berberis ARM64->x86_64 translation. Bullet.init()
 * loads the vendored arm64-v8a natives (libgdx.so, libgdx-bullet.so via
 * System.loadLibrary); the probe builds a headless btDiscreteDynamicsWorld (no
 * GL surface), drops a dynamic sphere from y=50, steps the simulation for two
 * seconds of sim time, and self-checks that the body actually fell under
 * gravity. Stepping the world drives Bullet's collision broadphase and
 * sequential-impulse constraint solver — heavy FP/SIMD math — through the
 * translator, logging "BULLET OK" or "BULLET FAIL" so the suite's StatusTest can
 * assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runBulletProbe()
    }

    private fun runBulletProbe(): String {
        val msg = try {
            // Loads the native Bullet libraries (libgdx.so, libgdx-bullet.so).
            Bullet.init()

            val collisionConfig = btDefaultCollisionConfiguration()
            val dispatcher = btCollisionDispatcher(collisionConfig)
            val broadphase = btDbvtBroadphase()
            val solver = btSequentialImpulseConstraintSolver()
            val world = btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfig)
            world.gravity = Vector3(0f, -10f, 0f)

            // A dynamic (mass > 0) sphere starting high up.
            val shape = btSphereShape(1f)
            val localInertia = Vector3(0f, 0f, 0f)
            shape.calculateLocalInertia(1f, localInertia)
            val startTransform = Matrix4()
            startTransform.setToTranslation(0f, 50f, 0f)
            val motionState = btDefaultMotionState(startTransform)
            val body = btRigidBody(1f, motionState, shape, localInertia)
            world.addRigidBody(body)

            val startY = body.worldTransform.getTranslation(Vector3()).y
            // 120 steps at 1/60 s = 2 s of simulated time.
            repeat(120) { world.stepSimulation(1f / 60f, 10) }
            val endY = body.worldTransform.getTranslation(Vector3()).y

            val fell = endY < startY - 1f

            val result = if (fell) {
                String.format("BULLET OK (fell from y=%.1f to y=%.1f over 2s sim)", startY, endY)
            } else {
                String.format(
                    "BULLET FAIL: body did not fall (y=%.1f to y=%.1f, expected drop)",
                    startY, endY
                )
            }

            // Release the native Bullet objects we created (Matrix4/Vector3 are
            // pure-Java and need no disposal).
            world.removeRigidBody(body)
            body.dispose()
            motionState.dispose()
            shape.dispose()
            world.dispose()
            solver.dispose()
            broadphase.dispose()
            dispatcher.dispose()
            collisionConfig.dispose()

            result
        } catch (t: Throwable) {
            "BULLET FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloBullet"
    }
}
