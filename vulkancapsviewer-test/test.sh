#!/usr/bin/env bash
# Smoke test for VulkanCapsViewer 4.11 under Digitalis translation.
#
# Looks for a pre-built APK at prebuilt/vulkancapsviewer-4.11.apk
# (see README.md for how to obtain one), installs it on the connected
# Digitalis emulator, launches it, and watches logcat for crashes.
#
# Exits 0 on PASS (process stays up for the watch window and no
# Berberis "Undefined" line / native tombstone / FATAL EXCEPTION fires),
# non-zero on FAIL.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APK="${VKCAPS_APK:-$HERE/prebuilt/vulkancapsviewer-4.11.apk}"
PKG="de.saschawillems.vulkancapsviewer"
ACTIVITY="org.qtproject.qt.android.bindings.QtActivity"
WATCH_SECS="${VKCAPS_WATCH_SECS:-10}"

if [[ ! -f "$APK" ]]; then
    echo "SKIP: no APK at $APK"
    echo "      See README.md (download from upstream releases or build via Qt SDK)"
    exit 77   # autotools-style "skip" exit code
fi

if ! adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r' | grep -q "1"; then
    echo "ERROR: no booted emulator; run 'emulator -memory 4096 -writable-system &' first"
    exit 1
fi

echo "[1/4] uninstall any prior $PKG"
adb uninstall "$PKG" >/dev/null 2>&1 || true

echo "[2/4] install $APK"
adb install -r "$APK" 2>&1 | tail -2

echo "[3/4] clear logcat and launch"
adb logcat -c
adb shell am start -n "$PKG/$ACTIVITY" 2>&1 | tail -1

echo "[4/4] watch for $WATCH_SECS s..."
sleep "$WATCH_SECS"

# Drain logcat
LOG=$(mktemp)
adb logcat -d > "$LOG" 2>&1

# Failure signals
fail=""
if grep -q 'Undefined arm64 instruction' "$LOG"; then
    fail="$fail\n  - Berberis hit Undefined ARM64 instruction:"
    fail="$fail\n$(grep -m 5 'Undefined arm64 instruction' "$LOG" | sed 's/^/      /')"
fi
if grep -qE "FATAL EXCEPTION.*$PKG|$PKG.*signal [0-9]+" "$LOG"; then
    fail="$fail\n  - Java/native FATAL EXCEPTION:"
    fail="$fail\n$(grep -m 5 -E "FATAL EXCEPTION.*$PKG|$PKG.*signal [0-9]+" "$LOG" | sed 's/^/      /')"
fi
if ! adb shell "pgrep -f $PKG" >/dev/null 2>&1; then
    fail="$fail\n  - process is no longer running after $WATCH_SECS s"
fi

rm -f "$LOG"

if [[ -n "$fail" ]]; then
    echo
    echo "FAIL$fail" | sed 's/\\n/\n/g'
    exit 1
fi

echo
echo "PASS: $PKG stayed alive for ${WATCH_SECS}s with no Undefined-instruction / FATAL hits"
exit 0
