package com.example.helloappsearch

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.appsearch.app.AppSearchSchema
import androidx.appsearch.app.AppSearchSession
import androidx.appsearch.app.GenericDocument
import androidx.appsearch.app.PutDocumentsRequest
import androidx.appsearch.app.SearchSpec
import androidx.appsearch.app.SetSchemaRequest
import androidx.appsearch.localstorage.LocalStorage
import com.example.hellodigitalis.helloappsearch.R

/**
 * Exercises AndroidX AppSearch with local storage under Berberis
 * ARM64->x86_64 translation. LocalStorage.createSearchSessionAsync brings up
 * the arm64-v8a native Icing engine (libicing.so); the probe registers a
 * schema, indexes one document, runs a full-text query, and self-checks that
 * exactly one result comes back with the expected id and indexed text.
 *
 * It uses AppSearch's schema-less [GenericDocument] / [AppSearchSchema] API
 * rather than annotated @Document classes, so no annotation processor (kapt)
 * is required — every put/search call still crosses into native Icing, which
 * is what this sample is here to exercise.
 *
 * AppSearch's APIs return Guava ListenableFutures that must be resolved with
 * .get(), which blocks — so the whole probe runs on a background thread and
 * posts its "APPSEARCH OK"/"APPSEARCH FAIL" result (logged for the suite's
 * StatusTest) back to the TextView on the main thread.
 */
class MainActivity : AppCompatActivity() {

    private lateinit var sampleText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        sampleText = findViewById(R.id.sample_text)
        sampleText.text = "AppSearch: running…"

        Thread {
            val msg = runAppSearchProbe()
            Log.i(TAG, msg)
            runOnUiThread { sampleText.text = msg }
        }.start()
    }

    private fun runAppSearchProbe(): String {
        var session: AppSearchSession? = null
        return try {
            val context = LocalStorage.SearchContext.Builder(this, DB_NAME).build()
            session = LocalStorage.createSearchSessionAsync(context).get()

            // Declare a "Note" schema with two full-text indexed string
            // properties. force-override keeps re-runs deterministic if the
            // schema already exists from a prior launch.
            val noteSchema = AppSearchSchema.Builder(SCHEMA_TYPE)
                .addProperty(
                    AppSearchSchema.StringPropertyConfig.Builder("title")
                        .setCardinality(AppSearchSchema.StringPropertyConfig.CARDINALITY_OPTIONAL)
                        .setIndexingType(AppSearchSchema.StringPropertyConfig.INDEXING_TYPE_PREFIXES)
                        .setTokenizerType(AppSearchSchema.StringPropertyConfig.TOKENIZER_TYPE_PLAIN)
                        .build()
                )
                .addProperty(
                    AppSearchSchema.StringPropertyConfig.Builder("body")
                        .setCardinality(AppSearchSchema.StringPropertyConfig.CARDINALITY_OPTIONAL)
                        .setIndexingType(AppSearchSchema.StringPropertyConfig.INDEXING_TYPE_PREFIXES)
                        .setTokenizerType(AppSearchSchema.StringPropertyConfig.TOKENIZER_TYPE_PLAIN)
                        .build()
                )
                .build()
            val setSchemaRequest = SetSchemaRequest.Builder()
                .addSchemas(noteSchema)
                .setForceOverride(true)
                .build()
            session.setSchemaAsync(setSchemaRequest).get()

            // Index one document built directly as a GenericDocument.
            val title = "Digitalis release notes"
            val body = "AppSearch runs on the native icing engine via Berberis."
            val doc = GenericDocument.Builder<GenericDocument.Builder<*>>(NAMESPACE, DOC_ID, SCHEMA_TYPE)
                .setPropertyString("title", title)
                .setPropertyString("body", body)
                .build()
            val putResult = session.putAsync(
                PutDocumentsRequest.Builder().addGenericDocuments(doc).build()
            ).get()
            if (!putResult.isSuccess) {
                return "APPSEARCH FAIL: put failed: ${putResult.failures}"
            }

            // Full-text query for a term in the document body.
            val spec = SearchSpec.Builder()
                .setTermMatch(SearchSpec.TERM_MATCH_PREFIX)
                .build()
            val results = session.search("icing", spec)
            val page = results.getNextPageAsync().get()
            results.close()

            if (page.size != 1) {
                return "APPSEARCH FAIL: expected 1 result, got ${page.size}"
            }
            val found = page[0].genericDocument
            if (found.id != DOC_ID) {
                return "APPSEARCH FAIL: wrong id ${found.id}, expected $DOC_ID"
            }
            val foundTitle = found.getPropertyString("title")
            val foundBody = found.getPropertyString("body")
            if (found.namespace != NAMESPACE || foundTitle != title || foundBody != body) {
                return "APPSEARCH FAIL: round-trip mismatch for ${found.id}"
            }

            "APPSEARCH OK (db=$DB_NAME, id=${found.id}, title=\"$foundTitle\")"
        } catch (t: Throwable) {
            Log.e(TAG, "AppSearch probe threw", t)
            "APPSEARCH FAIL: ${t.javaClass.simpleName}: ${t.message}"
        } finally {
            session?.close()
        }
    }

    companion object {
        private const val TAG = "HelloAppSearch"
        private const val DB_NAME = "hello-appsearch-db"
        private const val NAMESPACE = "notes"
        private const val DOC_ID = "note-1"
        private const val SCHEMA_TYPE = "Note"
    }
}
