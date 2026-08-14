package com.example.hellodigitalis.hellosharedmem

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.example.hellodigitalis.statustest.StatusTestRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class StatusTest {
    @get:Rule
    val rule = StatusTestRule(
        "com.example.hellodigitalis.hellosharedmem/com.example.hellosharedmem.MainActivity",
        7000
    )

    @Test
    fun runsCleanly() {
        rule.assertRunsCleanly()
    }
}
