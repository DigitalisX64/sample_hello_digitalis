package com.example.hellolynx

import android.content.Context
import com.lynx.tasm.provider.AbsTemplateProvider
import java.io.IOException

/**
 * Resolves a Lynx template URL to the prebuilt `.lynx.bundle` shipped in the
 * APK's assets. The bundle is produced by the frontend/ ReactLynx project.
 */
class AssetsTemplateProvider(context: Context) : AbsTemplateProvider() {
    private val appContext = context.applicationContext

    override fun loadTemplate(uri: String, callback: Callback) {
        try {
            appContext.assets.open(uri).use { input ->
                callback.onSuccess(input.readBytes())
            }
        } catch (e: IOException) {
            callback.onFailed(e.message)
        }
    }
}
