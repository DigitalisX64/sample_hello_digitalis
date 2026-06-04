package com.example.hellodigitalis.helloreactnative

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.example.hellodigitalis.statustest.StatusTestRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

// Launches the React Native + Hermes activity and asserts it starts and runs
// without crashing under Berberis translation. The JS bundle exercises Hermes
// number/string parsing (parseInt/Number/JSON/RegExp -> bionic strtoull/
// strtoumax/strtod), the path NetEase Cloud Music's RN login hits.
@RunWith(AndroidJUnit4::class)
class StatusTest {
    @get:Rule
    val rule = StatusTestRule(
        "com.example.hellodigitalis.helloreactnative/com.example.helloreactnative.MainActivity",
        8000  // React Native + Hermes cold start needs extra settle time.
    )

    @Test
    fun runsCleanly() {
        rule.assertRunsCleanly()
    }
}
