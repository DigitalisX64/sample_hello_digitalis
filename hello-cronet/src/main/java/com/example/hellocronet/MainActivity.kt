package com.example.hellocronet

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellocronet.R
import org.chromium.net.CronetEngine
import org.chromium.net.CronetException
import org.chromium.net.UrlRequest
import org.chromium.net.UrlResponseInfo
import java.nio.ByteBuffer
import java.util.concurrent.Executors

// Minimal Cronet (Chromium net stack) HTTPS probe. Cronet's ARM64 native
// library runs under Berberis translation, so a request through it exercises
// the same guest-side URL parsing + TLS path that fails inside full apps such
// as com.netease.cloudmusic (net::ERR_INVALID_URL / ERR_SSL_PROTOCOL_ERROR for
// valid URLs). This is a small, fast reproduction harness for that bug.
class MainActivity : AppCompatActivity() {

    private val tag = "hellocronet"
    private val results = StringBuilder()
    @Volatile private var done = 0
    private var engine: CronetEngine? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val view = findViewById<TextView>(R.id.sample_text)
        view.text = "Cronet probe running…"

        val engine = CronetEngine.Builder(this).build()
        this.engine = engine
        Log.i(tag, "Cronet version: ${engine.versionString}")
        results.append("Cronet ${engine.versionString}\n")
        // Capture a NetLog so the precise SSL handshake failure (BoringSSL
        // error / TLS alert) is recorded for diagnosis.
        runCatching {
            val f = java.io.File(filesDir, "netlog.json")
            engine.startNetLogToFile(f.absolutePath, true)
            Log.i(tag, "netlog -> ${f.absolutePath}")
        }
        val executor = Executors.newSingleThreadExecutor()

        // Plain, unambiguously-valid HTTPS URLs that drive a real TLS handshake
        // (incl. a long eapi-style URL with a big hex query param). A correct
        // translation returns an HTTP status; the TLS-under-translation bug made
        // every one fail with ERR_SSL_PROTOCOL_ERROR (BoringSSL signature verify
        // via the bignum carry path). They all succeed once that is fixed.
        val urls = listOf(
            "https://www.google.com/generate_204",
            "https://interface3.music.163.com/",
            "https://interface3.music.163.com/eapi/resource-exposure/config?" +
                "resourcePosition=login_new&fromPage=RN&rnBundleName=new-rn-login&" +
                "checkToken=9ca17ae2e6fbcda170e2e6ee82d15a8b9899a9d75b9bac8eb2d85e8" +
                "68e9b83d23d8f888db6e94b898aa8b6b52af0feaec3b92a918fc090b558ed8a9a92b55a"
        )

        for (url in urls) {
            val cb = object : UrlRequest.Callback() {
                private val sink = ByteBuffer.allocateDirect(16 * 1024)
                override fun onRedirectReceived(r: UrlRequest, i: UrlResponseInfo?, newUrl: String) =
                    r.followRedirect()
                override fun onResponseStarted(r: UrlRequest, i: UrlResponseInfo) = r.read(sink)
                override fun onReadCompleted(r: UrlRequest, i: UrlResponseInfo, b: ByteBuffer) {
                    b.clear(); r.read(b)
                }
                override fun onSucceeded(r: UrlRequest, i: UrlResponseInfo) =
                    record(url, "OK http=${i.httpStatusCode}", view, urls.size)
                override fun onFailed(r: UrlRequest, i: UrlResponseInfo?, e: CronetException) =
                    record(url, "FAIL ${e.message}", view, urls.size)
                override fun onCanceled(r: UrlRequest, i: UrlResponseInfo?) =
                    record(url, "CANCELED", view, urls.size)
            }
            engine.newUrlRequestBuilder(url, cb, executor).build().start()
        }
    }

    private fun record(url: String, outcome: String, view: TextView, total: Int) {
        val line = "  $url -> $outcome"
        Log.i(tag, line)
        val text: String
        synchronized(results) {
            results.append(line).append('\n')
            done++
            text = results.toString()
        }
        runOnUiThread { view.text = text }
        if (done == total) {
            runCatching { engine?.stopNetLog() }
            Log.i(tag, "Cronet probe complete:\n$text")
        }
    }
}
