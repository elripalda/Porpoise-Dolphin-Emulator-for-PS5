# The Switch 2's GameCube controller

Nintendo's GameCube controller for the Nintendo Switch 2 (Nintendo Switch
Online, USB `057E:2073`) plays as **player 1's GameCube controller** in
Porpoise, beside the DualSense: both work at once, and the rumble goes to
the one played with last. It also works Porpoise's menus (A confirms, B goes
back, X, Y, START, Z, ZL, L and R as Square, Triangle, Options, R1, L1, L2
and R2), with its own button prompts, and **HOME** opens the in-game menu.

## By its cable

Plug it into a USB port of the PS5 with a USB-C **data** cable. It's found by
itself (Settings → Controllers & Accessories shows it). Porpoise reads its
stick and trigger calibration from the controller and starts its input as
SDL's HIDAPI Switch 2 driver does.

## Without its cable: a USB Bluetooth adapter

The PS5's own Bluetooth is not used. Plug a USB Bluetooth adapter with a
**Realtek RTL8761B or RTL8761C** into the PS5 (tested: **TP-Link UB600**,
`37AD:0600`, an RTL8761BU). Porpoise finds it by itself, gives it its
firmware (`assets/bluetooth`, unmodified from linux-firmware) and listens.

1. Press the **SYNC** button on top of the controller. It connects (a short,
   soft rumble) and is paired to the adapter.
2. From then on, any button connects it again, also after Porpoise or the
   PS5 was restarted. A Switch 2 it was paired with before may need it
   paired again with its SYNC button there.

Controllers & Accessories → Controller Test & Diagnostics → USB Bluetooth
Adapter shows the chip, the firmware, the adapter's address, the link and
the Bluetooth LE devices nearby.

By Bluetooth the controller has no rumble motor to drive directly: the game's
rumble plays the controller's own soft vibration preset while it lasts.

## For the curious

- USB: `src/porpoise_ns2gc.cpp` (ugen transfers, the set-up commands on
  interface 1, the 64-byte input report on interface 0, report 3 for rumble).
- Bluetooth: `src/porpoise_bt.cpp` (HCI over the adapter's ugen node, the
  Realtek patch as Linux's `btrtl` uploads it, LE scanning and connection,
  L2CAP and ATT, the controller's command channel, its input report on
  handle `0x000A`, pairing).
- Everything they do goes to `/data/porpoise/dolphinbar.log`, lines `ns2gc:`
  and `bt:`.
- PC tests: `tests/ns2gc/run.sh` (a simulated controller), `tests/bt/run.sh`
  (the Realtek patch files, advertising data).

Thanks to SDL, ndeadly's switch2_controller_research, Nitrama's
switch2-controllers-linux and BlueV (see CREDITS.md).
