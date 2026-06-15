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

# Reproducibly vendor a trimmed copy of the WireGuard Android tunnel dependency.
#
# Why: the published tunnel AAR contains one java.lang.Record class
# (com.wireguard.android.backend.Statistics$PeerStats). D8/R8 can only desugar a
# record when a "global-synthetics consumer" is active, but no Android Gradle
# Plugin available here (8.2.2 / 8.7.3 / 9.0.0) enables that consumer for
# library dexing — every variant fails with:
#   "Attempt to create a global synthetic for 'Record desugaring' without a
#    global-synthetics consumer."
# This sample only calls GoBackend.wgVersion() (reflectively) plus
# SharedLibraryLoader; it never touches Statistics. So we drop the two
# Statistics classes (the record and its sole user) from the dependency's
# classes.jar and depend on the trimmed jar as a local file, and we package
# libwg-go.so directly as a jniLib. No record => dexes cleanly under AGP 9.0.0.
#
# Re-run this whenever bumping TUNNEL_VERSION. Requires: curl, unzip, zip.
set -euo pipefail

TUNNEL_VERSION="1.0.20260102"
AAR_URL="https://repo1.maven.org/maven2/com/wireguard/android/tunnel/${TUNNEL_VERSION}/tunnel-${TUNNEL_VERSION}.aar"
MOD_DIR="$(cd "$(dirname "$0")" && pwd)"
JAR_OUT="${MOD_DIR}/libs/wireguard-tunnel-${TUNNEL_VERSION}-norecord.jar"
SO_OUT_DIR="${MOD_DIR}/src/main/jniLibs/arm64-v8a"

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

echo "Downloading ${AAR_URL}"
curl -fsSL -o "${TMP}/tunnel.aar" "${AAR_URL}"
unzip -q "${TMP}/tunnel.aar" -d "${TMP}/aar"

echo "Trimming the java.lang.Record classes from classes.jar"
mkdir -p "${TMP}/cj"
( cd "${TMP}/cj" && unzip -q "${TMP}/aar/classes.jar" )
rm -f "${TMP}/cj/com/wireguard/android/backend/Statistics.class" \
      "${TMP}/cj/com/wireguard/android/backend/Statistics\$PeerStats.class"
mkdir -p "${MOD_DIR}/libs"
rm -f "${MOD_DIR}/libs/"wireguard-tunnel-*-norecord.jar
( cd "${TMP}/cj" && zip -qr "${JAR_OUT}" . )

echo "Extracting libwg-go.so into jniLibs"
mkdir -p "${SO_OUT_DIR}"
cp "${TMP}/aar/jni/arm64-v8a/libwg-go.so" "${SO_OUT_DIR}/libwg-go.so"

echo "Done:"
echo "  ${JAR_OUT}"
echo "  ${SO_OUT_DIR}/libwg-go.so"
