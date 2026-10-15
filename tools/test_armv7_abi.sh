#!/bin/bash
set -euo pipefail
ROOT=$(dirname "$(dirname "$(realpath "$0")")")
NDK=${ANDROID_NDK_HOME:-"$HOME/Android/Sdk/ndk/27.2.12479018"}
OUT="${TMPDIR:-/tmp}/ts3a-armv7-abi-test"
"$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi26-clang++" \
    -std=c++20 -O2 -fno-omit-frame-pointer -static-libstdc++ \
    -I"$ROOT/android/app/src/main/cpp" \
    "$ROOT/tools/tests/armv7_abi_test.cpp" "$ROOT/tools/tests/armv7_abi_fixture.S" \
    "$ROOT/android/app/src/main/cpp/hle/abi.cpp" -llog -o "$OUT"
adb push "$OUT" /data/local/tmp/ts3a-armv7-abi-test
adb shell chmod 755 /data/local/tmp/ts3a-armv7-abi-test
adb shell /data/local/tmp/ts3a-armv7-abi-test
