#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$(mktemp -d "${TMPDIR:-/tmp}/porpoise-memory.XXXXXX")"
trap 'rm -rf "$build"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    "$root/src/memory_ps5.cpp" "$root/tests/memory/calloc.cpp" -o "$build/calloc"
"$build/calloc"
