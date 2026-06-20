package com.example.hellodigitalis.hellofilament

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.example.hellodigitalis.statustest.StatusTestRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class StatusTest {
    @get:Rule
    val rule = StatusTestRule(
        "com.example.hellodigitalis.hellofilament/com.example.hellofilament.MainActivity",
        10000
    )

    @Test
    fun runsCleanly() {
        rule.assertRunsCleanly()
    }
}
