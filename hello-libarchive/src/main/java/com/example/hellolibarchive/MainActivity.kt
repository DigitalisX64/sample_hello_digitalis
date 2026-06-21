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
package com.example.hellolibarchive

import android.os.Bundle
import android.util.Base64
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellolibarchive.R
import me.zhanghai.android.libarchive.Archive
import me.zhanghai.android.libarchive.ArchiveEntry
import java.nio.ByteBuffer
import java.nio.charset.StandardCharsets

/**
 * Exercises libarchive (the multi-format archive C library) through the
 * me.zhanghai JNI binding under Berberis ARM64->x86_64 translation. The first
 * Archive call loads the arm64-v8a libarchive-jni.so (libarchive statically
 * linked).
 *
 * The probe is self-contained — it touches no on-device assets. An embedded
 * single-entry USTAR (tar) archive (built on the host: a 512-byte ustar header
 * for "hello.txt", a 512-byte data block holding 30 bytes, and the two-zero-
 * block end-of-archive marker — 2048 bytes total) is decoded from a base64
 * constant into a direct ByteBuffer, then opened with libarchive's READ API
 * using full format/filter auto-detection (archive_read_support_format_all /
 * _filter_all). The probe reads the first header, then the entry data, and
 * verifies the read-back pathname and bytes match the known originals. This
 * drives libarchive's format-detection state machine and its tar header parse
 * + data path through the translator.
 *
 * On success it logs "LIBARCHIVE OK ..."; on any failure or exception it logs
 * "LIBARCHIVE FAIL: ..." so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runLibarchiveProbe()
    }

    private fun runLibarchiveProbe(): String {
        val msg = try {
            val expectedName = "hello.txt"
            val expectedContent =
                "digitalis libarchive roundtrip".toByteArray(StandardCharsets.UTF_8)

            // Raw bytes of the embedded archive. The binding's readOpenMemory
            // reads the buffer's native address directly, so it must be a
            // *direct* ByteBuffer that outlives the archive handle.
            val archiveBytes = Base64.decode(TAR_BASE64, Base64.DEFAULT)
            val archiveBuffer = ByteBuffer.allocateDirect(archiveBytes.size)
            archiveBuffer.put(archiveBytes)
            archiveBuffer.position(0)
            archiveBuffer.limit(archiveBytes.size)

            var readName: String? = null
            var readSize = -1
            val readData = ByteArray(expectedContent.size)
            run {
                val reader = Archive.readNew()
                try {
                    Archive.readSupportFilterAll(reader)
                    Archive.readSupportFormatAll(reader)
                    Archive.readOpenMemory(reader, archiveBuffer)

                    val entry = Archive.readNextHeader(reader)
                    if (entry == 0L) {
                        throw IllegalStateException("readNextHeader found no entry")
                    }
                    readName = ArchiveEntry.pathnameUtf8(entry)
                    readSize = ArchiveEntry.size(entry).toInt()
                    if (readSize != expectedContent.size) {
                        throw IllegalStateException(
                            "entry size mismatch: got $readSize expected ${expectedContent.size}"
                        )
                    }

                    // readData advances the buffer's position by bytes read;
                    // loop until the whole entry is consumed (one block may not
                    // deliver everything).
                    val dataIn = ByteBuffer.allocateDirect(expectedContent.size)
                    while (dataIn.hasRemaining()) {
                        val before = dataIn.position()
                        Archive.readData(reader, dataIn)
                        if (dataIn.position() == before) {
                            throw IllegalStateException("readData made no progress")
                        }
                    }
                    dataIn.flip()
                    dataIn.get(readData)
                } finally {
                    Archive.readFree(reader)
                }
            }

            val nameOk = readName == expectedName
            val dataOk = readData.contentEquals(expectedContent)

            if (nameOk && dataOk) {
                "LIBARCHIVE OK (read 1 entry '$expectedName', ${expectedContent.size} bytes verified)"
            } else {
                "LIBARCHIVE FAIL: nameOk=$nameOk (got '$readName') dataOk=$dataOk " +
                    "(size=$readSize, expected ${expectedContent.size} bytes)"
            }
        } catch (t: Throwable) {
            "LIBARCHIVE FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloLibarchive"

        // A complete, valid single-entry USTAR (tar) archive built on the host:
        //   printf 'digitalis libarchive roundtrip' > hello.txt
        //   tar -cf a.tar hello.txt
        // trimmed to its minimal valid form — a 512-byte ustar header + 512-byte
        // data block + the two-512-byte-zero-block end-of-archive marker (2048
        // bytes). Verified to extract cleanly with system tar.
        private const val TAR_BASE64 =
            "aGVsbG8udHh0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADAwMDA2NjQAMDAwMTc1" +
            "MAAwMDAxNzUwADAwMDAwMDAwMDM2ADE1MjE1NzY0NDQ0ADAxMTA1NQAgMAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAB1c3RhciAgAGRldgAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAZGV2AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABk" +
            "aWdpdGFsaXMgbGliYXJjaGl2ZSByb3VuZHRyaXAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
    }
}
