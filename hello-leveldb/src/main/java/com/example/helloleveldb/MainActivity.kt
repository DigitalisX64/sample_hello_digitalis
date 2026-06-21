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
package com.example.helloleveldb

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloleveldb.R
import com.github.lamba92.leveldb.LevelDB
import com.github.lamba92.leveldb.LevelDBOptions
import java.io.File

/**
 * Exercises LevelDB — Google's log-structured-merge (LSM) key-value store — under
 * Berberis ARM64->x86_64 translation. The first LevelDB call loads the arm64-v8a
 * libleveldb.so (+ libc++_shared.so) bundled in the kotlin-leveldb-android AAR.
 * The probe opens a fresh on-disk database in filesDir, puts three key/value
 * pairs (driving the skiplist memtable, SST writes, CRC32C and varint coding),
 * reads them back and verifies each value, deletes one key and verifies the
 * delete took effect while the others survive, then closes the DB. It logs
 * "LEVELDB OK" or "LEVELDB FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runLevelDbProbe()
    }

    private fun runLevelDbProbe(): String {
        val msg = try {
            // Use a fresh database directory so the run is deterministic: remove
            // any leftover from a previous launch before opening.
            val dbDir = File(filesDir, "leveldb-probe")
            dbDir.deleteRecursively()

            // createIfMissing defaults to true in LevelDBOptions, but be explicit.
            val db: LevelDB = LevelDB(
                dbDir.absolutePath,
                LevelDBOptions(createIfMissing = true)
            )

            val result = try {
                db.put("alpha", "one")
                db.put("beta", "two")
                db.put("gamma", "three")

                val a = db.get("alpha")
                val b = db.get("beta")
                val g = db.get("gamma")

                val putGetOk = a == "one" && b == "two" && g == "three"

                // Delete "beta"; it must vanish while alpha/gamma remain.
                db.delete("beta")
                val bAfter = db.get("beta")
                val aAfter = db.get("alpha")
                val gAfter = db.get("gamma")

                val deleteOk =
                    bAfter == null && aAfter == "one" && gAfter == "three"

                if (putGetOk && deleteOk) {
                    "LEVELDB OK (put/get/delete verified on 3 keys)"
                } else {
                    "LEVELDB " + "FAIL: putGetOk=$putGetOk deleteOk=$deleteOk " +
                        "(a=$a b=$b g=$g bAfter=$bAfter)"
                }
            } finally {
                db.close()
            }
            result
        } catch (t: Throwable) {
            "LEVELDB " + "FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloLeveldb"
    }
}
