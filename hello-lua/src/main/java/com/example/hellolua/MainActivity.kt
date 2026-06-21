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
package com.example.hellolua

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellolua.R
import party.iroiro.luajava.lua54.Lua54

/**
 * Exercises the native Lua 5.4 VM (via luajava) under Berberis ARM64->x86_64
 * translation. Constructing the Lua54 state loads the arm64-v8a liblua54.so
 * (bundled in the luajava android aar, lua54 classifier); running a Lua script
 * drives Lua's bytecode interpreter — a computed-goto / indirect-branch dispatch
 * loop — entirely as translated guest code. The probe runs a script that sums
 * 1..1000 and builds a repeated string, reads the results back out of the VM's
 * global table, and self-checks them, logging "LUA OK" or "LUA FAIL" so the
 * suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runLuaProbe()
    }

    private fun runLuaProbe(): String {
        val msg = try {
            // Constructing Lua54 loads the native liblua54.so and creates a
            // fresh lua_State. openLibraries() registers the standard library
            // (needed below for string.rep).
            val lua = Lua54()
            try {
                lua.openLibraries()

                // Real VM work: an arithmetic loop summing 1..1000 (expected
                // 500500) and a string-builder call. The results are written
                // into Lua globals so we can read them back across the JNI
                // boundary.
                lua.run(
                    """
                    local sum = 0
                    for i = 1, 1000 do sum = sum + i end
                    local s = string.rep("ab", 5)
                    result = sum
                    text = s
                    """.trimIndent()
                )

                // getGlobal pushes the global's value onto the Lua stack top
                // (index -1); read it, then pop to keep the stack balanced.
                lua.getGlobal("result")
                val result = lua.toInteger(-1)
                lua.pop(1)

                lua.getGlobal("text")
                val text = lua.toString(-1)
                lua.pop(1)

                val sumOk = result == 500500L
                val strOk = text == "ababababab"

                if (sumOk && strOk) {
                    "LUA OK (sum=500500, str=ababababab, Lua 5.4)"
                } else {
                    "LUA FAIL: sumOk=$sumOk strOk=$strOk " +
                        "(result=$result, text=$text)"
                }
            } finally {
                lua.close()
            }
        } catch (t: Throwable) {
            "LUA FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloLua"
    }
}
