#!/usr/bin/env bash
# PC tests of src/porpoise_ns2gc.cpp (the Switch 2's GameCube controller over
# ugen) against a simulated console and controller. They test the module's
# logic only, not a PS5 or the real controller. Runs once plainly, once with
# ThreadSanitizer and once with AddressSanitizer.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cxx=${CXX:-$(command -v clang++-18 || command -v clang++ || command -v g++)}
out="$root/build/tests/ns2gc"
mkdir -p "$out"
flags=(-std=c++20 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -DPORPOISE_NS2GC_HOST_TEST
       -I"$root/src" "$root/src/porpoise_ns2gc.cpp" "$root/tests/ns2gc/test_ns2gc.cpp" -lpthread)
"$cxx" "${flags[@]}" -o "$out/test_ns2gc"
"$out/test_ns2gc"
"$cxx" "${flags[@]}" -fsanitize=thread -o "$out/test_ns2gc_tsan"
"$out/test_ns2gc_tsan"
"$cxx" "${flags[@]}" -fsanitize=address,undefined -o "$out/test_ns2gc_asan"
"$out/test_ns2gc_asan"
