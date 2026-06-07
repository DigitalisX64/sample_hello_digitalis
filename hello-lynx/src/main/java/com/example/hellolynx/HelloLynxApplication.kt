package com.example.hellolynx

import android.app.Application
import com.lynx.tasm.LynxEnv

/**
 * Initialises the Lynx engine once per process. LynxEnv.init loads the native
 * Lynx/PrimJS libraries (arm64-v8a, translated by Berberis) and sets up the
 * shared JS runtime that every LynxView in the process uses.
 */
class HelloLynxApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        LynxEnv.inst().init(
            this,
            /* resourceProvider = */ null,
            /* templateProvider = */ null,
            /* behaviorBundle = */ null
        )
    }
}
