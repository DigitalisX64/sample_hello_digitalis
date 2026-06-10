package com.example.hellotracingperfetto;

import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.tracing.Trace;
import androidx.tracing.perfetto.jni.PerfettoNative;

import com.example.hellodigitalis.hellotracingperfetto.R;

/**
 * Exercises the AndroidX Perfetto SDK tracing native binary under Berberis
 * ARM64->x86_64 translation.
 *
 * The tracing-perfetto-binary AAR ships an arm64-v8a libtracing_perfetto.so;
 * the probe loads it through PerfettoNative and then calls its JNI entrypoints
 * (nativeVersion, nativeRegisterWithPerfetto, nativeTraceEventBegin/End) plus
 * the androidx.tracing Trace facade. The key assertion is that the native .so
 * loaded and its registered JNI symbols resolved and ran under translation.
 *
 * PerfettoNative is declared Kotlin {@code internal}, which is enforced only
 * for Kotlin callers — Kotlin {@code internal} compiles to JVM {@code public},
 * so this Java activity can reach it. (PerfettoSdkTrace.enable() is also
 * internal and its full enable flow needs an external am-broadcast handshake to
 * TracingReceiver, which can't run headlessly; loading the binary and driving
 * its JNI surface is the path the translator actually has to support.)
 */
public class MainActivity extends AppCompatActivity {

    private static final String TAG = "HelloTracingPerfetto";

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        ((TextView) findViewById(R.id.sample_text)).setText(runTracingProbe());
    }

    private String runTracingProbe() {
        String msg;
        try {
            // Load arm64-v8a libtracing_perfetto.so — the System.loadLibrary
            // path the translator must support.
            PerfettoNative.INSTANCE.loadLib();

            // Calling a registered JNI native method proves the .so loaded and
            // its symbols resolved + executed under translation.
            String nativeVersion = PerfettoNative.nativeVersion();
            String expectedVersion = PerfettoNative.Metadata.version;
            if (nativeVersion == null || nativeVersion.isEmpty()) {
                throw new IllegalStateException("nativeVersion() returned empty");
            }

            // Register the SDK with Perfetto (in-process), then emit a native
            // trace event plus the androidx.tracing Trace facade calls.
            PerfettoNative.nativeRegisterWithPerfetto();

            Trace.beginSection("hello-tracing-perfetto");
            PerfettoNative.nativeTraceEventBegin(1, "probe-event");
            PerfettoNative.nativeTraceEventEnd();
            Trace.endSection();

            boolean versionsMatch = nativeVersion.equals(expectedVersion);
            msg = "TRACINGPERFETTO OK (nativeVersion=" + nativeVersion
                    + ", expected=" + expectedVersion + ", match=" + versionsMatch + ")";
        } catch (Throwable t) {
            msg = "TRACINGPERFETTO FAIL: " + t.getClass().getSimpleName() + ": " + t.getMessage();
        }
        Log.i(TAG, msg);
        return msg;
    }
}
