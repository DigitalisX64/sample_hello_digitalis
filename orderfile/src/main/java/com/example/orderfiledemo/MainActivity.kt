/*
 * Copyright (C) The Android Open Source Project
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

package com.example.hellodigitalis.orderfile

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import com.example.hellodigitalis.orderfile.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        runWorkload(applicationContext.cacheDir.toString())
        binding.sampleText.text = "Hello, world!"
    }

    /**
     * A native method that is implemented by the 'orderfiledemo' native library,
     * which is packaged with this application.
     */
    external fun runWorkload(tempDir: String)

    companion object {
        // Used to load the 'orderfiledemo' library on application startup.
        init {
            System.loadLibrary("orderfiledemo")
        }
    }
}
