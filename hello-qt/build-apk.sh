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

# Reproducible build for the standalone hello-qt sample APK (ARM64-only Qt 6).
#
# This is a STANDALONE build, intentionally NOT wired into the suite's
# settings.gradle.kts: Qt 6.7's androiddeployqt generates a Gradle 8.3 / AGP
# 7.4.1 project, which is incompatible with the suite's Gradle 9.1 / AGP 9.0.
# See README.md for the full rationale and the one-time Qt SDK install.
#
# Prerequisites (one-time, outside the repo): install the Qt 6.7 desktop +
# android_arm64_v8a kits with aqtinstall into a directory of your choice, then
# point QT_ROOT at it. Example (replace <qt-dir> with your install location):
#   python3 -m aqt install-qt linux desktop 6.7.3 linux_gcc_64 --outputdir <qt-dir>
#   python3 -m aqt install-qt linux android 6.7.3 android_arm64_v8a --outputdir <qt-dir>
#   export QT_ROOT=<qt-dir>/6.7.3
#
# All toolchain locations come from the environment — there are no
# machine-specific path defaults. Required (export before invoking):
#   ANDROID_SDK_ROOT (or ANDROID_HOME), JAVA_HOME, QT_ROOT.
# Optional overrides: ANDROID_NDK_ROOT, QT_HOST, QT_ANDROID, QT_VERSION,
#   ANDROID_PLATFORM, BUILD_TOOLS_REVISION.
set -euo pipefail

require_env() {  # VAR_NAME  human description
  if [ -z "${!1:-}" ]; then
    echo "ERROR: \$$1 is not set ($2). Export it before running build-apk.sh." >&2
    exit 1
  fi
}

# Accept the standard ANDROID_HOME alias for ANDROID_SDK_ROOT.
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
require_env ANDROID_SDK_ROOT "Android SDK root, or set ANDROID_HOME"
require_env JAVA_HOME "JDK home"
require_env QT_ROOT "Qt install root containing gcc_64 and android_arm64_v8a (e.g. <qt-dir>/6.7.3)"

QT_VERSION="${QT_VERSION:-6.7.3}"
QT_HOST="${QT_HOST:-${QT_ROOT}/gcc_64}"
QT_ANDROID="${QT_ANDROID:-${QT_ROOT}/android_arm64_v8a}"
# NDK lives under the SDK; pick the newest installed unless overridden.
ANDROID_NDK_ROOT="${ANDROID_NDK_ROOT:-$(ls -d "${ANDROID_SDK_ROOT}"/ndk/* 2>/dev/null | sort -V | tail -1)}"
require_env ANDROID_NDK_ROOT "Android NDK (install under \$ANDROID_SDK_ROOT/ndk or set ANDROID_NDK_ROOT)"
# android-34 + build-tools 34.0.0 are the newest the Qt-generated AGP 7.4.1
# toolchain's aapt2 can parse (android-35's android.jar trips the old aapt2).
ANDROID_PLATFORM="${ANDROID_PLATFORM:-android-34}"
BUILD_TOOLS_REVISION="${BUILD_TOOLS_REVISION:-34.0.0}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-android"

export ANDROID_SDK_ROOT ANDROID_NDK_ROOT JAVA_HOME
export PATH="${JAVA_HOME}/bin:${PATH}"

echo ">>> [1/4] Configure (qt-cmake, arm64-v8a only)"
rm -rf "${BUILD_DIR}"
"${QT_ANDROID}/bin/qt-cmake" \
    -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_ANDROID_ABIS=arm64-v8a \
    -DANDROID_ABI=arm64-v8a \
    -DQT_HOST_PATH="${QT_HOST}" \
    -DANDROID_SDK_ROOT="${ANDROID_SDK_ROOT}" \
    -DANDROID_NDK_ROOT="${ANDROID_NDK_ROOT}"

echo ">>> [2/4] Build native lib (libhello_qt_arm64-v8a.so)"
cmake --build "${BUILD_DIR}" --target hello_qt

echo ">>> [3/4] Pin SDK build-tools revision in deployment settings"
# Qt 6.7's _qt_internal_android_get_sdk_build_tools_revision leaves
# sdkBuildToolsRevision empty unless it auto-detects; auto-detect picks the
# newest installed (an RC here), so pin a known-good stable revision.
python3 - "${BUILD_DIR}/android-hello_qt-deployment-settings.json" \
         "${BUILD_TOOLS_REVISION}" <<'PY'
import json, sys
path, rev = sys.argv[1], sys.argv[2]
d = json.load(open(path))
d["sdkBuildToolsRevision"] = rev
json.dump(d, open(path, "w"), indent=3)
print("    sdkBuildToolsRevision ->", rev)
PY

echo ">>> [4/4] Package APK (androiddeployqt + gradle)"
mkdir -p "${BUILD_DIR}/android-build/libs/arm64-v8a"
cp "${BUILD_DIR}/libhello_qt_arm64-v8a.so" \
   "${BUILD_DIR}/android-build/libs/arm64-v8a/"
"${QT_HOST}/bin/androiddeployqt" \
    --input "${BUILD_DIR}/android-hello_qt-deployment-settings.json" \
    --output "${BUILD_DIR}/android-build" \
    --android-platform "${ANDROID_PLATFORM}" \
    --gradle

APK="${BUILD_DIR}/android-build/build/outputs/apk/debug/hello-qt-debug.apk"
cp "${APK}" "${SCRIPT_DIR}/hello-qt-debug.apk"
echo ""
echo ">>> Done. APK: ${SCRIPT_DIR}/hello-qt-debug.apk"
echo ">>> (also at ${APK})"
