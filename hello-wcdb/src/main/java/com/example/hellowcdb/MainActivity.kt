package com.example.hellowcdb

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellowcdb.R
import com.tencent.wcdb.database.SQLiteCipherSpec
import com.tencent.wcdb.database.SQLiteDatabase
import java.io.File

/**
 * Exercises Tencent WCDB (WeChat Database) — an encrypted SQLite engine — under
 * Berberis ARM64->x86_64 translation. WCDB's SQLiteGlobal static initializer
 * System.loadLibrary("wcdb")s the arm64-v8a libwcdb.so (SQLCipher/AES statically
 * linked over SQLite), so the first SQLiteDatabase touch loads and runs that
 * native engine under translation.
 *
 * This drives the WCDB 1.x API generation (com.tencent.wcdb.database.SQLiteDatabase,
 * modelled on android.database.sqlite.SQLiteDatabase, the SQLCipher-style API), not
 * the 2.x ORM. The probe opens an AES-encrypted database in the app's files dir
 * with a passphrase + SQLiteCipherSpec, creates a table, inserts four rows via
 * execSQL, runs a SELECT (rawQuery), and self-checks the read-back row count, an
 * integer SUM, and a string column against known-correct values, logging
 * "WCDB OK" or "WCDB FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Fresh, encrypted database file in the app's private files dir.
            val dbFile = File(filesDir, "hello-wcdb.db")
            SQLiteDatabase.deleteDatabase(dbFile)

            // 32-byte passphrase + SQLCipher spec: exercises the AES crypto path
            // (PBKDF2 key derivation + per-page AES) inside the native engine.
            val passphrase = "digitalis-wcdb-passphrase-123456".toByteArray(Charsets.UTF_8)
            val cipher = SQLiteCipherSpec()
                .setKDFIteration(64000)
                .setPageSize(4096)

            val db: SQLiteDatabase = SQLiteDatabase.openOrCreateDatabase(
                dbFile, passphrase, cipher, null, null
            )

            val readBack: String
            try {
                db.execSQL("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT, qty INTEGER)")
                db.execSQL("INSERT INTO t (id, name, qty) VALUES (1, 'alpha', 10)")
                db.execSQL("INSERT INTO t (id, name, qty) VALUES (2, 'bravo', 20)")
                db.execSQL("INSERT INTO t (id, name, qty) VALUES (3, 'charlie', 30)")
                db.execSQL("INSERT INTO t (id, name, qty) VALUES (4, 'delta', 40)")

                // Read the rows back and verify them.
                var count = 0
                var qtySum = 0
                var firstName: String? = null
                val cursor = db.rawQuery("SELECT id, name, qty FROM t ORDER BY id ASC", null)
                try {
                    while (cursor.moveToNext()) {
                        if (count == 0) firstName = cursor.getString(1)
                        qtySum += cursor.getInt(2)
                        count++
                    }
                } finally {
                    cursor.close()
                }
                readBack = "count=$count qtySum=$qtySum firstName=$firstName"

                // Known-correct expectations: 4 rows, 10+20+30+40 = 100, first name "alpha".
                if (count != 4 || qtySum != 100 || firstName != "alpha") {
                    return logged("WCDB FAIL: unexpected read-back ($readBack)")
                }
            } finally {
                db.close()
            }

            "WCDB OK (encrypted SQLCipher db, $readBack)"
        } catch (t: Throwable) {
            "WCDB FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        return logged(msg)
    }

    private fun logged(msg: String): String {
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloWcdb"
    }
}
