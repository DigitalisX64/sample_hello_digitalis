/*
 * Copyright 2020 The Android Open Source Project
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

package com.sample.texturedteapot

import android.app.Application
import android.content.pm.PackageManager.NameNotFoundException
import android.util.Log
import android.widget.Toast

class TeapotApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        Log.w("native-activity", "onCreate")

        val pm = applicationContext.packageManager
        val ai = try {
            pm.getApplicationInfo(this.packageName, 0)
        } catch (e: NameNotFoundException) {
            null
        }
        val applicationName = if (ai != null) pm.getApplicationLabel(ai) as String else "(unknown)"
        Toast.makeText(this, applicationName, Toast.LENGTH_SHORT).show()
    }
}
