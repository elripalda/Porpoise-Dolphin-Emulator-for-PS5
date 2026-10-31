#!/usr/bin/env bash
# Builds and runs tools/ui-preview/controllers.cpp on Linux (or WSL) with
# lavapipe: PNGs of Settings > Controllers & Accessories and its test screen
# with DolphinBar states set by hand, in build/ui-preview/controllers/.
# Needs clang++, libvulkan and a Vulkan driver (mesa-vulkan-drivers' lavapipe).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out="$root/build/ui-preview/controllers"
obj="$root/build/ui-preview/obj"
mkdir -p "$out" "$obj"
[[ -f $root/build/title_build_identity.h ]] ||
    echo '#define PS5_RETROARCH_BUILD_ID "Porpoise build identity: preview"' > "$root/build/title_build_identity.h"
flags=(-O1 -w -DPORPOISE_DESKTOP -DPORPOISE -DPORPOISE_UI_PREVIEW -I"$root/src" -I"$root/third_party"
       -I"$root/third_party/libretro" -I"$root/third_party/vulkan" -I"$root/desktop" -I"$root/desktop/include"
       -I"$root/build")
# What the screens need of the title: the UI and the libraries it calls, not
# the PS5's platform code, its video output or the desktop's SDL window.
skip=" core_imports_ps5 core_loader_ps5 core_threads_ps5 crash_report memory_diagnostics memory_ps5 memory_status
 permissions_ps5 sampler_ps5 thread_probe porpoise_jailbreak porpoise_http vulkan_trace libc_shims locale_shims
 overflow_heap platform_wraps present_clock_ps5vk radv_icd_ps5 porpoise_notify porpoise_netfs porpoise_dns
 porpoise_vk porpoise_main "
skip=" $(echo $skip) "
objects=()
for f in "$root"/src/*.cpp "$root"/src/*.c; do
    base=$(basename "${f%.*}")
    [[ $skip == *" $base "* ]] && continue
    o="$obj/$base.o"
    if [[ ! -f $o || $f -nt $o || $root/src/ui_app.hpp -nt $o ]]; then
        if [[ $f == *.c ]]; then std=-std=c11; cc=clang; else std=-std=c++20; cc=clang++; fi
        $cc $std "${flags[@]}" -c "$f" -o "$o"
    fi
    objects+=("$o")
done
clang++ -std=c++20 "${flags[@]}" -c "$root/tools/ui-preview/controllers.cpp" -o "$obj/controllers_preview.o"
# The console's and the desktop's own calls (video out, pad, HTTP, network
# shares, the core loader) are never made by these screens: each gets a stub
# that returns 0, generated from what the link is missing.
link=(clang++ -o "$obj/controllers" "$obj/controllers_preview.o" "${objects[@]}" -l:libvulkan.so.1 -lpthread -lz)
: > "$obj/stubs.c"
if ! "${link[@]}" -Wl,--no-demangle 2> "$obj/link.txt"; then
    grep -o "undefined reference to \`[A-Za-z0-9_]*'" "$obj/link.txt" | sed "s/.*\`\(.*\)'/\1/" | sort -u |
        while read -r name; do echo "long $name(void) { return 0; }"; done > "$obj/stubs.c"
    clang -w -c "$obj/stubs.c" -o "$obj/stubs.o"
    "${link[@]}" "$obj/stubs.o"
fi
"$obj/controllers" "$root/assets" "$out" "$out"