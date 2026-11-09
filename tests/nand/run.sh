#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/porpoise-nand-test.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    "$test_dir/preserve-existing.cpp" -o "$build_dir/preserve-existing"
"$build_dir/preserve-existing" "$build_dir/fixtures"
