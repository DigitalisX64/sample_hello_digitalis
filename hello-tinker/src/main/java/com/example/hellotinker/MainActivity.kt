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
package com.example.hellotinker

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellotinker.R
import com.tencent.tinker.bsdiff.BSDiff
import com.tencent.tinker.bsdiff.BSPatch
import java.io.ByteArrayInputStream

/**
 * Exercises Tencent Tinker's runtime binary-patch primitive
 * (com.tencent.tinker.bsdiff.BSDiff / BSPatch) in the Digitalis environment.
 *
 * Tinker is the hot-fix framework real apps (e.g. NetEase Cloud Music) use to
 * ship dex/so/resource patches without a reinstall. BSDiff/BSPatch is the exact
 * code path an app runs when applying a downloaded patch to a bundled library or
 * resource file: BSDiff.bsdiff() computes a compact delta between an old and a
 * new buffer, and BSPatch.patchFast() reconstructs the new buffer from the old
 * one plus that delta. This probe drives both directions on in-memory buffers and
 * self-checks that the reconstruction is byte-exact AND that the delta is smaller
 * than a full copy (the diff actually found the common regions), logging
 * "TINKER OK" or "TINKER FAIL" so the suite's StatusTest can assert a clean run.
 *
 * Note: Tinker's published runtime artifacts implement BSDiff/BSPatch in pure
 * Java (no Tinker Maven artifact ships an arm64-v8a *.so), so this validates
 * Tinker's real patch algorithm running under Digitalis rather than translated
 * native machine code.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runTinkerProbe()
    }

    private fun runTinkerProbe(): String {
        val msg = try {
            // "old": the file as bundled in the base APK. Deterministic pseudo-
            // random content so the suffix-sort match finder in BSDiff has real,
            // non-repeating data to work on rather than a trivial run.
            val old = ByteArray(8192)
            var seed = 0x1234_5678
            for (i in old.indices) {
                seed = seed * 1103515245 + 12345
                old[i] = (seed ushr 16).toByte()
            }

            // "new": the patched file. Mostly identical to old (so BSDiff can
            // encode it as a small delta), with scattered single-byte edits plus
            // a fresh appended tail that has no match in old.
            val new = old.copyOf(old.size + 777)
            var i = 96
            while (i < old.size) {
                new[i] = (new[i].toInt() xor 0x5A).toByte()
                i += 128
            }
            var tailSeed = 0x0BADF00D
            for (j in old.size until new.size) {
                tailSeed = tailSeed * 1103515245 + 12345
                new[j] = (tailSeed ushr 16).toByte()
            }

            // Forward: compute the Tinker delta (old -> new).
            val diff = BSDiff.bsdiff(old, old.size, new, new.size)

            // Reverse: reconstruct new from old + delta, exactly as Tinker does
            // when applying a patch on-device.
            val patched = BSPatch.patchFast(
                ByteArrayInputStream(old),
                ByteArrayInputStream(diff)
            )

            val roundTripOk = patched != null && patched.contentEquals(new)
            // A real delta of a mostly-identical file must be far smaller than a
            // full copy of the target; this proves BSDiff matched the common
            // regions rather than emitting everything verbatim.
            val didDiff = diff != null && diff.isNotEmpty() && diff.size < new.size

            if (roundTripOk && didDiff) {
                "TINKER OK (bsdiff/bspatch: ${old.size}->${new.size} bytes, " +
                    "delta ${diff.size} bytes)"
            } else {
                "TINKER FAIL: roundTripOk=$roundTripOk didDiff=$didDiff " +
                    "(delta=${diff?.size} target=${new.size})"
            }
        } catch (t: Throwable) {
            "TINKER FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloTinker"
    }
}
