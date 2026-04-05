package com.example.hellodigitalis.screenshottest;

import android.app.Instrumentation;
import android.app.UiAutomation;
import android.content.ComponentName;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Bundle;
import android.util.Log;

import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.rules.TestRule;
import org.junit.runner.Description;
import org.junit.runners.model.Statement;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

public class ScreenshotTestRule implements TestRule {

    private static final String TAG = "ScreenshotTestRule";

    private final String componentName;
    private final long waitMs;
    private final float threshold;

    private String packageName;
    private UiAutomation uiAutomation;

    public ScreenshotTestRule(String componentName, long waitMs, float threshold) {
        this.componentName = componentName;
        this.waitMs = waitMs;
        this.threshold = threshold;
        this.packageName = componentName.split("/")[0];
    }

    @Override
    public Statement apply(Statement base, Description description) {
        return new Statement() {
            @Override
            public void evaluate() throws Throwable {
                Instrumentation instrumentation = InstrumentationRegistry.getInstrumentation();
                uiAutomation = instrumentation.getUiAutomation();
                base.evaluate();
            }
        };
    }

    /**
     * Launch the app, capture screenshot, compare against reference.
     * If instrumentation arg "updateReferences" is "true", saves capture as reference instead.
     */
    public void assertMatchesReference(String referenceAssetName) {
        Instrumentation instrumentation = InstrumentationRegistry.getInstrumentation();

        // Launch the activity via explicit Intent
        String[] parts = componentName.split("/");
        Intent intent = new Intent();
        intent.setComponent(new ComponentName(parts[0], parts[1]));
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        instrumentation.getContext().startActivity(intent);

        // Wait for rendering to stabilize
        try {
            Thread.sleep(waitMs);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }

        // Capture screenshot and crop out status bar and navigation bar
        // These contain dynamic content (clock, battery) that changes between runs
        Bitmap fullScreen = uiAutomation.takeScreenshot();
        assertNotNull("UiAutomation.takeScreenshot() returned null", fullScreen);
        Bitmap actual = cropSystemBars(fullScreen);
        fullScreen.recycle();

        // Check if we're in update-references mode
        Bundle args = InstrumentationRegistry.getArguments();
        boolean updateMode = "true".equals(args.getString("updateReferences"));

        if (updateMode) {
            saveReferenceImage(actual);
            actual.recycle();
            Log.i(TAG, "Reference image saved for " + packageName);
            return;
        }

        // Load reference image from test APK assets
        Bitmap reference = loadReferenceFromAssets(referenceAssetName);
        assertNotNull("Reference image not found in assets: " + referenceAssetName
                + ". Run with -e updateReferences true to generate.", reference);

        // Compare
        ComparisonResult result = BitmapComparator.compare(actual, reference);

        try {
            float requiredMatch = 1.0f - threshold;
            if (result.matchPercentage < requiredMatch) {
                // Save actual and diff for debugging
                String moduleName = packageName.replace(".", "_");
                saveBitmapToDevice(actual, moduleName, "actual.png");
                saveBitmapToDevice(result.diffBitmap, moduleName, "diff.png");

                fail(String.format(
                        "Screenshot mismatch for %s: %.2f%% match (required %.2f%%). "
                                + "Diff images saved to app data dir.",
                        packageName,
                        result.matchPercentage * 100,
                        requiredMatch * 100));
            }

            Log.i(TAG, String.format("Screenshot match for %s: %.2f%%",
                    packageName, result.matchPercentage * 100));
        } finally {
            // Always clean up bitmaps, even on assertion failure
            if (result.diffBitmap != null) {
                result.diffBitmap.recycle();
            }
            reference.recycle();
            actual.recycle();
        }
    }

    private Bitmap loadReferenceFromAssets(String assetName) {
        try {
            Instrumentation instrumentation = InstrumentationRegistry.getInstrumentation();
            InputStream is = instrumentation.getContext().getAssets().open("reference/" + assetName);
            Bitmap bmp = BitmapFactory.decodeStream(is);
            is.close();
            return bmp;
        } catch (IOException e) {
            Log.e(TAG, "Failed to load reference asset: " + assetName, e);
            return null;
        }
    }

    /**
     * Crop status bar (top) and navigation bar (bottom) from a screenshot.
     * These bars contain dynamic content (clock, battery, etc.) that changes between runs.
     * Uses display metrics to calculate bar heights in pixels.
     */
    private Bitmap cropSystemBars(Bitmap screenshot) {
        int width = screenshot.getWidth();
        int height = screenshot.getHeight();
        // Status bar: ~24dp, nav bar: ~48dp. At any density, calculate from display metrics.
        // Use conservative fixed percentages: top 4% (status bar), bottom 7% (nav bar)
        int statusBarHeight = (int) (height * 0.04);
        int navBarHeight = (int) (height * 0.07);
        int croppedHeight = height - statusBarHeight - navBarHeight;
        return Bitmap.createBitmap(screenshot, 0, statusBarHeight, width, croppedHeight);
    }

    /**
     * Get the output directory in the test app's data dir.
     * Files here are pullable via "adb shell run-as" or "adb pull" with root.
     */
    private File getOutputDir(String subdir) {
        File dir = new File(InstrumentationRegistry.getInstrumentation()
                .getTargetContext().getFilesDir(), subdir);
        dir.mkdirs();
        return dir;
    }

    private void saveReferenceImage(Bitmap bitmap) {
        String moduleName = packageName.replace(".", "_");
        File dir = getOutputDir("references");
        File file = new File(dir, "screenshot_default.png");
        saveBitmapToFile(bitmap, file);
        Log.i(TAG, "Reference saved to " + file.getAbsolutePath());
    }

    private void saveBitmapToDevice(Bitmap bitmap, String moduleName, String filename) {
        File dir = getOutputDir("screenshots");
        File file = new File(dir, filename);
        saveBitmapToFile(bitmap, file);
        Log.i(TAG, "Saved " + file.getAbsolutePath());
    }

    private void saveBitmapToFile(Bitmap bitmap, File file) {
        try (FileOutputStream fos = new FileOutputStream(file)) {
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, fos);
        } catch (IOException e) {
            Log.e(TAG, "Failed to save bitmap to " + file.getAbsolutePath(), e);
        }
    }

}
