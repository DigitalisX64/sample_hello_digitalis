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
package com.example.hellodigitalis.hellobcrypt

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.example.hellodigitalis.statustest.StatusTestRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class StatusTest {
    // Before logging its verdict the probe hashes 28 known-answer vectors and
    // runs three cost-12 key schedules (4096 iterations each). That lands
    // around 3 s on the emulator; 10 s leaves room for a slower host.
    @get:Rule
    val rule = StatusTestRule(
        "com.example.hellodigitalis.hellobcrypt/com.example.hellobcrypt.MainActivity",
        10000
    )

    @Test
    fun runsCleanly() {
        rule.assertRunsCleanly()
    }
}
