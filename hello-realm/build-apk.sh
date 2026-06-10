#!/usr/bin/env bash
# Copyright (C) 2026 utzcoz
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Reproducible build for the standalone hello-realm sample APK (ARM64-only).
#
# This is a STANDALONE build, intentionally NOT wired into the suite's
# settings.gradle.kts (the hello-qt pattern): Realm Kotlin's compiler plugin
# (final release 2.3.0) is ABI-locked to Kotlin 2.0.20 and crashes the suite's
# AGP 9.0 built-in Kotlin compiler. The settings.gradle.kts here anchors the
# build and pins AGP 8.7.3 / Kotlin 2.0.20 / Gradle 8.9. See NOTES.md.
#
# Requires: ANDROID_SDK_ROOT or ANDROID_HOME (or an sdk.dir in
# local.properties), JDK 17+, network access for the pinned Gradle/AGP/Realm
# artifacts on first run.
set -euo pipefail

cd "$(dirname "$0")"

if [ ! -f local.properties ]; then
  SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
  if [ -z "$SDK" ]; then
    echo "ERROR: set ANDROID_SDK_ROOT or ANDROID_HOME (no local.properties found)." >&2
    exit 1
  fi
  printf 'sdk.dir=%s\n' "$SDK" > local.properties
fi

./gradlew --no-daemon assembleDebug
cp build/outputs/apk/debug/hello-realm-debug.apk hello-realm-debug.apk
echo "Built hello-realm-debug.apk"
