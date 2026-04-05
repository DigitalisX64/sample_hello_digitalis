package com.example.hellodigitalis.sensorgraph;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.example.hellodigitalis.screenshottest.ScreenshotTestRule;

import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class ScreenshotTest {
    @Rule
    public ScreenshotTestRule rule = new ScreenshotTestRule(
            "com.example.hellodigitalis.sensorgraph/com.example.hellodigitalis.sensorgraph.AccelerometerGraphActivity",
            5000,
            0.10f
    );

    @Test
    public void screenshotMatchesReference() {
        rule.assertMatchesReference("screenshot_default.png");
    }
}
