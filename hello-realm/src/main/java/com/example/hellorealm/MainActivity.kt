package com.example.hellorealm

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellorealm.R
import io.realm.kotlin.Realm
import io.realm.kotlin.RealmConfiguration
import io.realm.kotlin.ext.query
import kotlin.concurrent.thread

/**
 * Exercises Realm Kotlin — an on-device object database backed by a native
 * (C/C++) core — under Berberis ARM64->x86_64 translation. Opening a Realm loads
 * the arm64-v8a librealmc.so; the probe writes an Item, queries it back, and
 * self-checks the round-trip, logging "REALM OK" or "REALM FAIL" so the suite's
 * StatusTest can assert a clean run.
 *
 * RealmConfiguration.create / writeBlocking / query().find() are blocking calls,
 * so the probe runs on a background thread and posts its result to the TextView.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val text = findViewById<TextView>(R.id.sample_text)
        thread {
            val msg = runRealmProbe()
            runOnUiThread { text.text = msg }
        }
    }

    private fun runRealmProbe(): String {
        return try {
            // Use a fresh in-app file and delete-if-migration so re-runs are
            // deterministic; the schema is just the single Item model.
            val config = RealmConfiguration.Builder(schema = setOf(Item::class))
                .name("hello-realm.realm")
                .deleteRealmIfMigrationNeeded()
                .build()
            val realm = Realm.open(config)
            try {
                // Start from a clean slate.
                realm.writeBlocking {
                    delete(query<Item>().find())
                }
                realm.writeBlocking {
                    copyToRealm(Item().apply {
                        id = 1
                        name = "realm-ⓦ"
                    })
                }

                val items = realm.query<Item>().find()
                val first = items.firstOrNull()
                val checks = listOf(
                    "count" to (items.size == 1),
                    "id" to (first?.id == 1),
                    "name" to (first?.name == "realm-ⓦ"),
                )

                val failed = checks.filterNot { it.second }.map { it.first }
                if (failed.isEmpty()) {
                    "REALM OK (schemaVersion=${config.schemaVersion}, count=${items.size}, name=${first?.name})"
                } else {
                    "REALM FAIL: round-trip mismatch for ${failed.joinToString(",")}"
                }
            } finally {
                realm.close()
            }
        } catch (t: Throwable) {
            "REALM FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }.also { Log.i(TAG, it) }
    }

    companion object {
        private const val TAG = "HelloRealm"
    }
}
