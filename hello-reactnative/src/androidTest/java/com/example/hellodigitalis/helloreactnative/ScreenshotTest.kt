package com.example.hellodigitalis.helloreactnative

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.example.hellodigitalis.screenshottest.ScreenshotTestRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

// Launches the React Native + Hermes activity and pixel-compares the rendered
// screen against a committed reference. The JS bundle exercises Hermes
// number/string parsing (parseInt/Number/JSON/RegExp -> bionic strtoull/
// strtoumax/strtod), the exact path NetEase Cloud Music's RN login hits, and
// renders the deterministic result (acc/matches/json + "RN+Hermes parsing OK").
// This guards against the FCSEL-class interpreter bug that made number parsing
// return 0 and collapsed the layout to a blank screen.
@RunWith(AndroidJUnit4::class)
class ScreenshotTest {
    @get:Rule
    val rule = ScreenshotTestRule(
        "com.example.hellodigitalis.helloreactnative/com.example.helloreactnative.MainActivity",
        9000,  // React Native + Hermes cold start needs extra settle time.
        0.05f
    )

    @Test
    fun screenshotMatchesReference() {
        rule.assertMatchesReference("screenshot_default.png")
    }
}
