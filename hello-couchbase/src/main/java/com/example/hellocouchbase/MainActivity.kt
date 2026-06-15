/*
 * Copyright (C) 2026 utzcoz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.example.hellocouchbase

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.couchbase.lite.CouchbaseLite
import com.couchbase.lite.Database
import com.couchbase.lite.MutableDocument
import com.example.hellodigitalis.hellocouchbase.R

/**
 * Exercises Couchbase Lite for Android — an embedded NoSQL document database
 * whose storage engine is the native (C++) LiteCore library — under Berberis
 * ARM64->x86_64 translation. Initializing Couchbase Lite and opening a Database
 * loads the arm64-v8a libLiteCore.so / libLiteCoreJNI.so; the probe then drives
 * the native storage engine end-to-end: it builds a MutableDocument carrying a
 * String and an Int property, saves it into the default collection (a JNI call
 * straight into LiteCore), reads the document back by id, and self-checks that
 * both round-tripped values match. It logs "COUCHBASE OK" or "COUCHBASE FAIL"
 * so the suite's StatusTest can assert a clean run, then deletes the database to
 * leave no on-disk state behind.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // One-time native init: loads libLiteCoreJNI.so + libLiteCore.so.
            CouchbaseLite.init(applicationContext)

            // Database names must be lowercase; "digitalis" qualifies.
            val db = Database("digitalis")
            try {
                val collection = db.defaultCollection

                val name = "digitalis"
                val answer = 42
                val doc = MutableDocument("probe-doc")
                    .setString("name", name)
                    .setInt("answer", answer)
                collection.save(doc)

                // Read it back through LiteCore by id.
                val read = collection.getDocument("probe-doc")

                val readName = read?.getString("name")
                val readAnswer = read?.getInt("answer")
                val stringOk = readName == name
                val intOk = readAnswer == answer

                if (read != null && stringOk && intOk) {
                    "COUCHBASE OK (LiteCore round-trip: name=\"$readName\" answer=$readAnswer)"
                } else {
                    "COUCHBASE FAIL: present=${read != null} stringOk=$stringOk " +
                        "intOk=$intOk (name=$readName answer=$readAnswer)"
                }
            } finally {
                // delete() closes the database and removes its on-disk files.
                db.delete()
            }
        } catch (t: Throwable) {
            "COUCHBASE FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloCouchbase"
    }
}
