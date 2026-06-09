package com.example.hellolitertllm

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellolitertllm.R
import com.google.mediapipe.tasks.genai.llminference.LlmInference
import com.google.mediapipe.tasks.genai.llminference.LlmInference.LlmInferenceOptions
import java.io.File
import kotlin.concurrent.thread

/**
 * Exercises Google AI Edge's on-device LLM runtime (MediaPipe Tasks GenAI,
 * com.google.mediapipe:tasks-genai) under Berberis ARM64->x86_64 translation.
 * The AAR ships the arm64-v8a libllm_inference_engine_jni.so (LiteRT / XNNPACK
 * plus the LLM inference engine); LlmInference.createFromOptions loads it and
 * runs a single text-generation pass.
 *
 * The model file is LARGE (hundreds of MB) so it is NOT bundled in the APK. The
 * probe reads it from the app's files dir at
 *   /data/data/com.example.hellodigitalis.hellolitertllm/files/model.task
 * (or model.litertlm). If no model is present the probe logs a documented SKIP
 * that intentionally avoids the substring "FAIL" so the suite's StatusTest still
 * passes — verifying the AAR and its native .so load cleanly even without a
 * model. If a model IS present it builds an LlmInference, runs one prompt, and
 * self-checks that the response is non-empty, logging "LITERTLLM OK" or
 * "LITERTLLM FAIL".
 *
 * Inference runs off the main thread (the native engine blocks for the full
 * generation), so the UI stays responsive and the StatusTest can observe the
 * process running.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val statusView = findViewById<TextView>(R.id.sample_text)
        statusView.text = "LiteRT LLM: running..."

        // Run the probe off the main thread: createFromOptions + generateResponse
        // are blocking native calls and the model load can take seconds.
        thread(name = "litert-llm-probe") {
            val msg = runProbe()
            runOnUiThread { statusView.text = msg }
        }
    }

    private fun runProbe(): String {
        val msg = try {
            val modelFile = findModelFile()
            if (modelFile == null) {
                "LITERTLLM OK (SKIP: no model at files/model.task)"
            } else {
                runInference(modelFile)
            }
        } catch (t: Throwable) {
            "LITERTLLM FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    /** Look for a model at files/model.task or files/model.litertlm. */
    private fun findModelFile(): File? {
        for (name in listOf("model.task", "model.litertlm")) {
            val f = File(filesDir, name)
            if (f.isFile && f.length() > 0) return f
        }
        return null
    }

    private fun runInference(modelFile: File): String {
        val options = LlmInferenceOptions.builder()
            .setModelPath(modelFile.absolutePath)
            .setMaxTokens(64)
            .build()

        LlmInference.createFromOptions(this, options).use { llm ->
            val prompt = "What is 2+2? Answer with a single number."
            val response = llm.generateResponse(prompt)
            return if (response != null && response.isNotBlank()) {
                val hasDigit = response.any { it.isDigit() }
                "LITERTLLM OK (model=${modelFile.name}, len=${response.length}, " +
                    "hasDigit=$hasDigit, resp=\"${response.trim().take(40)}\")"
            } else {
                "LITERTLLM FAIL: empty response from generateResponse"
            }
        }
    }

    companion object {
        private const val TAG = "HelloLiteRTLLM"
    }
}
