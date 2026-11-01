#!/usr/bin/env bash
# PC tests of src/porpoise_bt.cpp's pure parts (Realtek patch files, LE
# advertising data), built as the desktop build has the module.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cxx=${CXX:-$(command -v clang++-18 || command -v clang++ || command -v g++)}
out="$root/build/tests/bt"
mkdir -p "$out"
"$cxx" -std=c++20 -O1 -g -Wall -Wextra -Werror -DPORPOISE_DESKTOP -fsanitize=address,undefined -I"$root/src" \
    "$root/src/porpoise_bt.cpp" "$root/tests/bt/test_bt.cpp" -o "$out/test_bt"
"$out/test_bt" "$root/assets/bluetooth"
