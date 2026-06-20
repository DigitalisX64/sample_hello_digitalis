package com.example.hellodigitalis.hellosecp256k1

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.example.hellodigitalis.statustest.StatusTestRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class StatusTest {
    @get:Rule
    val rule = StatusTestRule(
        "com.example.hellodigitalis.hellosecp256k1/com.example.hellosecp256k1.MainActivity",
        8000
    )

    @Test
    fun runsCleanly() {
        rule.assertRunsCleanly()
    }
}
