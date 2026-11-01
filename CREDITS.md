# Credits

Porpoise would not exist without the people and projects below. Thank you.

## Porpoise

**Ruben ([@elripalda](https://ripalda.dev))**: creator. The idea, the
design direction, the logo and artwork, the menu music and every sound effect,
and all of the testing on real hardware.

### Contributors

**[Martin Pham (MartinPham)](https://github.com/MartinPham)**: home screen
forwarders, starting a game straight from its own tile (`--rom`,
[docs/FORWARDER.md](docs/FORWARDER.md)).

## Dolphin on PS5

**[Mihawk (mihawk-99)](https://github.com/mihawk-99)** brought the Dolphin core
to the PS5. His PS5 RetroArch port — the Dolphin PS5 patch, the in-process core
loader, threads, memory and crash reports — and his PS5_Vulkan and PS5_Mesa
work, which put Vulkan on the PS5 through Mesa's RADV, are what Porpoise is
built on. Porpoise began as a fork of his PS5 RetroArch, and it would not exist
without him. Details and licenses are under [PS5 platform](#ps5-platform).

## Emulation

| Project | Role in Porpoise | License |
|---|---|---|
| [Dolphin](https://dolphin-emu.org) by the Dolphin Emulator Project and contributors | The GameCube and Wii emulator. Every game runs on Dolphin. | GPL-2.0-or-later |
| [libretro/dolphin](https://github.com/libretro/dolphin) | Dolphin packaged as a libretro core, tracking upstream Dolphin | GPL-2.0-or-later |
| [libretro / RetroArch](https://www.libretro.com) | The libretro API (`libretro.h`) that Porpoise uses to host the Dolphin core | MIT (API headers) |
| [libretro-core-info](https://github.com/libretro/libretro-core-info) | The Dolphin core's `.info` metadata | MIT |
| [rcheevos](https://github.com/RetroAchievements/rcheevos) by [RetroAchievements](https://retroachievements.org) | The achievements runtime in Dolphin's RetroAchievements support, built into the PS5 core | MIT |
| [ps5-framegen](https://github.com/Swordpdf/ps5-framegen) by **swordpdf** (from PS5SX2 and RPCS3-PS5), with AMD FidelityFX FSR 3's optical flow | Frame Generation (beta): the in-between frames and when to show them (`src/ps5fg`) | GPL-3.0-or-later; shaders MIT |

Porpoise builds Dolphin from libretro/dolphin revision
`4d23cf151640eb810cb1b8e9d9fc922cf59c0b87` with one patch,
[`patches/dolphin/ps5-port.patch`](patches/dolphin/ps5-port.patch).

## PS5 platform

| Project | Role in Porpoise | License |
|---|---|---|
| [PS5 RetroArch](https://github.com/mihawk-99/PS5_RetroArch) by **Mihawk (mihawk-99)** | Porpoise began as a fork of it. The Dolphin PS5 port patch, the in-process core loader, threads, memory, crash reports and the build scripts all come from there. | GPL-3.0-or-later |
| [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) by **Mihawk (mihawk-99)** | Vulkan on PS5: the RADV link recipe and linker script | GPL-3.0-or-later |
| [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) by **Mihawk (mihawk-99)**, on [Mesa](https://mesa3d.org) | Mesa's **RADV** Vulkan driver with a PS5 winsys: everything Porpoise and Dolphin draw goes through it | MIT and per-file licenses |
| [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) by **John Törnblom** and contributors, with [Mihawk's fork](https://github.com/mihawk-99/PS5_PayloadSDK) | The toolchain, headers and platform layer | GPL-3.0-or-later |
| **BlackBearReloaded**: ps5-native-app-boilerplate and ProsperoLight | The native title pipeline: ELF to signed `eboot.bin`, the generated `libc.prx` loader companion | GPL-3.0-or-later |
| [LLVM](https://llvm.org) libc++, libc++abi, libunwind, compiler-rt | The C++ runtime, linked from the SDK release | Apache-2.0 WITH LLVM-exception |
| [zlib](https://zlib.net) by Jean-loup Gailly and Mark Adler | Compression | Zlib |
| [dlmalloc](https://gee.cs.oswego.edu/dl/html/malloc.html) by Doug Lea | The overflow heap | MIT |

## Launcher

| Project | Role in Porpoise | License |
|---|---|---|
| [GameTDB](https://www.gametdb.com) and its contributors | Box art, back covers, disc art and game details, downloaded on the console. Nothing from GameTDB ships with Porpoise. | GameTDB terms |
| [PS5 Button Icons and Controls](https://zacksly.itch.io) by **Zacksly** ([@_Zacksly](https://twitter.com/_Zacksly)) | The DualSense drawings on the button-mapping screen and the in-game Controls tab, and every button icon in the menus. Modified for Porpoise: rendered to PNG, the PlayStation logo and the pack's own labels removed, lines thickened (`tools/make-controller-art.py`; originals in `third_party/zacksly-ps5-icons`). | CC BY 3.0 |
| [Nunito](https://github.com/googlefonts/nunito) by Vernon Adams, Jacques Le Bailly and contributors | Every word on screen | SIL Open Font License 1.1 |
| [Noto Sans JP](https://github.com/google/fonts/tree/main/ofl/notosansjp) by Adobe and Google | The Japanese menus (a subset of the characters they use, `tools/make-jp-font.py`) | SIL Open Font License 1.1 |
| [Dolphin's per-game settings](https://github.com/dolphin-emu/dolphin/tree/master/Data/Sys/GameSettings) by the Dolphin team | Shown under *Recommended* in a game's settings (they ship with the Dolphin core) | GPL-2.0-or-later |
| [stb](https://github.com/nothings/stb) by Sean Barrett | `stb_truetype`, `stb_image` and `stb_vorbis`: text, images and the menu music | MIT or public domain |
| [Wii Banner Player](https://github.com/jordan-woyak/wii-banner-player) by the Wii Banner Player Project | How a Wii disc's own tile and banner (layouts, animations, textures, jingle) are read and played. Rewritten for Porpoise as a software renderer (`src/porpoise_banner.cpp`). | zlib |
| [Zstandard](https://github.com/facebook/zstd) educational decoder by Meta Platforms | Reading `.rvz` disc images for their banners | BSD or GPL-2.0 |
| [libsmb2](https://github.com/sahlberg/libsmb2) by Ronnie Sahlberg and contributors | Games on a network share (SMB): the library search, the launcher and Dolphin read them through it (`src/porpoise_netfs.cpp`; the client library in `third_party/libsmb2`) | LGPL-2.1-or-later |
| [libnfs](https://github.com/sahlberg/libnfs) by Ronnie Sahlberg and contributors | Games on an NFS share, the same way (`src/porpoise_netfs.cpp`; the client library in `third_party/libnfs`) | LGPL-2.1-or-later; protocol files BSD-2-Clause |
| Widescreen codes collected by **Warped Polygon** | The GameCube widescreen codes in `assets/widescreen` (cleaned by `tools/make-widescreen.py`), which play games in true 16:9 | the collection's own |
| 16:9 codes for Virtual Console N64 games from **Admentus64**'s Enhancement Codes (with gamemasterplc and the codes' other authors) | Optional 16:9 codes in Cheats and Patches for the Wii's Virtual Console N64 games, in `assets/widescreen` | GPL-3.0 |

## Controllers

| Project | Role in Porpoise | License |
|---|---|---|
| [BlueV](https://github.com/0xZ3n0x/BlueV) by **0xZ3n0x** | Showed that a PS5 title can talk to USB devices through their `ugen` nodes, and the console's `USB_DEVICEINFO` number; Porpoise's own USB code (`src/porpoise_usbhid.cpp`, `src/porpoise_ns2gc.cpp`, `src/porpoise_bt.cpp`) follows that approach | reference only |
| [SDL](https://github.com/libsdl-org/SDL)'s HIDAPI Switch 2 driver by **Valve Corporation** and the SDL contributors | The USB set-up of the Switch 2's GameCube controller, its flash calibration, its input report and rumble, as `src/porpoise_ns2gc.cpp` reimplements them | zlib (reference) |
| [switch2_controller_research](https://github.com/ndeadly/switch2_controller_research) by **ndeadly** | The Switch 2 controllers' Bluetooth LE advertisements, GATT handles, commands and report formats | reference |
| [switch2-controllers-linux](https://github.com/Nitrama/switch2-controllers-linux) by **Nitrama** | The Bluetooth set-up that brings the GameCube controller's input over a raw ATT channel, its pairing values and its rumble presets, as `src/porpoise_bt.cpp` reimplements them | MIT (reference) |
| Linux's `btrtl` driver and [linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git) | How a Realtek RTL8761B/C USB Bluetooth adapter is given its patch (reimplemented in `src/porpoise_bt.cpp`), and the patch files themselves, shipped unmodified in `assets/bluetooth` | GPL-2.0 (reference); Realtek's redistributable licence (`assets/bluetooth/LICENCE.rtlwifi_firmware.txt`) |

## The PS5 scene

- **etaHEN**, **kstuff** and **ShadowMountPlus**, which make running homebrew
  like Porpoise possible.
- **PS5SX2** and **ProsperoEden**, PS5 homebrew front ends that showed the way
  and inspired Porpoise's approach.
- **PS5SX2 (Spyros, with Gabriel Fonseca's RetroAchievements groundwork)**:
  Porpoise's RetroAchievements popups follow its design and findings: the
  account on the shelf with L1 + Square, softcore only, and unlocks as PS5
  trophy-style notifications with the trophy sounds, sent through the
  jailbreak's ELF loader by a small relay payload
  (`tools/notify-relay/notify_relay.c`, `src/porpoise_notify.cpp`;
  GPL-3.0-or-later, as PS5SX2's).

## Trademarks

Nintendo, GameCube and Wii are trademarks of Nintendo. PlayStation, PS5 and
DualSense are trademarks of Sony Interactive Entertainment Inc. Porpoise is not
affiliated with, endorsed by or sponsored by either company, and contains no
material from them.

---

Every component that ships inside the app, with its license text and the exact
source revision it was built from, is listed in `licenses/README.txt` and
`licenses/components.json` in the release folder.

If you think someone is missing from this page, please say so on the
[RIPALDA Discord](https://discord.gg/GgDE5Vynyu).
