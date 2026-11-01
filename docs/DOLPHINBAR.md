# Real Wii Remotes through a Mayflash DolphinBar

Porpoise can play Wii games with original Wii Remotes and Wii Remote Plus
controllers through a **Mayflash DolphinBar** (W010) in a USB port of the PS5.
The DolphinBar does the Bluetooth itself and shows each of its four Remotes as
a USB HID interface; the console's own Bluetooth is not used and not touched.

**Status: tested on a PS5** (October 2026) with a DolphinBar W010 (mode 4),
a Wii Remote Plus with a Nunchuk, in Super Mario Galaxy 1 and 2 and in
Porpoise's menus: buttons, motion, IR, the Nunchuk, the MotionPlus, rumble
and the speaker in games, long sessions without a dropout, a Remote switched
off and on in a game, leaving a game and Porpoise. The speaker sounds a little
rough through the DolphinBar (nothing is lost on the way; see the log);
Classic Controller and more than one Remote were only tested on a PC against a
simulated console.

## Using it

1. Set the DolphinBar to **mode 4** (Wii Remote mode; the LED shows which) and
   plug it into a USB port of the PS5. No firmware change is needed.
2. Pair your Remotes with the DolphinBar as its manual says (its sync button,
   then 1 + 2 or the Remote's sync button).
3. **Settings → Wii Remote → Wii Remotes → Real Wii Remotes (DolphinBar)**.
   A game can have its own choice, and the in-game menu has it too.
4. Start a Wii game. Each Remote is found when it's connected to the
   DolphinBar and goes into the first free player slot.

What then comes from the real Remote: its buttons, accelerometer, IR pointer
(through the DolphinBar's own IR lights), the Nunchuk, the Classic
Controller, the Wii MotionPlus (and the Remote Plus's built-in one), rumble,
the LEDs and speaker data, for up to four players. A Remote that is switched
off and on again, or that loses its connection, is found again. The DualSense
still opens Porpoise's menu (Options + touch pad).

`/data/porpoise/dolphinbar.log` lists what was found, opened and lost, and at
the end of each game a summary per interface. Should the log show
`driver console's` for the DolphinBar's interfaces and input arrive only now
and then, create an empty file `/data/porpoise/dolphinbar-detach`: Porpoise
then detaches the console's driver from the DolphinBar's interfaces (only
those; unplugging the bar gives them back).

## Controllers & Accessories (before any game)

**Settings → Controllers & Accessories** lists the DualSense controllers, the
**Mayflash DolphinBar W010** and **Wii Remote 1–4**, each with what it is doing
now, and the *Wii Remotes* choice (DualSense or real Wii Remotes).
**Controller Test & Diagnostics** (or Cross on any of those rows) opens a
screen with:

- **DualSense**: which players are connected and player 1's buttons live.
- **Mayflash DolphinBar W010**: its state (*Not connected*, *USB device
  detected*, *USB access failed*, *Ready*, *In use by the game*), USB id,
  node, product name, whether the descriptors were read, every interface with
  its endpoints, whether the console's driver has it and its packet count,
  and the **diagnosis**: DolphinBar not found; a USB device there but access
  refused; an HID interface not opened; no Wii Remote connected; a Remote that
  takes data but doesn't answer; no input packets; an attachment not
  recognized; the console's driver on an interface. The firmware version is
  shown as not readable: nothing on USB reports it reliably.
- **Wii Remote 1–4**: connected / waiting / disconnected / no Remote; when
  connected, live: A, B, 1, 2, the D-pad, −, HOME, +; the accelerometer in g
  and the tilt; the IR camera's dots in a field the size of what it sees; the
  battery; the attachment with the Nunchuk's stick, C, Z and motion, the
  Classic Controller's every button, both sticks and both triggers, and the
  Wii MotionPlus's three rates. Square rumbles that Remote for 0.4 s, Triangle
  asks for its status and identifies the attachment again.

A Remote is shown as *Connected* only once it has answered (a status
report came back); a value is drawn only once it was read. Plugging in or
pulling out the DolphinBar, a Remote switched on or off, an attachment
plugged in or out: the screen follows within about a second. Diagnosis
changes also go to `/data/porpoise/dolphinbar.log`.

Attachments (fixed after the first PS5 test, where a Remote's attachment
kept appearing and disappearing): turning a MotionPlus on or off makes the
Remote report its attachment gone and back. Those status reports are the
monitor's own doing and are not taken for a plug (they were, and the
identification that followed turned the MotionPlus off again, round and
round). An active MotionPlus is recognised at 0xa400fa before anything is
initialised (the initialisation would turn it off); a switched-off one is
turned on once per connection, in the mode for what's plugged into it; a
Nunchuk or Classic Controller plugged into or out of an active MotionPlus,
which only its data shows, restarts it once in the other mode. The
unencrypted initialisation is tried up to three times (a freshly plugged
attachment can answer late), and each identification writes one line with
every step and register error to `dolphinbar.log`.

**Wii Remote Speaker** (a list item of its own): short test tones through a
Remote's speaker in the format a Wii sends (4-bit Yamaha ADPCM at 3000 Hz,
20 bytes a report, 75 reports a second; set up as WiiBrew documents:
0xa20009, 0xa20001, the configuration, 0xa20008), at a moderate volume
(levels 0x08-0x30), with the packets sent and refused, the rate and the
spacing between them. Left/right picks the Remote, Cross plays, Triangle
picks the signal, Square switches the speaker on or off, L1/R1 the volume.
The speaker is muted and switched off when the screen closes.

**In a game** (after the Super Mario Galaxy test, where the Remote stopped
responding after a while): Dolphin takes a Remote whose output report fails
even once as disconnected. With the speaker fed 75 times a second through a
DolphinBar without an interrupt OUT endpoint (each report a SET_REPORT
control transfer), one refusal was enough. Now a refused speaker report is
dropped and counted (a moment of lost sound, never a disconnect), any other
refused report is sent a second time (a Remote that's really gone refuses
both), only ENXIO/ENODEV count as a device that's gone, and a report stream
that stops while the Remote still takes reports has its interrupt IN
transfers restarted once. Every 30 s, a summary line per interface goes to
`dolphinbar.log`.

**The menu Remote.** In Porpoise's menus the Wii Remote connected first
works the menus beside the DualSense (Remotes 2–4 don't); the prompts show
its buttons while it was the last one pressed, the DualSense's again on its
next press:

| Wii Remote | with a Nunchuk (held upright) | without (held sideways) |
|---|---|---|
| D-pad / Nunchuk stick | move | move (the D-pad turned: its Up points left) |
| A | confirm (Cross) | Sort & Filter (Triangle) |
| B | back (Circle) | tapped: Details, Apply (Square); held: a shift |
| 1 | Sort & Filter (Triangle) | confirm (Cross) |
| 2 | R2 (GameCube / Wii saves, next page) | back (Circle) |
| + / − | next / previous tab (R1 / L1) | the same |
| Nunchuk C / Z | Favorite (Options) / Details (Square) | — |
| B + 1 / B + 2 / B + − | — | Options / R2 / L2 |
| HOME | nothing | nothing |

The buttons come from the Remote's latest report itself
(`usbhid::latest_report`), not from the monitor's thread, which can be busy
in a write for a moment; a report older than 250 ms is no button. The menus
ask for continuous reports (0x30, or 0x32 with the Nunchuk's bytes): the
console's own driver can sit on the DolphinBar's interfaces and take
reports, and a lost "let go" reported only on a change kept the menu
moving. The Nunchuk's stick is read after its own calibration (min, centre,
max per axis) with a small dead zone, and only from report 0x32. The monitor
runs light for the menus (LEDs, buttons, the Nunchuk; a MotionPlus off) and
full only on the controller test screen itself.

**HOME held for a second in a game** opens Porpoise's in-game menu: the game
doesn't see HOME while it's held, and a short press reaches it as HOME is let
go (a report with HOME, then the one without), so the Wii's own HOME menu
still opens. The in-game menu takes the Remote's D-pad, A and B.

**Wii Remote Setup** (Settings → Wii Remote) shows, with real Wii Remotes, a
page of its own: a Remote upright with a Nunchuk and one sideways without,
the one the DolphinBar sees now marked; Triangle opens the DualSense's own
setup.

**Real Wii Remote Speakers** (Controllers & Accessories, the in-game menu,
a game's own settings): off holds every speaker report back (no transfer)
and keeps the speakers off and muted; the TV's sound and the DualSense's
speaker are untouched.

**Notices**: "Wii Controller Port n connected / disconnected", over the
menus and over a game, about 3 s each, up to four, in order: from the
monitor (a Remote that answered, one refused twice; none for Remotes there
from the start, status requests, MotionPlus switches or the hand-over to a
game) and, in a game, from Dolphin itself (`porpoise_take_wiimote_event`, a
few lines in `WiimoteReal.cpp`, also written to `dolphinbar.log`). And
"Port n: Nunchuk plugged in / pulled out": in the menus with the
attachment's name (a MotionPlus switched on or off around it doesn't
count), in a game from the status reports once settled for 1.5 s (a game
switching a MotionPlus makes the attachment go and come back in a moment).

**Telling failures apart** (`dolphinbar.log`): reports arriving that nobody
takes (the reader stopped), reports whose content hasn't changed for 10 s
(the Remote's data is frozen), no reports while the Remote takes them (the
transport: reads restarted), and every 30 s a summary per interface. Empty
slots are asked at most every 2 s (Dolphin's scanner asks twice a second, on
the control pipe the speaker data uses).

How it reaches the hardware: `src/porpoise_dolphinbar.cpp` runs a monitor
in the menus (light) and on the controller test (full). It uses the same
hidapi layer as the Dolphin core (`porpoise_usbhid.cpp`), opens the four
interfaces, and speaks the Wii Remote's protocol itself (status, register
reads for calibration and the attachment's id, the MotionPlus turned on
with its pass-through modes, the IR camera in basic mode, continuous report
0x37). **Only one of the monitor and the Dolphin core ever has the
DolphinBar**: the core's run calls `hold_for_game()` before it loads Dolphin,
which stops the monitor, leaves each Remote as Dolphin expects it (no rumble,
camera off, MotionPlus off, no continuous reports), closes every interface
and keeps the monitor off until the game has ended. `hid_open_path` itself
refuses an interface that is already open. No driver is ever detached
unasked.

## How it works

```
 Wii game (emulated)                                         PS5 USB
      │                                                        │
 Dolphin core: WiimoteReal (protocol, attachments, IR, M+)     │
      │   Core/HW/WiimoteReal/IOhidapi.cpp, unchanged          │
      │   hid_enumerate / hid_open_path / hid_read_timeout /   │
      │   hid_write / hid_close  (undefined in the core)        │
      ▼                                                        │
 core loader binds them to the title (tools/core-imports.py)   │
      ▼                                                        │
 Porpoise: src/porpoise_usbhid.cpp ── /dev/ugenB.A ──────────── DolphinBar 057e:0306
           USB_FS_* (interrupt IN reads, interrupt OUT)          4 HID interfaces
           USB_DO_REQUEST (HID SET_REPORT, descriptors)          (one per Remote)
```

- **Why hidapi, libusb and real Wii Remotes were off.** The PS5 port's patch
  (`patches/dolphin/ps5-port.patch`) left out libusb, hidapi and cpp-ipc: the
  bundled hidapi needs libusb or Linux's hidraw/udev, and a PS5 title has
  neither a libusb it can link nor hidraw. Dolphin's libretro core already has
  a "Real WiiMote" device (`RETRO_DEVICE_REAL_WIIMOTE`) and a scanner thread;
  without a backend the scanner found nothing.
- **What the console offers.** The PS5 is FreeBSD underneath: every USB device
  has a `ugen` node with FreeBSD's `USB_FS_*` and `USB_DO_REQUEST` ioctls.
  BlueV (github.com/0xZ3n0x/BlueV) runs a USB Bluetooth adapter through them
  on the console, which shows they work for a jailbroken process (BlueV is a
  payload; whether Porpoise, a title, may open a ugen node is the first thing
  the hardware test checks). `libSceUsbd` (the system's libusb) was the other
  candidate; it needs loading a system module at run time, and ugen is what
  BlueV proved.
- **The changes to Dolphin** (`patches/dolphin/ps5-port.patch`):
  - CMake: on the PS5, `Hidapi::Hidapi` is an interface target with hidapi's
    header only, so `IOhidapi.cpp` is built and its calls are left for the
    title.
  - `WiimoteReal.cpp`: a queue of real Remotes connected to and taken off the
    game's ports, `porpoise_take_wiimote_event`, for the notices.
  - A crash when a real Remote goes in a game (switched off, battery,
    range): libretro's `WiimoteDevice::SetSource` ignores "no source", so the
    emulated Bluetooth kept the Remote Dolphin had deleted and called it on
    the next input update. `WiimoteCommon::UpdateSource` now clears the
    source of a real Remote that goes (`WiimoteDevice::ClearSource`), also
    before a source change hands the Remote to the pool; emulated Remotes keep
    libretro's behaviour. Not specific to the DolphinBar.
  - Reconnecting: a real Remote the game let go waits, as on a Wii, for a
    button. `Wiimote::Read` dropped every report while not linked, so the
    press was never seen (on a PC the Remote makes a new Bluetooth connection
    instead; at a DolphinBar it stays connected). The buttons of reports
    while unlinked are now kept for `GetCurrentlyPressedButtons`.
  - With Debug Logs, Dolphin's own Wii Remote log (`WIIMOTE`) in core.log.
- **The title side**, `src/porpoise_usbhid.cpp`: lists the ugen nodes, reads
  each one's device information (no bus traffic) and takes only Nintendo's
  057e:0306 and 057e:0330; reads the DolphinBar's configuration descriptor
  (the kernel's copy, else GET_DESCRIPTOR, else its known layout of IN
  0x81..0x84); opens the node once for all its interfaces; keeps two interrupt
  IN reads armed per open interface; sends output reports to an interrupt OUT
  endpoint if the interface has one, else as a HID SET_REPORT. A slot without
  a Remote stalls the output report; that is `hid_write` = -1 with `EPIPE`,
  which is how Dolphin's scanner (`IsDeviceUsable`) and its once-a-second write
  test recognise "no Remote" and "Remote gone". Never opened: the console's
  Bluetooth chip (`ugen0.2`), root hubs and every other device.
- **Transfers are set up once per device and kept** while it is plugged in:
  opening and closing an interface (Dolphin's scanner, twice a second; the
  hand-over to a game; the game's end) tears nothing down. A teardown
  (USB_FS_CLOSE / USB_FS_UNINIT / close) makes the kernel drain running
  transfers, a DolphinBar read only ends with a report, and a drain that
  never ended left the process impossible to close (the PS5 needed its power
  button). When a device does close (unplugged, Porpoise's exit), every
  transfer is stopped first and waited for at most 500 ms; if one still runs,
  the node is left open and that is logged. Porpoise's exit and restart let
  the DolphinBar go first and wait at most 1.5 s. Every call into the USB
  stack that can wait is tracked: one over 200 ms logs `came back after N
  ms`, one that doesn't come back is logged after 2 s (`USB: ... and not back
  yet`).
- **Porpoise**: `Settings::wii_source` (`wii_source` in the settings files) and
  `WiiConfig::real_remotes`. With it, every Wii port gets the core's real Wii
  Remote device, the DualSense's Wii features (pointer, motion, two-controller
  play, the Nunchuk motion file, the Wii Remote Setup before a game) stay out
  of it, and the DualSense keeps Porpoise's menu.

## Tests

| Test | Where | What it shows |
|---|---|---|
| `tests/usbhid/run.sh`, leaving | PC | the scanner's open/close tears nothing down, a slow call is reported, reads that don't stop don't hold up the close, Porpoise's exit waits at most its time |
| `tests/usbhid/run.sh` | PC | `porpoise_usbhid.cpp` against a model of FreeBSD's ugen with a simulated DolphinBar (with and without interrupt OUT endpoints): enumeration, Remote present / absent (EPIPE), reports kept in order per slot, four Remotes with Dolphin's read and write threads and a scanner at once, a Remote lost and found, unplug and replug, unreadable descriptors, the console's driver; plainly, with ThreadSanitizer and with AddressSanitizer + UBSan. Compiled against Dolphin's real `hidapi.h`. |
| `tests/usbhid/run.sh`, the controller test | PC | the Wii Remote's reports decoded from WiiBrew's byte layouts (buttons, accelerometer, IR basic and extended, Nunchuk, Classic Controller, MotionPlus and its pass-through modes, status, attachment ids, calibration); the monitor against a simulated DolphinBar with simulated Remotes that answer register reads and writes: Remotes found, attachments identified, the MotionPlus turned on, live input, rumble, a Remote lost and found again, one joining, the DolphinBar unplugged and replugged, access refused, and the hand-over to a game (every interface closed, the node and its transfers kept, the Remotes left quiet, Dolphin's scanner can open them, the monitor stays off until released) |
| `tools/ui-preview/build-controllers.sh` | PC (lavapipe) | renders the new screens with states set by hand, for the layout (English and German) |
| static_asserts in `porpoise_usbhid.cpp` | PS5 build | the ioctl numbers and structure sizes equal the SDK's FreeBSD headers |
| `tools/check-core.py` | PS5 build | the core's ABI (the build runs it) |
| A Wii game with real Remotes in Porpoise | **PS5** | Super Mario Galaxy 1 and 2: input, motion, IR, Nunchuk, rumble, speaker; long sessions; a Remote off and on; leaving the game and Porpoise |
| Settings → Controllers & Accessories with a real DolphinBar | **PS5** | the bar, a Remote Plus with Nunchuk and MotionPlus, live test, diagnosis, unplug and replug |
| Porpoise's menus with the Wii Remote | **PS5** | the layouts above, upright and sideways, the prompts, the notices, HOME held in a game |
| `tools/dolphinbar-probe` stages 1 and 2 | **PS5** | not needed in the end: Porpoise itself showed a title may open `/dev/ugen*` |

## Hardware tests

See `tools/dolphinbar-probe/README.md`. In order:

1. **Stage 1** (`dolphinbar-probe-1.elf`, read only): is the DolphinBar seen,
   can its node be opened, its descriptor read, and does the console's driver
   hold its interfaces.
2. **Stage 2** (`dolphinbar-probe-2.elf`): status request to each slot, LEDs,
   rumble, attachment ids, buttons and motion, the IR camera, Remotes switched
   off and on.
3. **Porpoise**: a Wii game with *Real Wii Remotes*; then `dolphinbar.log`.

What the console showed: a title may open `/dev/ugen*`; the console's own
driver attaches to the DolphinBar's interfaces (`driver console's`) and takes
some reports; the W010 has no interrupt OUT endpoint (every output report is
a SET_REPORT, which the console times out after 5 s when the bar doesn't
answer, e.g. a Remote just switched off).
