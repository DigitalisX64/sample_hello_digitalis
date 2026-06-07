package com.example.hellolynx

import android.os.Bundle
import android.view.ViewGroup
import androidx.appcompat.app.AppCompatActivity
import com.lynx.tasm.LynxView
import com.lynx.tasm.LynxViewBuilder

/**
 * Hosts a single LynxView that renders the prebuilt `main.lynx.bundle`. Reaching
 * a rendered Lynx page means the native Lynx engine and the PrimJS JavaScript
 * runtime loaded and executed correctly under Berberis ARM64->x86_64 translation.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val lynxView = buildLynxView()
        lynxView.layoutParams = ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        )
        setContentView(lynxView)
        lynxView.renderTemplateUrl(TEMPLATE_URI, "")
    }

    private fun buildLynxView(): LynxView {
        val builder = LynxViewBuilder()
        builder.setTemplateProvider(AssetsTemplateProvider(this))
        return builder.build(this)
    }

    companion object {
        private const val TEMPLATE_URI = "main.lynx.bundle"
    }
}
