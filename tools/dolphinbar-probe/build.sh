#!/usr/bin/env bash
# Builds the DolphinBar test payloads (tools/dolphinbar-probe/probe.cpp) with
# this title's payload SDK: build/dolphinbar-probe/dolphinbar-probe-{1,2}.elf.
# Send one to the jailbreak's ELF loader (port 9021); see README.md there.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
sdk="$root/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang++ ]] || bash "$root/tools/setup-native-dependencies.sh" >/dev/null
out="$root/build/dolphinbar-probe"
mkdir -p "$out"
for stage in 1 2; do
    "$sdk/bin/prospero-clang++" -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \
        -DPORPOISE_USBHID_PAYLOAD -DPROBE_STAGE=$stage -I"$root/src" \
        "$root/tools/dolphinbar-probe/probe.cpp" "$root/src/porpoise_usbhid.cpp" \
        -o "$out/dolphinbar-probe-$stage.elf" -lkernel_sys
    echo "==> $out/dolphinbar-probe-$stage.elf"
done
