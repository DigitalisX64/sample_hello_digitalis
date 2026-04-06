package com.example.hellodigitalis.screenshottest

import android.graphics.Bitmap

data class ComparisonResult(
    val matchPercentage: Float,
    val diffBitmap: Bitmap
)
