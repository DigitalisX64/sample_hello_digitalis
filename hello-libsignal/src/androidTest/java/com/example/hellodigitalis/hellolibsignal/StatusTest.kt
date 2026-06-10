package com.example.hellodigitalis.hellolibsignal

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.example.hellodigitalis.statustest.StatusTestRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class StatusTest {
    @get:Rule
    val rule = StatusTestRule(
        "com.example.hellodigitalis.hellolibsignal/com.example.hellolibsignal.MainActivity",
        5000
    )

    @Test
    fun runsCleanly() {
        rule.assertRunsCleanly()
    }
}
