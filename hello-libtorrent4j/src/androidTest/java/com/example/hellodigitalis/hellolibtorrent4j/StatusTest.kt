package com.example.hellodigitalis.hellolibtorrent4j

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.example.hellodigitalis.statustest.StatusTestRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class StatusTest {
    @get:Rule
    val rule = StatusTestRule(
        "com.example.hellodigitalis.hellolibtorrent4j/com.example.hellolibtorrent4j.MainActivity",
        12000
    )

    @Test
    fun runsCleanly() {
        rule.assertRunsCleanly()
    }
}
