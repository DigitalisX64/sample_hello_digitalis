package com.example.helloquickjs

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import app.cash.quickjs.QuickJs
import com.example.hellodigitalis.helloquickjs.R

/**
 * Exercises Cash App's QuickJS — a native (C) JavaScript engine — under Berberis
 * ARM64->x86_64 translation. QuickJs.create() loads the arm64-v8a libquickjs.so
 * and spins up a JS context; the probe evaluates a couple of JavaScript snippets
 * through the native bytecode interpreter and self-checks the numeric results,
 * logging "QUICKJS OK" or "QUICKJS FAIL" so the suite's StatusTest can assert a
 * clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runQuickJsProbe()
    }

    private fun runQuickJsProbe(): String {
        val msg = try {
            val qjs = QuickJs.create()
            try {
                // evaluate() returns java.lang.Object; a numeric JS result comes
                // back as a boxed Integer or Double, so normalize through Number.
                val arith = (qjs.evaluate("var a = 2; var b = 40; a + b;") as Number).toInt()
                val fn = (qjs.evaluate("(function () { return 6 * 7; })();") as Number).toInt()

                val checks = listOf(
                    "arith" to (arith == 42),
                    "fn" to (fn == 42),
                )
                val failed = checks.filterNot { it.second }.map { it.first }
                if (failed.isEmpty()) {
                    "QUICKJS OK (arith=$arith, fn=$fn)"
                } else {
                    "QUICKJS FAIL: result mismatch for ${failed.joinToString(",")}"
                }
            } finally {
                qjs.close()
            }
        } catch (t: Throwable) {
            "QUICKJS FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloQuickJS"
    }
}
