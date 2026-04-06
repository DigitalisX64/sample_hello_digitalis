package com.example.hellodigitalis.teapotsmore

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.example.hellodigitalis.screenshottest.ScreenshotTestRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ScreenshotTest {
    @get:Rule
    val rule = ScreenshotTestRule(
        "com.example.hellodigitalis.teapotsmore/com.sample.moreteapots.MoreTeapotsNativeActivity",
        5000,
        0.05f
    )

    @Test
    fun screenshotMatchesReference() {
        rule.assertMatchesReference("screenshot_default.png")
    }
}
