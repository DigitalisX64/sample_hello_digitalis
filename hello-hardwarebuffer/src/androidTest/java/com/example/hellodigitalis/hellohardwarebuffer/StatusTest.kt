package com.example.hellodigitalis.hellohardwarebuffer

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.example.hellodigitalis.statustest.StatusTestRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class StatusTest {
    // Normal probe time is well under a second; the budget covers the socket
    // section's 3 s socket timeouts + 5 s thread reap on its failure path, so
    // a timed-out FAIL is logged inside the window.
    @get:Rule
    val rule = StatusTestRule(
        "com.example.hellodigitalis.hellohardwarebuffer/com.example.hellohardwarebuffer.MainActivity",
        8000
    )

    @Test
    fun runsCleanly() {
        rule.assertRunsCleanly()
    }
}
