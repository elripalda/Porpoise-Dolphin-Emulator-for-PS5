# Changelog

## Unreleased

**New**
- **Real Wii Remotes through a Mayflash DolphinBar** (Settings → Wii Remote →
  Wii Remotes → Real Wii Remotes): original Wii Remotes and Wii Remote Plus
  controllers with the Nunchuk, Classic Controller and MotionPlus, IR,
  rumble and speaker, for up to four players, in a USB port of the PS5.
  See docs/DOLPHINBAR.md.
- **Controllers & Accessories** (Settings): the DualSense controllers, the
  DolphinBar and Wii Remotes 1–4 at a glance, a live controller test with a
  diagnosis, and a speaker test.
- **Porpoise's menus with a Wii Remote:** upright with a Nunchuk or sideways
  without, with its own button prompts; HOME held for a second in a game
  opens the in-game menu.
- **Notices** when a Wii Remote or its Nunchuk is connected or taken off.
- **The Switch 2's GameCube controller** (Nintendo Switch Online, 057E:2073):
  by its USB cable, or without it through a USB Bluetooth adapter with a
  Realtek RTL8761B/C (tested: TP-Link UB600). It is player 1's GameCube
  controller beside the DualSense, with its analog triggers and rumble; it
  works Porpoise's menus with its own button prompts, and HOME opens the
  in-game menu. By Bluetooth it is paired once with its SYNC button and
  then connects with any button. See docs/NSO-GAMECUBE.md.
- **Controllers & Accessories** lists only what's plugged in.

**Fixed**
- A game no longer crashes when a real Wii Remote is switched off in it, and
  a Remote switched on again connects again with a button press.

## 4.0 — 2026-10-21

**New**
- **Frame Generation (beta)** (Settings → Video, and the in-game menu):
  frames made between the game's own, from ps5-framegen. A 30 fps game is
  shown at 60; with the PS5 set to 120 Hz, a 60 fps game at 120. Off by
  default. Needs V-Sync.
- **Steady 30 FPS** (under Frame Generation): the game's frames shown at an
  even 30, with frame generation making it 60. For games that swing between
  30 and 60.
- **Simpler Settings:** Settings and a game's settings show just what most
  players need. **Show All Settings** (the first row) brings back every one.
- **Auto Boost** (a game's settings): the settings that run a game best on
  PS5, as tested, with one press. Was Recommended.
- **A calmer Porpoise theme:** a fainter grid, no light beams, single-color
  glass edges instead of rainbow ones, and darker tile sides so the box art
  leads.
- **Star Cube** looks like the GameCube's own menu now: a glossy glass cube
  with a bezel, Game Play, Calendar, Memory Card and Options on its sides,
  and each side's picture built from little squares.
- **Smooth Edges (FXAA)** screen filter: smoother edges at almost no cost.
- **Player 1's Port** (Controls, and the in-game menu): put your controller in
  another GameCube port, mid-game too (Psycho Mantis).
- **Change Disc** in the in-game menu: a game's other disc, or another game's
  disc for games that ask for their expansion's. Games on two discs also
  change by themselves when they ask for it.

**Faster and steadier**
- Save states and screenshots are written away from the emulator's cores, so
  quick saves don't hitch.
- The controllers are read once a frame instead of on every one of the
  game's thousands of checks.
- Less logging while you play.
- Shaders built before a game starts use three threads instead of eleven, so
  a big shader cache no longer runs the PS5 out of memory at boot. If a game
  ever stops before its first picture, its shader list is set aside the next
  time (Phantasy Star Online Episode III).

**Fixes**
- Luigi's Mansion (PAL) and other games whose Dolphin cheat codes start with
  the code's ID line now start with cheats on.
- Cheats whose names have an "=" in them stay on.
- NFS shares with version 3 turned off (TrueNAS) are tried with version 4.
- A rare hang when the GPU refused a frame.
- A crash with a stray folder in the Wii save folders.
- The launch screen no longer draws over the game while it builds shaders.
- Very large cover or screenshot images are skipped instead of crashing.
- Rumble no longer sets off a shake (the Nunchuk shake in two-controller play).
- Quick resume keeps the right disc's place.
- The launch screen keeps moving while a game is checked for achievements.
- Achievements stay on when a disc the Wii Menu won't take starts directly.
- Cover downloads start again after a Wii System Update is cancelled.
- Dolphin 2609a's save-state check and achievement code approvals.

**Removed**
- The test-only options (Debug Logs, Performance Report and the hidden
  developer options) are gone from Settings. Porpoise still keeps its logs
  for bug reports: **Settings → About → Report a Bug**.

## 3.0.1 — 2026-10-10 (test build; its changes shipped in 4.0)

- **GameCube Menu and Wii Menu** (Settings → Console): start your own
  console's menu, with a game in the drive or none. The GameCube Menu needs
  your own BIOS; the Wii Menu comes from Wii System Update, your Wii System
  Data or its WAD.
- **Wii Channels** (Settings → Console): put your channels (WADs) in the Wii
  Menu. Choose them, and the Wii Menu opens with them in it.
- **Wii System Update** (Settings → Console): the Wii's system software (the
  Wii Menu, its IOS and channels) downloaded from Nintendo's servers, as
  Dolphin's Online System Update does, so there's a Wii Menu without a
  backup.
- **Add a Code** (a game's Cheats and Patches): type in a Gecko code with a
  name, as Dolphin's Add New Code; it's saved in /data/porpoise/cheats.
- **Wii System Data:** remove an imported Wii to start fresh. What was in it
  is kept in saves/User/Wii-removed, not deleted.
- Start Wii Discs in the Wii Menu: GameCube games, and Wii discs of another
  region than your Wii's, start directly instead of showing "An incorrect
  disc has been inserted".

## 3.0 — 2026-10-10

- **Wii games online:** an Online Hub (Settings → Online) with custom
  servers saved by name and a test, WiiLink WFC, Wiimmfi and the
  WiiConnect24 channels. Custom servers get the secure connection fix on
  their own, no cheat file needed.
- **Network shares:** games from a computer or NAS (SMB or NFS), found on
  your network.
- **Wii MotionPlus** with the DualSense's gyroscope (Wii Sports Resort,
  Skyward Sword, Red Steel 2...).
- **Wii buttons:** two sets of your own per Wii controller, for every game or
  one; Pointer Range for games whose pointer can't reach an edge (Zack &
  Wiki); a GameCube controller in a Wii game turns the Wii Remote off.
- **Stick Sensitivity and Stick Dead Zone** for the control stick (Settings
  → Controls, a game's own settings and the in-game menu), for games whose
  steering is twitchy (F-Zero GX).
- The Wii pointer reaches the top of the screen with the Sensor Bar below
  the TV (Mario Kart Wii), and a box flips to its back smoothly.
- **Wii System Data (beta):** import your own Wii's BootMii NAND backup.
- **HD texture packs** listed in Settings → Textures with the game each is
  for, and held to what the console's memory allows.
- **Cheats and Patches** as cards; .txt code files with several codes read
  properly.
- **Riptide Boost (beta)**, universal widescreen (beta), FSR 1, Sharpen,
  Extra Bloom, picture color controls and new screen filters.
- **Looks:** new themes, colors and TV borders; Spines and Spotlight views;
  Recently Played as a Dock, Floating boxes or Discs; Settings in more
  sections; screenshots.
- **Smoother games:** a PAL game switched to 60 Hz runs at 60, and games no
  longer run a little fast on some displays (the 60/62 fps and skipping
  sound in Mario Kart Wii).
- **Faster:** quicker scrolling (hold a direction, L2 / R2 by letter), a
  quicker start with games remembered, and USB games shown before network
  shares.
- Other consoles' games in shared roms folders are left out instead of
  crashing; a crash after the jailbreak on 12.70 fixed; many smaller fixes.

## 3.0 Alpha Build 7 — 2026-10-10

- **Recently Played** has looks (Settings → Interface): Off, Dock, Floating
  (the boxes alone, growing like a dock's icons) and Discs.
- **Wii System Data (beta)** (Settings → Console): your own Wii's BootMii
  NAND backup imported, with its settings, Miis, channels, saves and the
  certificates Wiimmfi checks. Saves Porpoise had for the same games are
  kept in saves/User/Wii-before-NAND.
- Startup: disc headers are remembered, so a start reads none for games it
  has seen; GCZ titles read without their whole block table; USB drives'
  games show before a slow or offline network share's.
- Online: the secure connection fix only changes real "https" addresses.

## 3.0 Alpha Build 6 — 2026-10-10

- PAL games switched to 60 Hz in their own menu run at 60 (the pacing
  stayed at 50).
- Frame pacing: a display whose vblank waits don't hold is caught and the
  game's own clock takes over, instead of running about 4% fast (60/62 fps
  and the sound jumping every few seconds); one late frame no longer turns
  into a run of them.
- HD texture packs: the memory they may use is held to what the console can
  give, so a long session with a big pack doesn't end in a crash.
- Wii buttons: two sets of your own per Wii controller (Mine 1, Mine 2) next
  to Porpoise's, picked for every game or one (Controls → Customize Buttons,
  L1 / R1; or Wii Remote → Wii Buttons).
- Wii Remote: Pointer Range (Auto, Standard, Wide, Extra Wide); Auto is wide
  for Zack & Wiki, whose pointer couldn't reach the top of the screen.
- A GameCube controller in a Wii game no longer leaves the Wii Remote on too.
- A Porpoise folder made by hand on extended storage is offered at the first
  start.
- Cheats: a .txt with several codes keeps them apart, by name; files saved
  with a byte-order mark are read.
- After being freed by the jailbreak on 12.70, Porpoise no longer crashes
  putting its own folder back.
- Online hub: lighter, three servers at a time; Retro Rewind's own servers
  explained. A Wii game's every socket call is logged only with Debug Logs.
- A game's saved setups can be reached in its settings again.
- Startup: the library isn't rebuilt (covers faded in again, the cursor moved
  back) when the background search finds the same games; GameTDB info and
  long titles are quicker to read and fit.

## 3.0 Alpha Build 5 — 2026-10-09

- **Online Hub** (Settings → Online): custom servers saved by name, a test
  that the chosen one answers, WiiLink WFC and Wiimmfi in a few steps, and
  the WiiConnect24 channels. With a custom server on, Wii games get the
  secure connection fix on their own; no cheat file needed. A WiiLink WFC
  code saved as <game ID>.txt in /data/porpoise/cheats is picked up.
- Settings in more sections (Downloads, Storage, Textures, Save States,
  Emulation, Console, Online), in a list that scrolls.
- **Textures**: every HD texture pack found, the game it's for and how many
  textures it holds; a pack named for no game in the library says so.
- A game's Cheats and Patches as cards: what each code is, who made it, and
  a switch.
- Spines view: cases on the shelf show their spines only, and one without
  spine art gets a printed spine with its title. Helix view: no more noise
  behind the column. Details: the whole description starts below the disc.
- A game never runs faster than its own speed on a display a little faster
  than it (the sound skipped every few seconds in Mario Kart Wii).
- The search skips folders named for other consoles (psp, ps2, xbox...).
- A home screen tile for another console's game says so; starting a game
  from a share no longer waits on the network first; L2 / R2 shown in the
  library's buttons.

## 3.0 Alpha Build 4 — 2026-10-09

- Library: other consoles' disc images (a PS2 or PSP .iso in a shared roms
  folder) are left out, and one started anyway says so instead of crashing.
- Library: hold a direction and the scrolling speeds up; L2 / R2 jump to the
  previous / next letter (a page when sorted another way).
- The Wii filter shows Wii discs only; WiiWare, Virtual Console, channels and
  homebrew stay under Channels.
- A Wii controller changed in the in-game menu applies when the game goes on,
  without starting it again.
- NFS: a wrong export name says so and lists the computer's exports.

## 3.0 Alpha Build 3 — 2026-10-09

- Wii games online: a port the console refuses a game (Mario Kart Wii's for
  its players, error 67010) is taken lower instead.

## 3.0 Alpha Build 2 — 2026-10-09

- Wii games online: no more freeze connecting (Mario Kart Wii), the console's
  real address for the game, WiiConnect24 downloads can look up their servers,
  and a custom DNS server gets three tries before the console's own.
- Universal widescreen (beta) uses the widescreen hack where it can't widen
  the game itself, instead of staying 4:3.
- Network steps of a Wii game going online are kept in core.log.

## 3.0 Alpha Build 1 — 2026-10-09

A test build, shared ahead of 3.0. Everything in 3.0 Beta 1 below, and:

- **Network shares:** games from a shared folder on a computer or NAS (SMB or
  NFS). Porpoise finds the computers on your network that share files, has a
  number pad for an address, and lists their shared folders to pick from.
- **Wii games online:** the emulator can look up servers' names now (it
  couldn't before), for Wiimmfi, custom servers and WiiConnect24 through
  WiiLink; a DNS server for a custom server in Settings → System.
- **Wii MotionPlus**, played by the DualSense's gyroscope: on by itself for
  Wii Sports Resort, Skyward Sword, Red Steel 2 and Wii Play: Motion, or set
  per game.
- **Universal widescreen** (beta), and an optional 16:9 code for Mario
  Superstar Baseball.
- Settings: one Move Porpoise's Folder row, and the menu sounds in Interface.
- An ALPHA tag by the logo, and a note at the first start on reporting
  problems.

## 3.0 Beta 1 — 2026-10-09

- **Riptide Boost** (beta, Settings → System) speeds up games that run slow
  on the PS5.
- **Enhancements** in Settings: the everyday picture options first, the rest
  under Advanced Options. FSR 1 upscaling, Sharpen (CAS), Extra Bloom,
  saturation, contrast and warmth, and new screen filters (16-Bit, NTSC
  Composite, Aperture Grille, a gentler 8-Bit).
- **Looks:** four new themes (8-Bit, Beige, Translucent, Arcade), eight more
  colors, seven new borders (80s, wood-grain, 90s and black TVs, a beige
  monitor, Revolution and Star Cube). Themes, borders, filters, fonts and
  sounds apply as you choose them; Apply is for the technical settings.
- **Library:** two new views, Spines and Spotlight, and a Recently Played dock
  along the bottom (Settings → Interface). A Retro TV view for Memory Cards.
- **Screenshots:** Take Screenshot in the in-game menu, or touch pad + Square.
  They're in Settings → Screenshots, and in a game's Details (except in the
  Revolution theme).
- **Menu sounds:** Crisp by default, with Soft and Porpoise's original sounds
  a choice away.
- **Accessibility:** a Smaller text size and Bolder Focus.
- **GameCube BIOS** found under any name in /data/porpoise/bios.
- Optional 16:9 codes for Virtual Console N64 games (Admentus64's Enhancement
  Codes), in each game's Cheats and Patches.
- The trigger click is Firm for everyone (once, on updating; Off, Light and
  the new Extra Firm stay a choice), Cheats and Patches is out of beta, and
  the cheats folder is no longer made automatically.
- Labels in title case, the whole description on the back of the box in
  Details, and optimizations and fixes throughout.

## 2.7 — 2026-10-08

- **Home screen tiles:** a game's settings → Home screen → **Add to home
  screen** gives it its own tile on the PS5 home screen, which opens Porpoise
  straight into the game. **Tile art** shows the icon and background as the
  home screen will: the cover, the back of the box, a screenshot, the title
  screen or your own picture, cropped and zoomed, or whole over a blur, white,
  black or a Porpoise pattern. Other apps can start a game the same way
  (`--rom`, thanks to MartinPham, #11).
- **Settings wait for Apply** (Square), ask before you leave with changes,
  and offer a restart for the few that need one; in the in-game menu too.
- **Inverted sticks:** the control stick and the C-stick, up-down, left-right
  or both, for each game.
- **Scanlines, Shadow mask and LCD** screen filters.
- **First-start setup** questions, and a **Diagnostic test** in Settings →
  About.
- Faster save states, with pictures of the whole image.
- **Download covers again**, for every game or just one.
- **Stay in the sandbox** (Settings → Games) for consoles where freeing
  Porpoise closes it.
- Optimizations, stability improvements and fixes for reported bugs.

## 2.5 — 2026-10-06

Everything in 2.5 Beta 1 (smoother sound, sound presets, the in-game Audio
tab), and:

- **GameCube controller for Wii games** that take one, such as Super Smash
  Bros. Brawl and Mario Kart Wii (Settings → Wii Remote → Wii controller, or
  the in-game menu's Controls tab).
- **Graphics mods:** Dolphin's built-in bloom, depth of field and HUD mods for
  about fifty games, in each game's Graphics settings.
- **Controller extras** in Settings → Controls: each player's light bar color,
  a turbo button, quick save buttons (touch pad + L1 saves, touch pad + L2
  loads) and, in beta, a trigger click on L2 and R2 like the GameCube's.
- Porpoise finds its folder on extended storage or a USB drive after a
  reinstall, and can switch to a Porpoise folder that's already on a drive.
- Optimizations and fixes for reported bugs.

## 2.5 Beta 1 — 2026-10-06

- **Smoother sound:** when a game runs below full speed, its sound now comes
  through Dolphin's own mixer, as Dolphin plays it on a PC, which covers the
  gaps instead of crackling.
- **Sound presets** in Settings → Audio: Smooth, Responsive, Extra smooth and
  Classic (2.1), with the audio buffer and gap filling on their own rows.
- An **Audio** tab in the in-game menu: volume, mute, the sound preset and the
  rest, while you play.
- Games no longer run faster than their own speed (some did on a PS5 set to
  120 Hz output).
- Optimizations and fixes for reported bugs.

## 2.1.1 — 2026-10-06

- **Achievements lists:** an Achievements tab in the in-game menu, and a
  game's list from its Details (Square), with badges.
- **Widescreen done per game:** games with a widescreen code (Warped Polygon's
  collection, or Dolphin's own) play in true 16:9 by default; the rest stay
  4:3 unless you choose the emulated hack, which now warns what it does.
- Achievement popups drawn by Porpoise when the console's own can't show.
- A **Patches** tab in the in-game menu: the game's widescreen state and its
  cheats and patches, and a Cheats and patches section in every game's settings.
- A clearer message, with the reason, when a game doesn't start.
- RetroAchievements fixes, stability improvements, optimizations and fixes for
  reported bugs.

## 2.1 — 2026-10-05

RetroAchievements with PS5-style trophy popups, your own cheats folder,
custom buttons for every Wii controller, Porpoise's folder on extended
storage or a USB drive, an Online switch for WiiLink, and fixes for what
players reported after 2.0.

### RetroAchievements (softcore)
- **Sign in from the library:** **L1 + Square** (or Settings → Games →
  RetroAchievements) opens the account panel, with an on-screen keyboard.
  Porpoise keeps a sign-in token in `retroachievements.ini`, never your
  password. Sign out from the same panel.
- **Unlocks pop up like PS5 trophies:** the achievement's badge, its title and
  the trophy sound, in the PS5's own notifications (the Trophies channel, so
  your trophy notification settings apply). Completing a game plays the
  platinum sound. When a game starts, a popup says how many you've unlocked.
- The popups go through your jailbreak's ELF loader (port 9021), as PS5SX2's
  do; without one they're plain notifications.
- Dolphin's own RetroAchievements support, built into the PS5 core: every disc
  format Porpoise plays is recognized, and save states keep your progress.
  Softcore only: hardcore mode isn't available.
- Leaving a game waits a few seconds for an unlock still being sent.

### Cheats and buttons
- **Your own cheats folder:** put a game's codes in `/data/porpoise/cheats/`
  as `<game ID>.ini` (Dolphin's format, Action Replay or Gecko) and they turn
  on by themselves when the game starts. Each shows in the game's **Cheats**
  with its own switch. Wiimmfi's online patch codes go here too.
- **Customize buttons for Wii controllers:** Remote + Nunchuk, Remote,
  Sideways and Classic each get their own layout (Settings → Controls →
  Customize buttons, L1 / R1 pick the controller).

### Video and system
- **Output resolution** is now the first row of Video, and **Match the PS5**
  is the default: Porpoise keeps the console's own output, so the TV doesn't
  switch modes when Porpoise opens.
- **Online (beta)** (Settings → System): WiiConnect24 channels through
  WiiLink.
- **Move Porpoise's folder** (Settings → Games) to extended storage, a second
  extended drive or a USB drive: settings, saves, save states, covers and
  texture packs move together, with a progress bar and Circle to stop.

### Fixes
- The **Custom textures** row no longer disappears from Details after you play.
- A game with a texture pack says so as it starts: **Custom textures: N found**.
- Porpoise asks the jailbreak to leave the app sandbox before anything else
  starts, which should stop it closing right after Lapy frees it.
- The sandbox message has **Don't show again**, and Settings → Games →
  *Sandbox message at start* turns it back on.

### Coming in 2.5
- **Netplay** is next: playing GameCube and Wii games together online.

## 2.0 — 2026-10-05

A big update: faster and steadier games, WiiWare, Virtual Console and
channels, homebrew apps, new sound and system settings, the DualSense's
microphone and speaker, a new look with fifteen themes and Star Cube's own
home, fonts and colors to mix, more ways to see your games and saves,
accessibility settings, four new languages and a Discord for help.

Porpoise still ships no Nintendo files: channels, the Wii Menu and the
GameCube BIOS come from your own consoles. The README says how to add each.

### Channels, WiiWare and homebrew
- **WAD files play:** WiiWare, Virtual Console and channels you made from your
  own Wii appear in the library with their names and banners, marked
  *WiiWare*, *Virtual Console* or *Channel*. A new **Channels** filter shows
  only them.
- **Homebrew apps:** an app's folder as the Homebrew Channel keeps it
  (`boot.dol` or `boot.elf` beside `meta.xml` and `icon.png`) shows with its
  name and icon.
- **WBFS and GCZ games are recognized:** their headers are read, so Wii games
  in `.wbfs` are no longer listed as GameCube, and get their covers, banners
  and Wii controls.
- **Console** (a game's settings → Controls): Auto, GameCube or Wii, for a game
  Porpoise detected wrong.
- Covers for channels and Virtual Console titles are looked up by their
  four-letter ID too.

### Sound (beta)
Settings → Audio, for every game or one:
- **Accurate audio:** Dolphin's exact sound chip (LLE), for the few games
  whose sound is missing or wrong with the fast one.
- **Wii Remote speaker:** the Remote's own sounds in the TV's sound, or from
  each player's DualSense speaker, as on a Wii.
- **Audio buffer:** Low, Normal or Safe, against crackling in demanding games.
- **Audio stretching:** a game that slows down slows its sound with it instead
  of crackling.
- **Microphone:** the DualSense's microphone as the GameCube Microphone (Mario
  Party 6 and 7; hold R3 to talk) and the Wii Speak.

### The consoles' own settings (beta)
Settings → System:
- **Wii widescreen**, **PAL games at 60 Hz** and the **sensor bar** above or
  below the TV, as the Wii's own settings.
- **GameCube boot animation** from your own GameCube's BIOS: its `IPL.bin` in
  `/data/porpoise/bios/USA`, `EUR` or `JAP`.
- **Start Wii discs in the Wii Menu**, from the Wii Menu WAD of your own Wii.
  Started once from the library, the Wii Menu stays installed.

### Fast forward buttons
- **Touch pad + R1** steps fast forward (off, 2x, 4x) and **touch pad + R2**
  fast-forwards while held, without opening the menu. Settings → Controls →
  Fast forward buttons turns them off.

### Help and community
- **The RIPALDA Discord** (discord.gg/GgDE5Vynyu) is now where to get help and
  report bugs; Settings → About has its QR code.
- **About** is tidied: Ruben's mark beside his name, @elripalda and
  ripalda.dev, and the credits in one place.

### Sturdier
- **Damaged or unusual game files can no longer close Porpoise** while it reads
  the library: the disc, WAD and banner readers check every size and value in
  a file before using it (tested against thousands of damaged files). This is
  the likely cause of Porpoise closing quietly as it started with etaHEN 2.6b's
  jailbreak; if it still happens, share `trace.txt` on the Discord.

### Faster games
- **The emulator gets cores of its own.** While a game runs, Dolphin's
  emulated CPU and its video loop each keep to a core of the PS5 that
  Porpoise's other work stays off, so neither shares a core with something
  busy. **Settings → System → Emulator on its own cores** (on) turns it off,
  for every game or one.
- **Much faster emulated CPU in 3D-heavy games.** A profile of WWE Day of
  Reckoning 2 on the console showed a third of the emulated CPU's time going
  to one correction routine: Dolphin (since 2025) rounds every
  single-precision multiply-add exactly as the GameCube does, at a cost the
  PS5 feels badly in games full of 3D math. It's now off, as Dolphin itself
  was until last year; **Settings → System → Exact multiply-add** turns it
  back on, for every game or one.
- The video thread waits more gently while it polls for work, leaving more of
  the processor to the emulated CPU.

### Smoother and steadier
- **V-Sync on the TV's own refresh.** Frames now wait on the display's vblank
  itself instead of Porpoise's own timer (which never locked to the TV in
  1.5.1). Porpoise takes the display's handle from the graphics driver as it
  opens the screen. A frame that is late doesn't wait for the next vblank, so a game
  that runs just short of 60 doesn't fall to 30. Settings → Video → V-Sync
  (on); off keeps the old timer.
- **No more waiting on the speakers.** In 1.5.1 nearly every frame waited
  about 6 ms for the sound buffer to drain; the buffer's ceiling now sits
  above the resampler's target, so it doesn't.
- **Newer Dolphin core** (libretro/dolphin after Dolphin 2609), with the fix
  that writes new shader pipelines to the cache in the background instead of
  in the middle of a frame. The core builds from source again.
- **Newer PS5 graphics driver** (Mihawk's PS5_Vulkan / PS5 Mesa dc82d01). The
  console's video output now offers 1080p, so Porpoise sends a 1080p picture
  instead of drawing every menu and game frame at 3840×2160.
  **Settings → Video → Output resolution** can choose 1440p or 4K instead
  (from the next start).
- **Memory Cards opens without a hitch:** both cards and the Wii saves are
  read in the background as the tab slides in.
- **Starts about five seconds sooner.** The menus' text is drawn from
  pictures of every letter that took about six seconds to make at each start;
  they are now kept in `/data/porpoise/cache` after the first.
- **Menus:** covers, disc art and box backs are read and decoded in the
  background and fade in, instead of stalling a frame for each cover that
  scrolls into view; memory card icons no longer wait on the GPU one by one.
- **Threaded GPU recording** (beta, Settings → Graphics, off): the driver
  records Dolphin's drawing on a thread of its own. Worth trying on a
  demanding game.
- **Quitting to the library no longer freezes:** the in-game menu and the game
  fade to black together, Porpoise saves where you were behind the dolphin,
  the library's pictures load, and the menus fade back in once they're ready.
- **No Porpoise logo flashing on the launch screen** as the game takes over
  the screen: the game's cover stays.
- **A proper start:** the console's splash stays until Porpoise is ready, then
  the dolphin rises out of black with a soft glow and stays while the library
  loads. It lifts only once the menus run smoothly: the background shows, then
  the top bar and the page fade and rise in as the music comes up.
- **No more Dolphin messages over the game:** the yellow notes Dolphin shows
  ("Saved to memory card", changed video settings and the like) are gone.

### New
- **Quick resume** (beta, Settings → Graphics, off): leaving a game from the
  in-game menu keeps where you were. Next time, Porpoise asks **Resume** or
  **Start Over**, with a picture of where you left off. Restart in the
  in-game menu starts it over too.
- **Cheats and patches** (beta): each game's settings list the codes and
  patches Dolphin knows for it, one switch each.
- **Check my setup:** shown after the first start's welcome, and any time from
  Settings → Games: a checklist of what Porpoise can see (/data, USB drives,
  games, covers) and what to do about anything missing.
- **Saves on a USB drive** (beta): Options in Memory Cards copies a GameCube
  or Wii save to the drive's `Porpoise Saves` folder; Settings → Games →
  Saves from a USB drive copies them back in.
- **Report a bug:** Settings → About saves Porpoise's logs to
  `/data/porpoise/reports` (and a USB drive), with a QR code for the Discord's
  bug reports. **Performance report** records where a slow game spends its
  time.
- **Choose a version:** Settings → About lists every release, betas included,
  to install a newer or an older one. **Beta updates** offers test versions
  as they come out.
- **Wii multiplayer:** in Wii games every other controller is another
  player's own Wii Remote, with its own pointer and motion.
- **Four new languages:** **简体中文** (Simplified Chinese), **繁體中文**
  (Traditional Chinese), **한국어** (Korean) and **Türkçe** (Turkish), sixteen
  in all. Porpoise picks them up from the PS5's own language, or Settings →
  Interface → Language. Chinese and Japanese text now wraps between
  characters, as those languages do.
- **Every menu translated again:** the texts added since the Wii work (the
  Wii Remote setup, Revolution, Wii saves and 2.0's settings) in every
  language; the Revolution clock shows the date in each language's order.
- Beta features carry a **BETA** badge in Settings.
- **New icon and backdrop** on the PS5's home screen.

### A new look
- **Liquid glass:** panels and buttons bend the room behind them at their
  edges, catch the light along their rims with a thin spectrum, and cast a
  soft shadow. Corners are rounder; the tab bar and buttons are fully round.
- **Smaller button hints** along the bottom, and only one Play: the big
  button under the chosen game is gone (Cross still plays it).
- **More motion:** highlights in Settings glide from row to row, and the
  menus come in in steps when Porpoise starts and when you come back from a
  game.
- **The in-game menu matches your theme:** its glass, colors and font.

### Themes
Fifteen themes in Settings → Interface → Theme, each with its own room:
- **Porpoise**, **Revolution** and, new:
- **Star Cube:** a home of its own. A big glass cube floats on black, turning
  slowly, small cubes drifting around it; its four edges are **Games**,
  **Calendar**, **Memory Cards** and **Settings**. The stick turns the cube to
  an edge, Cross flies in, Circle comes back out; L1 / R1 move between pages.
  - **Games:** your library on a glass page as spinning discs or covers
    (Settings → Interface → Games page); each game opens its own page with
    Play, Save states, Game settings and Save data.
  - **Calendar:** the time and date, the month with what you played each
    day, and your latest games; L2 / R2 change the month.
  - **Memory Cards:** each save a little glass cube.
  - **Settings** in violet glass with dot-matrix headings, and button hints
    that read "✕ ··· Confirm".
- **OLED:** true black, crisp text, no glow.
- **Minimal:** quiet charcoal and gray, nothing but what you need.
- **Cube:** indigo and violet on black, a glass cube turning over a grid.
- **Broadcast:** a 2000s tube TV in its frame: a curved screen, scanlines and
  pixel lettering.
- **Terminal:** green phosphor on black in a monospaced font.
- **Depth:** panes of glass drifting through deep space.
- **Aurora:** northern lights over a starry night.
- **Aero:** a bright sky, glossy bubbles and white glass.
- **Dot Matrix:** lit dots on a dark grid, dotted headings.
- **Synthwave:** a neon sunset over a racing grid.
- **Paper:** ink on warm paper in a book's typeface.
- **Crystal:** clear glass with rainbow edges, light split by a prism.

### Colors and fonts
- **Colors** (Settings → Interface → Colors) for most themes: Sapphire,
  Indigo, Spice, Emerald, Platinum, Jet, Crystal, Pearl, Ruby, Rose, Gold,
  Lime, Ocean, Sunset and Midnight Violet.
- **Font** (Settings → Interface → Font): any font in any theme: Nunito,
  M PLUS 1 (close to the GameCube's own menu lettering), Nunito + Doto,
  JetBrains Mono, JetBrains Mono + Doto, VT323, Exo 2 and Porpoise Serif.
- Choosing a theme brings its own colors and font; change either after.

### Library views
Settings → Interface → Library view, in every theme:
- **Cover flow**, as before.
- **Wheel:** the boxes stand around a turning wheel.
- **Disc flow:** your discs; the chosen one slides out of its box and spins.
- **Shelf:** rows of boxes, many at once; up and down change row.
- **Box:** one game at a time as a black plastic case with its paper insert
  on the front, spine and back. Turn it with the right stick. Covers
  downloaded from now on keep their spine.
- **List:** your games by name beside the chosen box.
- **Stack:** a deck of boxes.
- **Helix:** the boxes climbing round a column.
- **Revolution's home screen** is Tiles (the default) or Library view.

### Memory Cards views
Settings → Interface → Memory Cards view:
- **Cards**, as before.
- **Blocks:** one card at a time, drawn as a card, with its saves and what
  fills it.
- **By game:** every save on both cards and the Wii, grouped by game.
- **Cubes:** each save a little glass cube on a grid.

### Accessibility
A new section in Settings:
- **Text size:** Normal, Large or Larger.
- **Color filter** for color blindness (red-weak, green-weak, blue-weak) or
  grayscale, for the menus and, if you like, the game's picture too.
- **High contrast:** solid panels, clearer edges, brighter text.
- **Reduced motion**, **Still background** and **Larger button hints**.

### A fresh start
- **The first start** offers three looks to begin with: Porpoise, Star Cube
  or Revolution, shown live as you choose. Then the welcome and the setup
  checklist.
- **Settings → Interface → Reinitialize Porpoise** starts over as if it were
  the first time: every setting, each game's own settings too, back to how
  Porpoise ships. Your games, folders, memory cards, saves and save states
  stay.

### Languages
- The menus are now in US English (Favorite, Color, Center).
- Every new text in all sixteen languages.

## 1.5.1 — 2026-10-04

### Getting out of the sandbox
- **More reliable sandbox escape.** When Porpoise starts inside the app
  sandbox and can't see `/data` or USB drives, it now keeps asking a jailbreak
  daemon to free it for a few seconds, instead of giving up after one try. This
  targets the "worked the first time, then couldn't read /data on later
  launches" reports, where the daemon starts a moment after Porpoise or the
  first attempt loses a timing race.
- **Works with more daemons.** Porpoise now sends every request name the
  daemons in use watch for: etaHEN's and OnionHEN's, and both the classic and
  the newer owned-root **Lapy** daemon (it prepares itself the way the newer
  Lapy daemon requires, before starting any threads).
- **Clearer help.** The "can't reach /data" message now says exactly what to
  change: in etaHEN, turn on *Legacy Command Server* (etaHEN's built-in app list
  can't take Porpoise); in OnionHEN, add `PPSA99764` to `exact_title_ids` in its
  `config.ini`; or run a Lapy daemon.

### External drives
- USB drives, extended storage and the usual game folders are now searched
  **four folders deep** (two before), so games sorted into subfolders on a
  drive are found without adding the folder by hand.
- The README has step-by-step instructions for playing from a USB or external
  drive.

## 1.5 — 2026-10-04

### Revolution theme (Settings → Interface → Theme)
- A bright home screen of game tiles, twelve to a page: page arrows at the
  sides and L2 / R2 turn the page, and a floor along the bottom keeps a
  segmented clock, the date, and round buttons for Settings and the memory
  cards. Porpoise's mark and the tabs sit on top.
- **Point with the controller:** a hand pointer (drawn by Ruben) that opens
  over empty space and points over anything you can choose, leans as you twist
  the controller, with a tap of rumble and a tick as it lands on something.
  The D-pad works as well; the touch pad turns pointing on and off; R3
  centers it.
- **Wii discs' own tiles and banners:** Porpoise reads each Wii disc's banner
  (.iso, .rvz, .wia, .wbfs, .ciso, .gcz) and plays it — animated tiles on the
  home screen, the full banner and its jingle when a tile opens. GameCube games
  keep their covers.
- **Opened tile:** Cross on a tile grows it to fill the screen and opens it,
  with **Wii Controls** (or Game Settings) and **Start** below, save states,
  save data and favorite; Triangle turns between the banner and the game's
  facts. Circle shrinks it back into its tile.
- Every other screen goes white with it: Memory Cards, Settings, the Wii Remote
  setup, the guide, dialogs and the in-game menu. A cover flow layout is there
  too (Interface → Home screen).
- Switching to it shows the Wii games and the Wii saves; GameCube games are
  one press away in Sort & filter. Switching back to Porpoise shows every game
  and the GameCube cards.
- Buttons and prompts read in Title Case (in English).

### Wii Remote on the DualSense (beta)
- Wii games get a Wii controller: **Remote + Nunchuk** (the default),
  **Remote**, **Remote sideways**, **Classic Controller**, or **two
  controllers** (alpha: a second DualSense is the Nunchuk).
- **Point at the screen** with the gyro (hold R1 a moment to center), the touch
  pad or the right stick. The DualSense's motion is the Remote's (tilt, swing,
  point) and a quick flick shakes it. The pointer is smoothed against hand
  tremor without lagging on a sweep.
- **Wii Remote setup:** pick the controller, center, then point at two corners
  of your screen, so the pointer matches your TV and how far you sit. It can
  come up before each Wii game (Triangle skips it); Simple and Advanced, with
  four named presets.
- **Grip:** Auto reads how you hold the DualSense (flat in both hands, or stood
  on end in either hand), or choose one yourself.
- **How to hold it:** a page that draws the DualSense as each Wii controller,
  with what every button does. In a Wii game, the in-game menu can
  recalibrate the pointer, open the setup or switch presets.

### Memory Cards
- **Wii saves:** L2 / R2 switches between the GameCube cards and the Wii saves
  Dolphin keeps, each with its own banner, icon and name. Square backs one up,
  Triangle deletes it.

### Faster and lighter
- Banners and covers stop working in the background the moment a game starts,
  so the game has the console to itself.
- Animated tiles are drawn a frame at a time into textures streamed to the
  GPU, and covers and banners off screen give their memory back.

### Also
- **Sort & filter:** All / GameCube / Wii, and "Get covers and info now".
- Covers arrive for games added while a download is running.
- **Custom texture packs** load now (the core was told the console had no
  memory to spare for them).
- The updater shows "Finishing up" with a spinner while it installs.

## 1.1 — 2026-10-04 (build 15)

### Recommended settings (beta)
- Every game's settings open with **Recommended**: Dolphin's own fixes for the
  game and Porpoise's tested picks, each with an **on/off switch** — green is
  on — and a tag saying where it comes from. Picks are updated daily.
- Picks can change Dolphin's own per-game settings too. The first one is for
  **WWE Day of Reckoning 2**: EFB copies stay on the GPU and textures decode
  there, which should lift it well above the ~6 fps it ran at (still being
  tested).
- A game's Dolphin settings now **stay put when you change graphics in the
  in-game menu** (the Dolphin core used to drop them until the next launch).
- If you keep your own Dolphin settings file for a game, Recommended says so
  and leaves it alone.

### In-game menu
- Four tabs, switched with L1 / R1: **Game**, **Video**, **Graphics** and
  **Controls**, with nearly every setting there, applied at once and saved for
  that game.
- **Save states:** three slots per game, each with a picture. *Save state…* and
  *Load state…* open a slot picker: saving over a used slot needs a second
  press, loading shows empty slots dimmed. A spinner shows while a state is
  written, and *Fast save states* (on by default) leaves Dolphin's texture
  cache out of them, so saving takes a moment instead of many seconds.
  Pictures fit the game's real shape — 4:3 or 16:9 — without the black bars.
  Start a game from a slot in its Details page.
- **Setups:** save a game's Video and Graphics settings as one of four setups
  and use it on any other game, from the menu or from Settings.
- **Fast forward:** 2x or 4x, shown in a corner while it's on.
- The Controls tab draws the DualSense with a line out to every button and the
  GameCube button it plays, and opens Customize buttons over the paused game.

### Controls
- **PlayStation is the default layout** (Cross is A, Circle is B). The
  **GameCube** layout now matches Dolphin's: Circle is A, Cross is B (it was
  Cross A, Square B). Saved settings keep the layout they had.
- **Four layouts of your own** (*My layout 1–4*) beside GameCube and
  PlayStation. Any game can use any of them. 1.0's custom buttons become
  *My layout 1* (a game's own custom buttons move to a free layout).
- New controller art and **button icons everywhere**, from Zacksly's
  *PS5 Button Icons and Controls* (CC BY 3.0).
- The DualSense **light bar** shows each player's color.

### Picture
- **Nine screen filters:** Smooth, Sharp, Sharpen, CRT, Arcade CRT, VHS,
  **Soft VHS** (soft, faded color and glow, no glitches), **8-bit** and
  **Pocket** (a green handheld screen), with a strength setting.
- **Eight borders** for 4:3 games: **Porpoise** (the logo and name), Porpoise
  glass, Midnight, **Frost**, **Carbon**, and three cabinets — Arcade, **Retro**
  and **Synthwave** — plus your own PNGs in `/data/porpoise/borders/`. Borders
  now draw at full resolution.

### Library
- **Favorites** (Options): a gold star on the box, and a *Favorites first*
  sort.
- **Play time** for every game, and a *Most played* sort.

### Languages
- **Twelve languages**, each with its flag: English, Español (España),
  **Español (Latinoamérica)**, Français, **Deutsch**, **Italiano**,
  **Nederlands**, **Polski**, Português (Portugal), **Português (Brasil)**,
  **Русский** and **日本語**. *System* follows the PS5's language.
- Game descriptions in Spanish, French, Portuguese, Italian, German and Dutch
  where GameTDB has them.
- Translations are first drafts: corrections go in
  `/data/porpoise/lang/<code>.txt` (see README) and need no new build.

### Updates
- **Settings > About > Updates:** *Check now* looks on GitHub, and when a newer
  Porpoise is out the same row **installs it**: the download is checked against
  GitHub's SHA-256 and every file against the release's manifest before
  anything is replaced, then Porpoise closes so the new one opens next time.
  Games, saves and settings are never touched.
- An update notice in the library when a new Porpoise is out.

### Fixes and speed
- **Can't see /data, empty folder browser, games in /data/games not found:**
  when the console starts Porpoise inside the app sandbox, Porpoise now asks the
  HEN (etaHEN / OnionHEN) to free it, as PS5SX2 does, and says so if it can't.
  Settings, memory cards and states a sandboxed 1.0 kept in the app's own
  folder come along to `/data/porpoise` the first time.
- **A clearer folder browser:** it opens on the whole console at `/data`,
  lists the game files in each folder (and how many each folder holds), keeps
  drives and shortcuts on Triangle (only drives with something on them), shows
  folders the old one missed, and copies a game from a USB drive to the
  console with one press.
- Shaders compile on four background threads instead of one: less stutter the
  first time a game shows something new.
- "Last played 1 weeks ago" is gone.

### Also
- A **QR code** in About that opens the bug tracker on your phone.
- About credits **Mihawk (mihawk-99)**, who brought the Dolphin core to the PS5.
- Second discs of two-disc games keep their own history and states.

## 1.0 — 2026-10-03

The first public release of Porpoise.

### Controllers
- Up to four players, one per signed-in PS5 user. Controllers can join in the
  middle of a game.
- Three button layouts: GameCube (the default), PlayStation (Cross A, Circle B)
  and Custom.
- Customize buttons: a picture of the DualSense with the GameCube button on each
  control. Pick a button, press the one you want, and the two swap.

### Library
- A cover-flow shelf of thick glass box-art tiles in a perspective grid room,
  with gloss, chromatic edges and a slow moving reflection.
- Automatic game search in the usual folders and on USB and extended drives.
  You can also add folders with a built-in folder browser, searched four
  levels deep.
- Supports `.iso`, `.gcm`, `.rvz`, `.ciso`, `.gcz`, `.wbfs` and `.wia`.
- Sort by title or by recently played.

### Box art and details
- Box art from GameTDB: 5:7 front covers, back covers and disc label art, which
  appear as they download.
- Game details from GameTDB: description, developer, publisher, release date,
  genre, players and rating.
- Square flies the box into Details, Triangle shows the back of the box, the
  right stick turns it, and the disc spins alongside.
- L2 / R2 swipe to the previous or next game inside Details.

### Memory cards
- Slot A and Slot B with save icons, banners, block counts and dates.
- Copy and delete saves, each with a confirmation.
- *Save data* on a game's Details page jumps to that game's save.

### Settings
- A rail of sections: Video, Graphics, Audio, Controls, System, Games,
  Interface and About.
- Many Dolphin options: internal resolution (1080p default), widescreen hack,
  aspect ratio, anisotropic filtering, anti-aliasing, texture filtering, output
  resampling, ubershaders, texture cache accuracy, per-pixel lighting, fog,
  overscan, custom textures, CPU clock, dual core, fast disc loading, cheats,
  console language and progressive scan.
- Per-game settings, with changed values highlighted and a reset to default.
- Reset all settings.

### In-game menu
- Options + touch pad pauses the game and slides in a menu with Resume, quick
  video and volume settings, Quit to library and Close Porpoise.

### Music, sound and languages
- Original menu music and sound effects by @elripalda, with separate switches
  and volumes.
- English, Spanish, French and Portuguese. Porpoise follows the PS5's language,
  with an override in Settings and optional translation fix files. More
  languages are coming in a future update.

### Under the hood
- Dolphin's JIT on the PS5's CPU, with Vulkan through Mesa's RADV.
- Frames locked to the TV's vblank when the display allows it, for even
  motion; clean 48 kHz sound; never above 60 fps.
- Asynchronous ubershaders by default, so games don't stall on new shaders.
- Textures are freed only after the GPU is done with them, and large images are
  scaled down to save memory.
