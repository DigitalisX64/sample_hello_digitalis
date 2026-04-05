package com.example.hellodigitalis.screenshottest;

import android.graphics.Bitmap;

public class ComparisonResult {
    public final float matchPercentage;
    public final Bitmap diffBitmap;

    public ComparisonResult(float matchPercentage, Bitmap diffBitmap) {
        this.matchPercentage = matchPercentage;
        this.diffBitmap = diffBitmap;
    }
}
