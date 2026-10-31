# DolphinBar test payloads

Two payloads for the PS5 that test `src/porpoise_usbhid.cpp`, the USB code
Porpoise uses for real Wii Remotes, on the console before Porpoise does
(`docs/DOLPHINBAR.md`). Build them with:

```sh
bash tools/dolphinbar-probe/build.sh
```

which writes `build/dolphinbar-probe/dolphinbar-probe-1.elf` and `-2.elf`.
Send one at a time to the jailbreak's ELF loader, e.g.
`nc -q0 <console-ip> 9021 < dolphinbar-probe-1.elf`. Each shows a
notification when it starts and ends and writes its log to
`/data/dolphinbar-probe/probe-<n>.log`.

| | What it does | Risk |
|---|---|---|
| **1** | Lists every USB device's ugen node with its device information (the read-only ioctl BlueV uses), skipping the console's Bluetooth chip. For a DolphinBar: its configuration descriptor (hex), its interfaces and endpoints, and whether the console's driver has them. | Reads only. Nothing is sent to a Remote, no transfer is started. |
| **2** | Turn the Remotes on when it says so (5 s). A status request to each of the four slots (which slots have a Remote), then for each Remote: the player LED, a 0.25 s rumble, its status and attachment (Nunchuk, Classic Controller, MotionPlus), 20 s of buttons and motion (press buttons, move it), 10 s with the IR camera on (point at the DolphinBar), and 30 s in which to switch a Remote off and on or connect another. About 70 s in all. | Talks to the Remotes the way Dolphin does; touches no other device. |

An empty file `/data/dolphinbar-probe/detach` has stage 2 detach the
console's driver from the DolphinBar's interfaces (only if stage 1 showed it
attached and stage 2 got few reports).

What to send back: both log files, and for stage 2 what you did when (which
buttons, which attachment, when a Remote was switched off).
