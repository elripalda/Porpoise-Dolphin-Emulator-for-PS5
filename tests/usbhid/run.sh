#!/usr/bin/env bash
# PC tests of src/porpoise_usbhid.cpp (the DolphinBar's hidapi over ugen)
# against a simulated console. They test the module's logic only, not a PS5.
#
# Needs a host C++ compiler and Dolphin's hidapi.h, from the tree
# tools/build-dolphin.sh checks out (.deps/dolphin-src), so the module's
# hidapi declarations are compiled against the real header. Runs once plainly
# and once with ThreadSanitizer.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
hidapi="$root/.deps/dolphin-src/Externals/hidapi/hidapi-src/hidapi"
[[ -f $hidapi/hidapi.h ]] || { echo "error: no $hidapi/hidapi.h (run tools/build-dolphin.sh once)" >&2; exit 2; }
cxx=${CXX:-$(command -v clang++-18 || command -v clang++ || command -v g++)}
out="$root/build/tests/usbhid"
mkdir -p "$out"
flags=(-std=c++20 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -DPORPOISE_USBHID_HOST_TEST
       -I"$root/src" -I"$hidapi" "$root/src/porpoise_usbhid.cpp" "$root/src/porpoise_dolphinbar.cpp" "$root/tests/usbhid/test_usbhid.cpp" -lpthread)
"$cxx" "${flags[@]}" -o "$out/test_usbhid"
"$out/test_usbhid"
"$cxx" "${flags[@]}" -fsanitize=thread -o "$out/test_usbhid_tsan"
"$out/test_usbhid_tsan"
"$cxx" "${flags[@]}" -fsanitize=address,undefined -o "$out/test_usbhid_asan"
"$out/test_usbhid_asan"
