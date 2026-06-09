package com.example.hellodigitalis.helloijkplayer

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.example.hellodigitalis.statustest.StatusTestRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class StatusTest {
    // Decode runs asynchronously and the probe waits up to 8s for playback
    // callbacks, so give it a generous window before the log is inspected.
    @get:Rule
    val rule = StatusTestRule(
        "com.example.hellodigitalis.helloijkplayer/com.example.helloijkplayer.MainActivity",
        12000
    )

    @Test
    fun runsCleanly() {
        rule.assertRunsCleanly()
    }
}
