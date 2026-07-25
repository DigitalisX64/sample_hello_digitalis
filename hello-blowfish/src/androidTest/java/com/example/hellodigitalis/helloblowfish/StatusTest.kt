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
package com.example.hellodigitalis.helloblowfish

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.example.hellodigitalis.statustest.StatusTestRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class StatusTest {
    // The probe runs three bcrypt cost-10 key schedules (~3.2M block
    // encryptions) before it logs its verdict, so allow more than the usual
    // 5 s for the result line to appear.
    @get:Rule
    val rule = StatusTestRule(
        "com.example.hellodigitalis.helloblowfish/com.example.helloblowfish.MainActivity",
        10000
    )

    @Test
    fun runsCleanly() {
        rule.assertRunsCleanly()
    }
}
