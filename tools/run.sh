#!/bin/bash
# Restart the app on the device and collect logs (+ optional screenshot).
# usage: tools/run.sh [seconds=15] [screenshot.png]
SECS=${1:-15}
PKG=com.ts3ambitions.port
adb shell am force-stop $PKG
adb logcat -c
adb logcat -s TS3A:* TS3A-crash:* TS3A-NSLog:* TS3A-printf:* DEBUG:* libc:* AndroidRuntime:* > /tmp/ts3a_log.txt &
LOG_PID=$!
stop_log() {
    kill "$LOG_PID" 2>/dev/null
    wait "$LOG_PID" 2>/dev/null
}
trap stop_log EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
adb shell am start -n $PKG/.LauncherActivity >/dev/null
sleep "$SECS"
stop_log
trap - EXIT
if [ -n "$2" ]; then adb exec-out screencap -p > "$2"; fi
grep -c "" /tmp/ts3a_log.txt
