package com.example.hellodigitalis.statustest

import android.content.ComponentName
import android.content.Intent
import android.util.Log
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.rules.TestRule
import org.junit.runner.Description
import org.junit.runners.model.Statement
import java.io.FileInputStream

/**
 * Instrumentation rule for non-rendering samples (compute / status / callback).
 *
 * Unlike [ScreenshotTestRule] it does NOT pixel-compare a reference image —
 * these samples have no meaningful, stable visual output. Instead it launches
 * the activity, confirms the process actually started, lets the sample run its
 * probe / callbacks, and asserts the app's logcat contains no crash or
 * functional-failure marker.
 *
 * This is a genuine functional check, not just liveness: the self-checking
 * samples log "FAIL"/"FAILED" (and the ARM-extension probes a per-op "FAIL at")
 * or crash outright when the translator mishandles an instruction, syscall, or
 * proxy call, so a clean run with none of those markers means the status /
 * callback path worked as expected.
 *
 * Logcat is read through `UiAutomation.executeShellCommand`, which runs with the
 * shell identity (it can read and clear the log buffer); the buffer is drained
 * immediately before launch and the test runner force-stops the other sample
 * packages first, so the captured log reflects only this run.
 */
class StatusTestRule(
    private val componentName: String,
    private val waitMs: Long,
    private val extraFailureMarkers: List<String> = emptyList(),
) : TestRule {

    private val packageName: String = componentName.split("/")[0]
    private var uiAutomation: android.app.UiAutomation? = null

    override fun apply(base: Statement, description: Description): Statement {
        return object : Statement() {
            override fun evaluate() {
                uiAutomation = InstrumentationRegistry.getInstrumentation().uiAutomation
                base.evaluate()
            }
        }
    }

    private fun shell(cmd: String): String {
        val pfd = uiAutomation!!.executeShellCommand(cmd)
        FileInputStream(pfd.fileDescriptor).use { return it.readBytes().toString(Charsets.UTF_8) }
    }

    /**
     * Launch the activity, wait for its probe to run, and assert it completed
     * without a crash or self-reported failure.
     */
    fun assertRunsCleanly() {
        val parts = componentName.split("/")

        // Drain the log buffer so we only inspect this run. NOTE: do NOT
        // force-stop packageName — androidTest instrumentation runs inside the
        // target app's own process, so stopping it would kill this test
        // ("Process crashed"). The runner force-stops the *other* samples.
        shell("logcat -c")

        val intent = Intent().apply {
            component = ComponentName(parts[0], parts[1])
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        InstrumentationRegistry.getInstrumentation().context.startActivity(intent)

        // Confirm the process actually started.
        var pid = ""
        val startDeadline = System.currentTimeMillis() + 5000
        while (System.currentTimeMillis() < startDeadline) {
            // pidof can return several space-separated pids; take the first.
            pid = shell("pidof $packageName").trim().split(Regex("\\s+")).firstOrNull().orEmpty()
            if (pid.isNotEmpty()) break
            Thread.sleep(200)
        }
        assertTrue("Activity $componentName never started (no pid)", pid.isNotEmpty())

        // Let the sample run its probe / deliver callbacks.
        Thread.sleep(waitMs)

        // Scope the log read to this app's pid so unrelated system/runner lines
        // (which routinely contain words like "FAIL") can't trip the markers.
        // A native crash that kills the process still logs "Fatal signal …"
        // under that pid, and berberis logs "Undefined arm64 instruction …" in
        // the guest process, so both remain visible here.
        val log = shell("logcat -d -v brief --pid=$pid")
        val markers = DEFAULT_FAILURE_MARKERS + extraFailureMarkers
        for (m in markers) {
            if (log.contains(m)) {
                val line = log.lineSequence().firstOrNull { it.contains(m) }?.trim() ?: m
                fail("Sample $packageName logged failure marker \"$m\": $line")
            }
        }
        Log.i(TAG, "$packageName ran cleanly (pid=$pid, no failure markers)")
    }

    companion object {
        private const val TAG = "StatusTestRule"

        // Native crashes surface as "F libc : Fatal signal …"; the berberis
        // translator logs "Undefined arm64 instruction …" when it hits an opcode
        // it can't handle; Java/Kotlin crashes log "FATAL EXCEPTION"; and the
        // self-checking samples print "FAIL" (e.g. "FAIL at <tag>", "… FAILED.")
        // when a result is wrong.
        private val DEFAULT_FAILURE_MARKERS = listOf(
            "Fatal signal",
            "Undefined arm64 instruction",
            "FATAL EXCEPTION",
            "FAIL",
        )
    }
}
