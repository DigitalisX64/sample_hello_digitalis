package com.example.hellodigitalis.screenshottest

import android.content.ComponentName
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Log
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.rules.TestRule
import org.junit.runner.Description
import org.junit.runners.model.Statement
import java.io.File
import java.io.FileOutputStream
import java.io.IOException

class ScreenshotTestRule(
    private val componentName: String,
    private val waitMs: Long,
    private val threshold: Float
) : TestRule {

    private val packageName: String = componentName.split("/")[0]
    private var uiAutomation: android.app.UiAutomation? = null

    override fun apply(base: Statement, description: Description): Statement {
        return object : Statement() {
            override fun evaluate() {
                val instrumentation = InstrumentationRegistry.getInstrumentation()
                uiAutomation = instrumentation.uiAutomation
                base.evaluate()
            }
        }
    }

    /**
     * Launch the app, capture screenshot, compare against reference.
     * If instrumentation arg "updateReferences" is "true", saves capture as reference instead.
     */
    fun assertMatchesReference(referenceAssetName: String) {
        val instrumentation = InstrumentationRegistry.getInstrumentation()

        // Launch the activity via explicit Intent
        val parts = componentName.split("/")
        val intent = Intent().apply {
            component = ComponentName(parts[0], parts[1])
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        instrumentation.context.startActivity(intent)

        // Wait for rendering to stabilize
        try {
            Thread.sleep(waitMs)
        } catch (e: InterruptedException) {
            Thread.currentThread().interrupt()
        }

        // Capture screenshot and crop out status bar and navigation bar
        // These contain dynamic content (clock, battery) that changes between runs
        val fullScreen = uiAutomation!!.takeScreenshot()
        assertNotNull("UiAutomation.takeScreenshot() returned null", fullScreen)
        val actual = cropSystemBars(fullScreen)
        fullScreen.recycle()

        // Check if we're in update-references mode
        val args = InstrumentationRegistry.getArguments()
        val updateMode = "true" == args.getString("updateReferences")

        if (updateMode) {
            saveReferenceImage(actual)
            actual.recycle()
            Log.i(TAG, "Reference image saved for $packageName")
            return
        }

        // Load reference image from test APK assets
        val reference = loadReferenceFromAssets(referenceAssetName)
        assertNotNull(
            "Reference image not found in assets: $referenceAssetName" +
                ". Run with -e updateReferences true to generate.",
            reference
        )

        // Compare
        val result = BitmapComparator.compare(actual, reference!!)

        try {
            val requiredMatch = 1.0f - threshold
            if (result.matchPercentage < requiredMatch) {
                // Save actual and diff for debugging
                val moduleName = packageName.replace(".", "_")
                saveBitmapToDevice(actual, moduleName, "actual.png")
                saveBitmapToDevice(result.diffBitmap, moduleName, "diff.png")

                fail(
                    String.format(
                        "Screenshot mismatch for %s: %.2f%% match (required %.2f%%). " +
                            "Diff images saved to app data dir.",
                        packageName,
                        result.matchPercentage * 100,
                        requiredMatch * 100
                    )
                )
            }

            Log.i(
                TAG, String.format(
                    "Screenshot match for %s: %.2f%%",
                    packageName, result.matchPercentage * 100
                )
            )
        } finally {
            // Always clean up bitmaps, even on assertion failure
            result.diffBitmap.recycle()
            reference.recycle()
            actual.recycle()
        }
    }

    private fun loadReferenceFromAssets(assetName: String): Bitmap? {
        return try {
            val instrumentation = InstrumentationRegistry.getInstrumentation()
            val inputStream = instrumentation.context.assets.open("reference/$assetName")
            val bmp = BitmapFactory.decodeStream(inputStream)
            inputStream.close()
            bmp
        } catch (e: IOException) {
            Log.e(TAG, "Failed to load reference asset: $assetName", e)
            null
        }
    }

    /**
     * Crop status bar (top) and navigation bar (bottom) from a screenshot.
     * These bars contain dynamic content (clock, battery, etc.) that changes between runs.
     * Uses display metrics to calculate bar heights in pixels.
     */
    private fun cropSystemBars(screenshot: Bitmap): Bitmap {
        val width = screenshot.width
        val height = screenshot.height
        // Status bar: ~24dp, nav bar: ~48dp. At any density, calculate from display metrics.
        // Use conservative fixed percentages: top 4% (status bar), bottom 7% (nav bar)
        val statusBarHeight = (height * 0.04).toInt()
        val navBarHeight = (height * 0.07).toInt()
        val croppedHeight = height - statusBarHeight - navBarHeight
        return Bitmap.createBitmap(screenshot, 0, statusBarHeight, width, croppedHeight)
    }

    /**
     * Get the output directory in the test app's data dir.
     * Files here are pullable via "adb shell run-as" or "adb pull" with root.
     */
    private fun getOutputDir(subdir: String): File {
        val dir = File(
            InstrumentationRegistry.getInstrumentation()
                .targetContext.filesDir, subdir
        )
        dir.mkdirs()
        return dir
    }

    private fun saveReferenceImage(bitmap: Bitmap) {
        val moduleName = packageName.replace(".", "_")
        val dir = getOutputDir("references")
        val file = File(dir, "screenshot_default.png")
        saveBitmapToFile(bitmap, file)
        Log.i(TAG, "Reference saved to ${file.absolutePath}")
    }

    private fun saveBitmapToDevice(bitmap: Bitmap, moduleName: String, filename: String) {
        val dir = getOutputDir("screenshots")
        val file = File(dir, filename)
        saveBitmapToFile(bitmap, file)
        Log.i(TAG, "Saved ${file.absolutePath}")
    }

    private fun saveBitmapToFile(bitmap: Bitmap, file: File) {
        try {
            FileOutputStream(file).use { fos ->
                bitmap.compress(Bitmap.CompressFormat.PNG, 100, fos)
            }
        } catch (e: IOException) {
            Log.e(TAG, "Failed to save bitmap to ${file.absolutePath}", e)
        }
    }

    companion object {
        private const val TAG = "ScreenshotTestRule"
    }
}
