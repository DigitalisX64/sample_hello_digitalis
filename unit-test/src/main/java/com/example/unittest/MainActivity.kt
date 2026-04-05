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

package com.example.hellodigitalis.unittest

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import com.example.hellodigitalis.unittest.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.sampleText.text = "1 + 2 = " + add(1, 2).toString()
    }

    /**
     * A native method that is implemented by the 'unittest' native library,
     * which is packaged with this application.
     */
    external fun add(a: Int, b: Int): Int

    companion object {
        // Used to load the 'unittest' library on application startup.
        init {
            System.loadLibrary("unittest")
        }
    }
}
