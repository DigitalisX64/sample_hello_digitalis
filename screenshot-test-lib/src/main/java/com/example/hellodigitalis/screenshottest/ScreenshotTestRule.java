package com.example.hellodigitalis.screenshottest;

import android.app.Instrumentation;
import android.app.UiAutomation;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
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
                try {
                    base.evaluate();
                } finally {
                    // Send HOME to dismiss the app. Don't use am force-stop because it
                    // kills the test process too (shared UID with instrumented app).
                    executeShellCommand("input keyevent KEYCODE_HOME");
                }
            }
        };
    }

    /**
     * Launch the app, capture screenshot, compare against reference.
     * If instrumentation arg "updateReferences" is "true", saves capture as reference instead.
     */
    public void assertMatchesReference(String referenceAssetName) {
        Instrumentation instrumentation = InstrumentationRegistry.getInstrumentation();

        // Launch the activity
        executeShellCommand("am start -n " + componentName);

        // Wait for rendering to stabilize
        try {
            Thread.sleep(waitMs);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }

        // Capture screenshot
        Bitmap actual = uiAutomation.takeScreenshot();
        assertNotNull("UiAutomation.takeScreenshot() returned null", actual);

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
                                + "Diff images saved to /data/local/tmp/screenshots/%s/",
                        packageName,
                        result.matchPercentage * 100,
                        requiredMatch * 100,
                        moduleName));
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

    private void saveReferenceImage(Bitmap bitmap) {
        String moduleName = packageName.replace(".", "_");
        String dirPath = "/data/local/tmp/references/" + moduleName;
        String filePath = dirPath + "/screenshot_default.png";
        saveBitmapViaShell(bitmap, dirPath, filePath);
        Log.i(TAG, "Reference saved to " + filePath);
    }

    private void saveBitmapToDevice(Bitmap bitmap, String moduleName, String filename) {
        String dirPath = "/data/local/tmp/screenshots/" + moduleName;
        String filePath = dirPath + "/" + filename;
        saveBitmapViaShell(bitmap, dirPath, filePath);
        Log.i(TAG, "Saved " + filePath);
    }

    /**
     * Save bitmap to device via a temp file + shell copy.
     * The test app process can't write to /data/local/tmp directly,
     * but UiAutomation shell commands run as shell user which can.
     */
    private void saveBitmapViaShell(Bitmap bitmap, String dirPath, String filePath) {
        try {
            // Write to app-private temp file first (use target context which has writable dirs)
            File cacheDir = InstrumentationRegistry.getInstrumentation()
                    .getTargetContext().getCacheDir();
            cacheDir.mkdirs();
            File tempFile = new File(cacheDir, "screenshot_tmp.png");
            try (FileOutputStream fos = new FileOutputStream(tempFile)) {
                bitmap.compress(Bitmap.CompressFormat.PNG, 100, fos);
            }
            // Use shell commands to mkdir and copy to /data/local/tmp
            executeShellCommand("mkdir -p " + dirPath);
            executeShellCommand("cp " + tempFile.getAbsolutePath() + " " + filePath);
            executeShellCommand("chmod 644 " + filePath);
            tempFile.delete();
        } catch (IOException e) {
            Log.e(TAG, "Failed to save bitmap to " + filePath, e);
        }
    }

    private void executeShellCommand(String command) {
        try {
            ParcelFileDescriptor pfd = uiAutomation.executeShellCommand(command);
            // Read and drain the output to ensure command completes
            InputStream is = new ParcelFileDescriptor.AutoCloseInputStream(pfd);
            byte[] buf = new byte[1024];
            while (is.read(buf) != -1) { /* drain */ }
            is.close();
        } catch (IOException e) {
            Log.e(TAG, "Shell command failed: " + command, e);
        }
    }
}
