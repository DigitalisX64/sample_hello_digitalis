package com.example.helloreactnative

import android.app.Application
import com.facebook.react.ReactApplication
import com.facebook.react.ReactNativeHost
import com.facebook.react.ReactPackage
import com.facebook.react.defaults.DefaultReactNativeHost
import com.facebook.react.internal.featureflags.ReactNativeFeatureFlags
import com.facebook.react.internal.featureflags.ReactNativeFeatureFlagsDefaults
import com.facebook.react.shell.MainReactPackage
import com.facebook.react.soloader.OpenSourceMergedSoMapping
import com.facebook.soloader.SoLoader

class MainApplication : Application(), ReactApplication {
    override val reactNativeHost: ReactNativeHost =
        object : DefaultReactNativeHost(this) {
            override fun getUseDeveloperSupport(): Boolean = false
            override fun getPackages(): List<ReactPackage> = listOf(MainReactPackage())
            override fun getJSMainModuleName(): String = "index"
            override fun getBundleAssetName(): String = "index.android.bundle"
            override val isNewArchEnabled: Boolean = false
            override val isHermesEnabled: Boolean = true
        }

    override fun onCreate() {
        super.onCreate()
        // RN 0.79 defaults to the bridgeless (new) architecture, which needs
        // codegen'd TurboModule registration (the RN gradle plugin). This sample
        // stays on the classic bridge to keep the build self-contained, so the
        // bridgeless flag must be turned off before any RN init.
        SoLoader.init(this, OpenSourceMergedSoMapping)
        ReactNativeFeatureFlags.override(object : ReactNativeFeatureFlagsDefaults() {
            override fun enableBridgelessArchitecture(): Boolean = false
        })
    }
}
