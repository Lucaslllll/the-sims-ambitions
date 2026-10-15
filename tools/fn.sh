#!/bin/sh
# usage: tools/fn.sh <hexaddr|name-regex>  — print a decompiled function
ROOT=$(dirname "$(dirname "$(realpath "$0")")")
F="$ROOT/analysis/decomp_arm64.c"
awk -v pat="$1" '
/^\/\/ ==== /{p = ($0 ~ pat)}
p' "$F"
