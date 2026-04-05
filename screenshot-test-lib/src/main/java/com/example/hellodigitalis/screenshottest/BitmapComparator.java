package com.example.hellodigitalis.screenshottest;

import android.graphics.Bitmap;
import android.graphics.Color;

public class BitmapComparator {

    /**
     * Compare two bitmaps pixel-by-pixel with RGBA absolute difference.
     * Returns match percentage (0.0 to 1.0) and a diff bitmap highlighting differences.
     */
    public static ComparisonResult compare(Bitmap actual, Bitmap reference) {
        // Scale to same dimensions if needed
        int width = reference.getWidth();
        int height = reference.getHeight();

        Bitmap scaledActual = actual;
        if (actual.getWidth() != width || actual.getHeight() != height) {
            scaledActual = Bitmap.createScaledBitmap(actual, width, height, true);
        }

        Bitmap diffBitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        long totalDiff = 0;
        long totalMax = (long) width * height * 4 * 255; // 4 channels, max 255 per channel

        int[] actualPixels = new int[width];
        int[] refPixels = new int[width];
        int[] diffPixels = new int[width];

        for (int y = 0; y < height; y++) {
            scaledActual.getPixels(actualPixels, 0, width, 0, y, width, 1);
            reference.getPixels(refPixels, 0, width, 0, y, width, 1);

            for (int x = 0; x < width; x++) {
                int ap = actualPixels[x];
                int rp = refPixels[x];

                int dr = Math.abs(Color.red(ap) - Color.red(rp));
                int dg = Math.abs(Color.green(ap) - Color.green(rp));
                int db = Math.abs(Color.blue(ap) - Color.blue(rp));
                int da = Math.abs(Color.alpha(ap) - Color.alpha(rp));

                totalDiff += dr + dg + db + da;

                // Diff bitmap: highlight differences in red, scale intensity
                int diffIntensity = Math.min(255, (dr + dg + db + da) * 2);
                if (diffIntensity > 0) {
                    diffPixels[x] = Color.argb(255, diffIntensity, 0, 0);
                } else {
                    // Faded version of original for context
                    diffPixels[x] = Color.argb(255,
                            Color.red(rp) / 3,
                            Color.green(rp) / 3,
                            Color.blue(rp) / 3);
                }
            }

            diffBitmap.setPixels(diffPixels, 0, width, 0, y, width, 1);
        }

        float matchPercentage = totalMax > 0 ? 1.0f - (float) totalDiff / totalMax : 1.0f;

        if (scaledActual != actual) {
            scaledActual.recycle();
        }

        return new ComparisonResult(matchPercentage, diffBitmap);
    }
}
