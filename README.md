<p align="center">
  <img src="docs/images/banner.png" alt="Porpoise - Dolphin Emulator for PS5 " width="100%">
</p>

<p align="center">
  <a href="https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/elripalda/Porpoise-Dolphin-Emulator-for-PS5?label=release&color=3b8cff"></a>
  <img alt="Platform: PS5 homebrew" src="https://img.shields.io/badge/platform-PS5%20homebrew-1f5fd6">
  <img alt="Powered by Dolphin" src="https://img.shields.io/badge/emulation-Dolphin-5cd3ff">
  <a href="LICENSE"><img alt="License: GPL-3.0-or-later" src="https://img.shields.io/badge/license-GPL--3.0--or--later-a9a6ff"></a>
</p>

<p align="center">
  <a href="https://recompstore.com/game/PPSA99764"><img alt="Get it on RecompStore" src="https://recompstore.com/assets/press/badge-dark.svg" height="56"></a>
</p>

<p align="center">
  <b>Porpoise</b> is a GameCube and Wii emulator for jailbroken PS5, built on
  <a href="https://dolphin-emu.org">Dolphin</a>, with its own launcher for the TV and the DualSense.
</p>

<p align="center">
  <a href="https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5/releases/latest"><b>Download Porpoise 4.0</b></a> ·
  <a href="#install">Install</a> ·
  <a href="#controls">Controls</a> ·
  <a href="BUILDING.md">Build from source</a> ·
  <a href="CREDITS.md">Credits</a>
</p>

---

## What it is

Porpoise is a PS5 homebrew app (title ID `PPSA99764`) that runs the Dolphin
emulator's libretro core inside its own launcher. It has a game library with
box art, a memory-card manager, per-game settings and an in-game menu, all
driven with a DualSense. Dolphin's x86-64 JIT runs on the console's CPU, and
graphics go through Vulkan on Mesa's RADV driver.

It plays your own GameCube and Wii disc backups. It doesn't include any games,
BIOS or firmware files.

### New in 4.0
- **Frame Generation (beta):** a 30 fps game shown at 60, or a 60 fps game at
  120 with the PS5 at 120 Hz. **Steady 30 FPS** for games that swing between
  30 and 60.
- **Simpler Settings** with **Show All Settings** for everything, and **Auto
  Boost**: a game's best PS5 settings in one press.
- A calmer **Porpoise** theme, and **Star Cube** redone after the GameCube's
  menu.
- **Change Disc** in the in-game menu, **Player 1's Port**, and the **Smooth
  Edges (FXAA)** filter.
- Smoother quick saves, games with big shader caches that start again, and a
  long list of fixes. The full list is in the [changelog](CHANGELOG.md).

## Features

### The library
- Games are shown as a row of box-art tiles. Left and right move between them.
- Porpoise looks for games in the usual folders and on USB and extended drives
  (see [Adding games](#adding-games)). You can also add your own folders; each
  is searched four levels deep.
- Formats: `.iso`, `.gcm`, `.rvz`, `.ciso`, `.gcz`, `.wbfs` and `.wia`, plus
  WiiWare, Virtual Console and channels as `.wad`, and homebrew apps (see
  [Channels, WiiWare and homebrew](#channels-wiiware-and-homebrew)).
- **Filter** the library to everything, GameCube, Wii or Channels.
- Sort by title, recently played, most played or favorites first. Each game
  shows its play time and when you last played it.

### Home screen tiles
- **A game's own tile on the PS5 home screen:** in a game's settings, **Home
  screen → Add to home screen** makes a small app in `/data/homebrew`. Install
  it with ShadowMountPlus, close Porpoise, and the tile opens Porpoise straight
  into that game. **Close Porpoise after the game** decides where leaving the
  game takes you. The tile hands the launch to the jailbreak's payload loader
  on port 9021 (etaHEN has one), since a PS5 app can't open another app itself.
- **Tile art** shows the icon and the background as the home screen will.
  Pick the cover, the back of the box, a screenshot or the title screen (from
  [libretro's thumbnails](https://github.com/libretro-thumbnails)) or your own
  `.png` / `.jpg` (from `/data/porpoise/home-art` or a USB drive). Fill the
  space and move and zoom it with the left stick and R2 / L2, or show it whole
  over a blur, white, black or a Porpoise pattern.

### Revolution theme
- **Settings → Interface → Theme** switches between **Porpoise** (the cover
  flow) and **Revolution**: a bright home screen of game tiles, twelve to a
  page, with a segmented clock and the date along the bottom and round buttons
  for Settings and the memory cards. Arrows at the sides (or L2 / R2) turn the
  page. *Home screen* can switch it to a cover flow instead.
- **Point with the controller:** a hand pointer that follows the DualSense,
  opens over empty space, points over anything you can choose and leans as you
  twist. The D-pad works too, the touch pad turns pointing on and off, and R3
  centers it.
- **Wii games play their own tile and banner**, animated, read from the disc
  (any of the formats above), with the banner's jingle when a tile opens.
  GameCube games show their covers.
- **Cross** on a tile grows it to fill the screen and opens it, with **Wii
  Controls** (or **Game Settings**) and **Start**, plus save states, save data
  and favorite. **Square** on a tile plays it straight away.
- Memory Cards, Settings, dialogs and the in-game menu all go white with it.
  Switching to Revolution shows your Wii games and Wii saves; switching back
  shows every game and the GameCube cards.

### Wii Remote on the DualSense (beta)
- Each Wii game gets a Wii controller: **Remote + Nunchuk** (the default),
  **Remote**, **Remote sideways**, **Classic Controller**, **two
  controllers** (alpha: a second DualSense is the Nunchuk), or the **GameCube
  controller**, for Wii games that take one, such as Super Smash Bros. Brawl
  and Mario Kart Wii. The GameCube controller uses your GameCube button layout.
- The Remote's pointer comes from the gyro (hold R1 a moment to center it),
  the touch pad or the right stick. The DualSense's motion is the Remote's,
  and a quick flick shakes it.
- **Wii Remote setup:** pick the controller, center, then point at two corners
  of your screen, so the pointer matches your TV and how far away you sit. It
  can come up before each Wii game (Triangle skips it), has Simple and Advanced
  modes and keeps up to four presets.
- **Grip:** Auto reads how you're holding the DualSense, or choose one.
  **How to hold it** (Settings → Wii Remote) draws the DualSense as each Wii
  controller with every button labeled.
- **Your own buttons for each Wii controller:** Settings → Controls →
  Customize buttons, then L1 / R1 to pick Remote + Nunchuk, Remote, Sideways
  or Classic.

### RetroAchievements (softcore)
- **Your lists:** the in-game menu's **Achievements** tab shows every
  achievement for the game you're playing, unlocked or not, with its badge,
  points and rarity. In the library, **Square** on a game's Details shows its
  list as of your last play.
- Earn [RetroAchievements](https://retroachievements.org) as you play. In the
  library, press **L1 + Square** (or go to Settings → Games →
  RetroAchievements) and sign in with your RetroAchievements account; the
  panel has its own on-screen keyboard. Porpoise keeps a sign-in token, never
  your password, in `/data/porpoise/retroachievements.ini`. Until you sign in,
  nothing connects.
- **Unlocks pop up like PS5 trophies:** the badge, the title and the trophy
  sound, in the Trophies notifications (your trophy notification settings
  apply). Completing a game plays the platinum sound, and each game starts
  with a popup of how many you've unlocked.
- The popups go through your jailbreak's **ELF loader** (port 9021, as etaHEN
  and the ELF loader payloads have); without one, Porpoise draws them over the
  game itself.
- Softcore only: hardcore mode isn't available. Your game has to be a version
  RetroAchievements knows; Porpoise says so when it isn't.

### Box art and game info
- Front covers, back covers and disc art download from
  [GameTDB](https://www.gametdb.com) while the console is online.
- Game details from GameTDB: description, developer, publisher, release date,
  genre, number of players and rating.
- To use your own art, put a PNG named after the game's disc ID in the covers
  folder.
- Covers and info can each be turned off under **Settings → Games**.

### The details page
- **Square** opens a game's details. **Triangle** shows the back of the box,
  the **right stick** turns it, and **L2 / R2** move to the previous or next
  game.
- From here: **Play**, **Save states**, **Game settings** and **Save data**.
- **Save states:** three slots per game, each with a picture. You can start a
  game from any of them.
- **Options** marks a game as a favorite, here or in the library.

### Memory cards
- Slot A and Slot B side by side, with each save's icon, banner, title, block
  count and date, and each slot's free blocks.
- **Square** copies a save to the other slot and **Triangle** deletes it. Both
  ask first.
- **Save data** on a game's details page goes straight to that game's save.
- **Wii saves:** L2 / R2 switches to the Wii saves Dolphin keeps, each with its
  banner, icon and name. **Square** backs one up and **Triangle** deletes it.

### Controllers and players
- Up to four players. Every controller signed in to a PS5 user is a player,
  and controllers can join in the middle of a game.
- **Button layouts:** *PlayStation* (the default: Cross is A, Circle is B),
  *GameCube* (Circle is A, Cross is B, as in Dolphin) and four layouts of your
  own (*My layout 1–4*).
- **Customize buttons** shows the DualSense with the GameCube button each
  control plays. Pick a GameCube button, press the DualSense button you want,
  and the two swap. It also works from the in-game menu.
- Any game can use any layout. Analog L and R follow the triggers.
- The light bar shows each player's color: blue, red, green and pink to begin
  with, or any of ten colors (**Settings → Controls → Light bar**).
- **Turbo button:** pick one button, and while you hold it, it presses again
  and again by itself (about fifteen times a second), for games that make you
  mash.
- **Quick save buttons:** pick a slot, and **touch pad + L1** saves to it and
  **touch pad + L2** loads it, without opening the menu. Off until you pick one.
- **Trigger click (beta):** the DualSense's adaptive triggers click near the
  bottom of L2 and R2, as the GameCube's L and R do. Off, Light, Firm (the
  default) or Extra Firm.

### Settings
Settings are split into sections. At first they show only what most players
need; **Show All Settings**, the first row, brings back every option.

| Section | What's inside |
|---|---|
| **Video** | **Output resolution** (*Match the PS5* by default, or 1080p, 1440p, 4K), **Frame Generation** (beta) and **Steady 30 FPS**, **Widescreen** (see [Widescreen](#widescreen)), aspect ratio, **border**, FPS overlay, V-Sync |
| **Enhancements** | Internal resolution (1080p by default), anti-aliasing (MSAA/SSAA), anisotropic filtering, the **screen filter** and its strength, **Extra Bloom**; under **Advanced Options**, saturation, contrast and warmth, texture filtering, output resampling, per-pixel lighting, disable fog, custom texture packs and, per game, **graphics mods** (see [Graphics mods](#graphics-mods)) |
| **Graphics** | Shader compilation (asynchronous ubershaders by default), build shaders before starting, threaded GPU recording (beta), texture cache accuracy, crop overscan, skip duplicate frames, quick resume (beta), fast save states |
| **Audio** | Game volume and mute; **menu music** and **menu sounds**, each with its own switch and volume, and the **sound set** (Crisp, Soft or Porpoise's original); the **sound preset**, **audio buffer** and **fill audio gaps** (see [Sound](#sound)); and, in beta, **accurate audio**, the **Wii Remote speaker** (TV or controller) and the **microphone** |
| **Controls** | Button layout (PlayStation, GameCube or one of your four), Customize buttons, **Player 1's Port**, vibration, **fast forward buttons**, **quick save buttons**, **turbo button**, **trigger click** (beta), each player's **light bar** color, connected controllers; per game, **Console** (Auto, GameCube or Wii) for a game Porpoise detected wrong |
| **Wii Remote** | Wii Remote setup, presets, setup before each Wii game, How to hold it, Wii controller (including the **GameCube controller**, for Wii games that take one), pointer, pointer speed, grip, motion, flick to shake |
| **System** | Emulated CPU clock (50–300%), **Riptide Boost** (beta), dual core, emulator on its own cores, exact multiply-add, fast disc loading, cheats, console language, progressive scan; and, in beta, **Wii widescreen**, **PAL games at 60 Hz**, **sensor bar** position, **start Wii discs in the Wii Menu**, the **GameCube boot animation** (see [Your own BIOS and Wii Menu](#your-own-bios-and-wii-menu-beta)) and **WiiConnect24 channels** (through WiiLink) |
| **Games** | Find games automatically, add or remove game folders, search again, download covers, download game info, **move Porpoise's folder** to extended storage or a USB drive, **RetroAchievements** |
| **Interface** | Menu language, **Theme** (nineteen, among them Porpoise, Star Cube, Revolution, 8-Bit, Beige, Translucent and Arcade), **Colors**, **Font**, library views (Cover Flow, Wheel, Disc Flow, Shelf, Box, List, Stack, Helix, Spines, Spotlight) and Memory Cards views, **Recently Played**, Revolution's home screen and pointer, **reset all settings**, **Reinitialize Porpoise** |
| **Screenshots** | The **gallery** of every game's screenshots, the **screenshot buttons** (touch pad + Square) and where they're kept (`/data/porpoise/screenshots`) |
| **Accessibility** | **Text size** (Smaller to Larger), color filters, high contrast, **Bolder Focus**, reduced motion, still background, larger button hints |
| **About** | Version, **Updates** (check GitHub and install a new Porpoise), the **Discord** for help and bug reports (with a QR code), **Report a bug**, and credits |

- **Per-game settings.** Any game can override the Video, Graphics, Audio, Controls
  and System settings. Changed values show in blue, and *This game → Reset to
  default* clears them.
- **Auto Boost.** Supported games come with the settings that run them best
  on PS5, as tested, switched on with one press at the top of the game's
  settings: **Dolphin's own fixes** for it, from Dolphin's per-game database,
  and **Porpoise's picks**. Each has its own switch and a tag saying where it
  comes from; changes apply the next time the game starts. They come from
  [`data/recommended.ini`](data/recommended.ini) in this repository and update
  by themselves once a day; suggestions for more games are welcome.
- **Setups.** Save a game's Video and Graphics settings as one of four setups
  (in-game menu → Graphics → *Save as a setup*) and use it on any other game,
  from the in-game menu or the game's settings.

### Screen filters and borders
- **Screen filters:** *Smooth*, *Sharp*, *Sharpen (CAS)*, *CRT* (scanlines and an
  aperture grille), *Arcade CRT* (a curved tube with rounded corners), *VHS*
  (tracking wobble, color bleed and tape noise), *Soft VHS* (soft, faded
  color and a gentle glow, no glitches), *8-Bit* and *16-Bit* (a small palette,
  big pixels and dithering), *Pocket* (a four-green handheld screen),
  *Scanlines*, *Shadow Mask*, *LCD*, *FSR 1* (AMD's upscaler), *NTSC Composite*,
  *Aperture Grille* and *Smooth Edges* (FXAA),
  each with a strength. Enhancements adds Extra Bloom and saturation, contrast
  and warmth.
- **Borders** fill the bars beside a 4:3 picture when widescreen is off:
  *Porpoise* (the logo and name), *Porpoise Glass*, *Midnight*, *Frost*,
  *Carbon*, three cabinets with a curved opening — *Arcade Cabinet*,
  *Retro Cabinet* (wood and 70s stripes) and *Synthwave Cabinet* (a striped
  sun and a neon grid) — TVs and a monitor from different eras (*80s TV*,
  *Wood-Grain TV*, *90s TV*, *Black TV*, *Beige Monitor*), and *Revolution*
  and *Star Cube* to match those themes.
  **Make your own:** a 1920×1080 PNG, transparent where the picture shows (the
  4:3 picture fills x 240–1680), in `/data/porpoise/borders/`. It appears in
  the list by its file name.
- **Reset all settings** puts Porpoise back the way it ships. Your games,
  folders and saves are kept.

### In-game menu
Press **Options + touch pad** together while playing. The game pauses and the
menu opens, with six tabs (**L1 / R1**), plus **Achievements** when the
game has a RetroAchievements set:

- **Game:** Resume, **Save state…** and **Load state…** (three slots, with
  pictures: pick a slot with left and right; saving over a used slot asks for a
  second press, and a spinner shows while the state is written), **Take
  Screenshot**, **Fast forward** (2x or 4x), **Change Disc** (a game's other
  disc, or an expansion's), Start Over, Quit to library and Close Porpoise.
- **Video:** resolution, Frame Generation and Steady 30 FPS, widescreen, aspect ratio, anti-aliasing, anisotropic
  filtering, screen filter and strength, border, FPS counter.
- **Graphics:** shader compilation, texture cache, texture filtering, output
  resampling, per-pixel lighting, fog, overscan, duplicate frames, the game's
  **graphics mods** when it has any, and **Save as a setup / Use a setup**.
- **Audio:** volume, mute, the **sound preset**, audio buffer, fill audio
  gaps, accurate audio, and the Wii Remote speaker in Wii games.
- **Controls:** button layout, **Customize buttons** over the paused game,
  Player 1's Port, vibration, turbo, quick save buttons, trigger click, and the DualSense drawn
  with a line to every button saying which GameCube button it is. In a Wii
  game: the Wii controller and its pointer, grip and motion.
- **Patches:** how this game plays in widescreen right now (its 16:9 code,
  its own option, the emulated hack, or 4:3), and every cheat and patch it has,
  with the ones that are on at the top, each with its own switch. Code changes
  take effect the next time the game starts.

Changes apply immediately and are saved for that game.

**Fast forward without the menu:** with **Settings → Controls → Fast forward
buttons** on (the default), **touch pad + R1** steps fast forward (off, 2x,
4x) and **touch pad + R2** fast-forwards while you hold it. A quick press of
the touch pad alone still reaches the game.

When you resume, the game ignores your buttons until you let go, so the press
that closed the menu never reaches the game.

### Music and sound
- Original menu music and sound effects by Ruben. The music fades out when a
  game starts and comes back in the library.
- Music and effects each have their own switch and volume.

### Sound
Settings → Audio, for every game or one (and the in-game menu's **Audio** tab):
- **Sound preset:** *Smooth* (the default) pulls the game's sound from
  Dolphin's own mixer, as Dolphin does on a PC: when a game runs slow, the
  mixer covers the gap with the sound it just played, faded, instead of a
  crackle. *Responsive* keeps less sound ready, for a little less delay;
  *Extra smooth* keeps more, for games that slow down often; *Classic (2.1)*
  is the sound of Porpoise 2.1. A change to or from Classic applies the next
  time a game starts.
- **Audio buffer:** how much sound is kept ready: 40, 80 or 160 ms (*Low*,
  *Normal* or *Safe* with Classic). More holds off crackling when a game slows
  down, for a little delay.
- **Fill audio gaps:** on by default; off leaves the gaps silent. With
  Classic, **Audio stretching** takes its place: the sound slows with the game,
  slightly lower, instead of crackling.
- **Accurate audio (beta):** Dolphin's exact sound chip (LLE) instead of its fast one.
  It fixes missing or wrong sound in a few games but needs much more of the
  processor, so turn it on for the game that needs it.
- **Wii Remote speaker (beta):** the sounds Wii games play from the Remote's own
  speaker. *TV* mixes them into the game's sound; *Controller* plays them from
  each player's DualSense, as on a Wii (if a controller's speaker can't be
  opened, they stay on the TV).
- **Microphone (beta):** the DualSense's microphone becomes the GameCube Microphone
  (Mario Party 6 and 7: hold **R3** to talk) and the Wii Speak.

### Languages
- **Seventeen languages**, each with its flag in the language list (Chinese
  with its script's character instead): English,
  Español (España), **Español (Latinoamérica)**, Français, **Deutsch**,
  Italiano, **Nederlands**, **Polski**, Português (Portugal), **Português
  (Brasil)**, **Svenska**, **Türkçe**, **Русский**, **日本語**, **한국어**, **简体中文** and
  **繁體中文**. Porpoise follows your PS5's system
  language, and **Settings → Interface → Language** overrides it.
- Game descriptions from GameTDB come in Spanish, French, Portuguese, Italian,
  German and Dutch too, where GameTDB has them (English otherwise).
- The translations are first drafts, and fixes are welcome. To fix a line
  without waiting for an update, create `/data/porpoise/lang/<code>.txt` —
  `es`, `es-419`, `fr`, `pt`, `pt-BR`, `it`, `de`, `nl`, `pl`, `ru`, `sv`, `tr`,
  `ja`, `ko`, `zh-Hans` or `zh-Hant` —
  with lines such as `Quit to library = Volver a la biblioteca`. In the source,
  translations live in [`i18n/`](i18n).

### Performance
- When the TV's refresh rate allows it, each frame is shown on its own vblank;
  otherwise Porpoise keeps its own clock. Games run at their normal speed,
  with 48 kHz sound.
- Dolphin compiles shaders on four background threads with asynchronous
  ubershaders, so games stutter less the first time they draw something new.
  The shader cache is kept between runs.
- Dolphin's x86-64 JIT with fastmem, and Vulkan through RADV.
- Some games are slow on PS5 because of Dolphin's own per-game fixes; see
  **Auto Boost** in a game's settings and the speed line in `core.log`.

## Requirements

- A PS5 able to run homebrew, with **etaHEN** and **kstuff** loaded and
  **ShadowMountPlus** (or your usual method) to register homebrew titles. Use
  **ShadowMountPlus 1.7 beta 4 or newer**: with older versions Porpoise can
  close as it opens on some setups (see [Troubleshooting](#troubleshooting)).
- A way to copy files to the console: FTP, or a tool such as PS5 Upload.

### Jailbreaks on firmware 13.60 (Relapse)

What players have reported so far. Porpoise needs a jailbreak daemon to free
it from the app sandbox so it can see `/data` and USB drives; without one it
still runs games from its own folder (`/app0/porpoise/games`).

| Jailbreak | Porpoise | What to do |
| --- | --- | --- |
| etaHEN | Works | If Porpoise says it can't reach `/data`, turn on **Legacy Command Server** in etaHEN's Toolbox settings. |
| OnionHEN | Works after one change | Add `PPSA99764` to `exact_title_ids` in `/data/OnionHEN/config.ini`, then reload OnionHEN. |
| Lapy | Closes Porpoise as it frees it | Since 2.7 Porpoise notices and starts in the sandbox the next time. Turn on **Settings → Games → Stay in the sandbox** to always start that way. |

Tried another one? Tell us how it went on the Discord.
- **Your own games**, as backups you made from discs you own. Porpoise
  includes no games and no system files of any kind.
- Optional: an internet connection on the console, for box art and game info.

## Install

1. Download the Porpoise zip (**`Porpoise-<version>.zip`**) from the
   [latest release](https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5/releases/latest) and
   unzip it on your computer. You get a folder named **`PPSA99764`**.
2. Connect to your PS5 with FTP (for example etaHEN's FTP server and a client
   like FileZilla) or with PS5 Upload. With FTP, set binary transfers
   (FileZilla: **Transfer → Transfer type → Binary**): in text mode some of
   Porpoise's `.txt` files fail to copy.
3. Copy the whole **`PPSA99764`** folder into **`/data/homebrew/`**, so you end
   up with `/data/homebrew/PPSA99764/`.
4. Register it the same way as your other homebrew (for example with
   ShadowMountPlus, **1.7 beta 4 or newer**). The Porpoise tile appears on the
   home screen.
5. Copy your game backups into **`/data/porpoise/games/`** (Porpoise creates it
   on first launch), into any folder listed under [Adding games](#adding-games),
   or onto a USB drive, then open Porpoise.

Box art and game details download automatically while the console is online.

### Updating
**From 1.1 on, Porpoise updates itself:** **Settings → About → Updates**.
*Check now* looks on GitHub (Porpoise also looks once a day while it's online
for covers), and when a newer Porpoise is out the same row installs it. The
download is checked against GitHub's SHA-256 and every file against the
release's manifest before anything is replaced; if anything goes wrong,
nothing is changed. Porpoise then closes; open it again from the home screen.
Your games, saves, covers and settings live in `/data/porpoise/` and are never
touched.

**By hand** (and from 1.0, which can't update itself): delete
`/data/homebrew/PPSA99764/`, then upload the new folder — ShadowMountPlus keeps
serving the old copy until it is removed. If your 1.0 couldn't reach `/data`
(your games were in the app's own `porpoise/games` folder), copy the new files
over the old folder instead of deleting it: 1.1 moves your settings, memory
cards and states to `/data/porpoise` itself and still finds games there. Check
**Settings → About** to confirm the version.

## Adding games

With **Find games automatically** on (the default), Porpoise looks in:

- `/data/porpoise/games`
- `/data/games`, `/data/GameCube`, `/data/Wii`, `/data/roms`, `/data/iso`
- every USB drive (`/mnt/usb0`–`/mnt/usb7`) and extended storage
  (`/mnt/ext0`, `/mnt/ext1`)

Each of these is searched four folders deep, so games sorted into subfolders
are found too.

To use any other folder, go to **Settings → Games → Add a game folder**, browse
to it, and press **Square**. The browser shows the game files in each folder,
so you can see where they are. Porpoise searches that folder and four levels
below it. After copying new games, use **Search for games now**.

### Channels, WiiWare and homebrew
Porpoise ships no Nintendo files of any kind: everything here comes from your
own console.

- **WiiWare, Virtual Console and channels:** put the `.wad` files you made
  from your own Wii in any games folder. They appear in the library with
  their names and banners, marked *WiiWare*, *Virtual Console* or *Channel*
  (the **Channels** filter shows only them). Start one like any game; its saves
  stay in Dolphin's Wii storage between runs.
- **Homebrew apps:** keep each app in its own folder, as the Homebrew Channel
  does: `boot.dol` (or `boot.elf`) next to its `meta.xml` and `icon.png`, for
  example `/data/porpoise/games/apps/MyApp/`. The app shows with its name and
  icon. A `.dol` or `.elf` without a `meta.xml` beside it isn't listed.
- **A game shows as GameCube but is a Wii game** (or the other way round)?
  Open its settings: **Controls → Console** sets it to GameCube or Wii.

### Your own BIOS and Wii Menu (beta)
- **GameCube boot animation:** copy the BIOS (`IPL.bin`) you dumped from your
  own GameCube to `/data/porpoise/bios/`, under any name (Porpoise reads which
  one it is, NTSC or PAL; Settings → About shows what it found), then turn on
  **Settings → System → GameCube boot animation**. GameCube games then start with the console's own start-up. If
  there's no `IPL.bin` for a game's region, the game starts as usual.
- **Wii Menu:** put the Wii Menu `.wad` from your own Wii in a games folder and
  start it once from the library; it stays installed. With **Settings → System
  → Start Wii discs in the Wii Menu** on, Wii discs then start from the Wii
  Menu, as on a Wii.

### Playing from a USB or external drive

1. **Format the drive as exFAT** on your computer. FAT32 also works but can't
   hold files over 4 GB (most Wii `.iso` files are bigger); the PS5 doesn't
   read NTFS.
2. **Copy your games onto it**, anywhere up to four folders deep, for example
   `games/Wii/Super Mario Galaxy.rvz` or `GameCube/Melee.iso`. `.rvz` files
   are much smaller than `.iso` and load just as well.
3. **Plug it into the PS5** and make sure a jailbreak daemon is running (see
   [Troubleshooting](#troubleshooting)). Porpoise has to be out of the app
   sandbox to see any drive: if it says it can't reach `/data`, it can't see
   your drive either.
4. **Open Porpoise.** The games appear in the library. If you plugged the
   drive in after opening Porpoise, use **Settings → Games → Search for games
   now**.
5. Games deeper than four folders? Add their folder with **Add a game folder**
   (Triangle in the browser jumps to your drives).

You can also copy a game from the drive to the console: in **Add a game
folder**, browse to it on the drive and press **Cross** on the game.

**PS5 extended storage** (a drive the PS5 formatted for its own games) shows
up as `/mnt/ext0` (a second one as `/mnt/ext1`). A computer can't read that
format, so copy games to it over FTP instead (for example into
`/mnt/ext0/games/`).

**Porpoise's own folder can live there too:** Settings → Games → *Move
Porpoise's folder to extended storage* (or a USB drive) moves your settings,
saves, save states, covers and texture packs, then Porpoise closes; open it
again. If that drive isn't connected at the next start, Porpoise waits a
moment for it (and finds a USB drive in another port), then says so and uses
the console's storage until it is. A drive that already has a Porpoise folder
is offered as it is: *Use it* switches to that folder, and the one in use stays
untouched. After a reinstall or on a fresh console storage, Porpoise finds its
folder on extended storage or a USB drive by itself and asks whether to use it.

### Widescreen

Settings → Video → **Widescreen** is **Auto** by default:

- **Games with a widescreen code** (over 300 GameCube games, from Warped
  Polygon's collection, plus Dolphin's own) play in true 16:9: the game itself
  draws the wider view, with nothing popping in at the edges.
- **Games with a 16:9 option of their own:** turn it on in the game's options
  and the picture follows. Wii games follow the Wii's widescreen setting.
- **Other games stay 4:3.** **On** uses Dolphin's emulated widescreen hack for
  them, which can glitch: things at the screen edges may pop in and out.
- A game's Details say which it is, and so does the in-game menu: the
  **Patches** tab, and next to Widescreen in the Video tab. Every game's
  settings have a **Cheats and patches** section listing its codes, to turn
  off one by one (or saying it has none).

### Graphics mods

Dolphin's built-in graphics mods are in the Graphics settings of the games
that have them (about fifty), and in the in-game menu's Graphics tab:

- **Bloom** (the glow around lights) and **Depth of field** (the blur on
  faraway things), which can look blocky at high resolutions: *Off* removes
  them, *Blurred* softens them, *Native resolution* draws them as the console
  did. Among others: Mario Kart Wii, Super Mario Galaxy 1 and 2, Twilight
  Princess, Metroid Prime 3 and Trilogy, Xenoblade Chronicles, Okami, Donkey
  Kong Country Returns, Pandora's Tower and The Last Story.
- **Hide the HUD**, for clean screenshots, in the games Dolphin has it for.
- **Native resolution goop** in Super Mario Sunshine.

They're off until you choose them, and take effect the next time the game
starts.

### Your own cheats

Put a game's codes in **`/data/porpoise/cheats/<game ID>.ini`** (the 6-letter
ID, such as `GYQE01.ini`; 4 or 3 letters for all regions), in Dolphin's
format: `[ActionReplay]` or `[Gecko]` sections, as Dolphin writes them. They
turn on by themselves when the game starts, and each appears in the game's
settings under **Cheats and Patches** (and the in-game **Patches** tab) with
its own switch. Make the `cheats` folder yourself the first time (Porpoise
doesn't make it on its own). A Wii game's online patch codes (Wiimmfi, or one
for a custom server) go here too. **Settings → System → WiiConnect24
Channels** is only for the WiiConnect24 channels; it doesn't look for or need
any code.

## Controls

### Launcher

| Button | Library | Details | Memory cards |
|---|---|---|---|
| D-pad / left stick | Browse games | Choose an action | Move between saves and slots |
| **Cross** | Play | Confirm | – |
| **Circle** | – | Back to the library | Back |
| **Square** | Open Details | – | Copy to the other slot |
| **Triangle** | Sort | Front / back of the box | Delete |
| **Options** | Favorite | Favorite | – |
| **L2 / R2** | – | Previous / next game | – |
| **Right stick** | – | Turn the box | – |
| **L1 / R1** | Switch tabs: Library, Memory Cards, Settings | | |
| **L1 + Square** | RetroAchievements: sign in or out | | |

### In a game

| Button | Action |
|---|---|
| **Options + touch pad** | Pause and open the in-game menu |
| **Touch pad + R1** | Fast forward: off, 2x, 4x |
| **Touch pad + R2** (hold) | Fast forward while held |
| **Touch pad + L1** | Save to the quick slot (with Quick save buttons on) |
| **Touch pad + L2** | Load the quick slot (with Quick save buttons on) |
| **R3** (hold) | Talk into the GameCube Microphone (with Microphone on) |
| **Circle** (in the menu) | Resume |
| **L1 / R1** (in the menu) | Switch tabs: Game, Video, Graphics, Audio, Controls, Patches |

With the default **PlayStation** layout:

| GameCube | DualSense |
|---|---|
| A / B / X / Y | Cross / Circle / Square / Triangle |
| Z | R1 |
| L / R (analog) | L2 / R2 |
| Start | Options |
| Control stick / C-stick | Left stick / right stick |
| D-pad | D-pad |

The **GameCube** layout puts A on Circle and B on Cross. To set any button
yourself, go to **Settings → Controls → Customize buttons** (or the in-game
menu's Controls tab), and give a game its layout in its own settings.

**More players:** turn on another controller and choose a PS5 user for it. It
becomes the next player, up to four, and can join in the middle of a game.

## Where things are kept

| Path | What |
|---|---|
| `/data/homebrew/PPSA99764/` | The app. Replace it to update. |
| `/data/porpoise/games/` | A good place for your game files |
| `/data/porpoise/covers/` | Box art, back covers and disc art (`<disc ID>.png`); replace any with your own |
| `/data/porpoise/saves/` | Dolphin's data, including the memory cards (`User/GC/<region>/Card A`, `Card B`) |
| `/data/porpoise/game-settings/` | Each game's own settings |
| `/data/porpoise/states/` | Save states, three per game, with their pictures |
| `/data/porpoise/borders/` | Your own borders (1920×1080 PNGs) |
| `/data/porpoise/info.tsv` | Downloaded game details (`info-es.tsv` and so on for other languages) |
| `/data/porpoise/recommended.ini` | Auto Boost's settings, as last downloaded |
| `/data/porpoise/setups/` | Your four setups (Video and Graphics settings to use on any game) |
| `/data/porpoise/banners/` | Wii discs' banners, read once from each disc |
| `/data/porpoise/saves/User/Wii/backups/` | Wii saves you backed up from Memory Cards |
| `/data/porpoise/latest-release.json` | What GitHub last said about the newest release |
| `/data/porpoise/lang/` | Your own translation fixes (optional) |
| `/data/porpoise/bios/` | Your own GameCube BIOS, under any name (optional) |
| `/data/porpoise/screenshots/` | Your screenshots, a folder for each game |
| `/data/porpoise/cheats/` | Your own cheat codes, one `<game ID>.ini` per game (optional) |
| `/data/porpoise/retroachievements.ini` | Your RetroAchievements sign-in token (not your password) |
| `/data/porpoise/achievements/` | Each game's achievement list as of your last play, and the badges |
| `/data/porpoise/location.txt` | Where Porpoise's folder is, after you moved it to another drive |
| `/data/porpoise/cache/` | The menus' lettering, made once at the first start |

## Troubleshooting

- **Adding a game folder:** *Settings → Games → Add a game folder* opens on
  the whole console (`/`), on `data`. Each folder shows how many games are in
  it, and the games themselves are listed under the folders, so you can see
  where they are. **Square** uses the folder you're in (add as many as you
  like), **Triangle** jumps to your drives and Porpoise's own games folder,
  and **Cross on a game** on a USB drive copies it to the console.
- **Still seeing the old version after updating by hand?** Delete
  `/data/homebrew/PPSA99764/` completely before uploading the new folder.
- **"Porpoise can't reach /data", only a few folders in the folder browser, or
  games in `/data/games` not showing?** The console started Porpoise inside the
  app sandbox, and a jailbreak daemon has to free it. Make sure one is running
  **before** you open Porpoise:
  - **etaHEN:** turn on **Legacy Command Server** in etaHEN's Toolbox
    settings. etaHEN's own app-jailbreak list is built in and can't be edited
    to add Porpoise, so this is the setting that lets etaHEN free it.
  - **OnionHEN:** add `PPSA99764` to `exact_title_ids` under `[app_jailbreak]`
    in `/data/OnionHEN/config.ini` (comma-separated), then restart the console
    or reload OnionHEN.
  - **A standalone daemon** such as **Lapy** (loaded from your Homebrew
    Launcher or `autoload.txt`). Porpoise speaks both the classic Lapy request
    and the newer owned-root one.

  Then open Porpoise. It now keeps asking the daemon for a few seconds as it
  starts, so a daemon that loads a moment late or needs a second try is handled
  on its own — but if a launch still lands in the sandbox, just open Porpoise
  once more. This commonly explains "it worked the first time but not after":
  the daemon wasn't up, or running, on the later launch. Until it's freed,
  Porpoise keeps its things in the app's own folder
  (`/data/homebrew/PPSA99764/porpoise/`).
- **Porpoise closes as your jailbreak frees it** (reported with Lapy on 13.60)?
  Porpoise notices, and the next time it opens it stays in the sandbox and
  asks whether to keep doing that (**Stay in the sandbox**) or try again. You
  can change it any time in **Settings → Games → Stay in the sandbox**.
- **Porpoise closes the moment it opens, every time, on firmware 12.x with the
  Lapy JB Daemon or LegacyJB** (and opens fine without the daemon)? Update
  **ShadowMountPlus to 1.7 beta 4 or newer**: that fixed it for a player whose
  console closed Porpoise on every launch.
- **Porpoise closes by itself just after it opens** (seen with etaHEN 2.6b's
  jailbreak list on firmware 13.60)? Once freed from the sandbox, Porpoise reads
  every game and banner it can find, and 2.0 reads damaged or unusual files far
  more carefully. If it still happens, take the game files out of the way a
  few at a time to find the one it trips on, and share `trace.txt` (below) on
  the Discord.
- **A game runs slowly?** Open its settings and look at **Auto Boost**: some
  of Dolphin's own fixes for a game cost speed on PS5, and Porpoise's picks
  turn them off where that's been tested. The speed line in `core.log` (below)
  shows how fast the game really runs.
- **No covers?** The console needs to be online when Porpoise opens, and
  **Settings → Games → Download covers** must be on. A few discs have no art on
  GameTDB. You can add your own.
- **Something went wrong?** These two files help when you report a problem:
  - `/data/homebrew/PPSA99764/trace.txt`
  - `/data/homebrew/PPSA99764/porpoise/core.log` (records the game's real speed
    every 10 seconds)

Share them in the bug reports on the **[RIPALDA Discord](https://discord.gg/GgDE5Vynyu)**
with your console firmware, your HEN and its version, the game's disc ID and
what happened. **Settings → About → Report a bug** gathers the logs for you,
and the code beside it opens the Discord on your phone.

## Help and community

- **Discord:** [discord.gg/GgDE5Vynyu](https://discord.gg/GgDE5Vynyu): help, bug
  reports, news and test builds.
- **Website:** [ripalda.dev](https://ripalda.dev): Ruben's projects and downloads.

## Known limitations

- Wii support is experimental. The Wii Remote on the DualSense is in beta:
  pointing works well, motion-heavy games are still being tuned, and two
  controllers as Remote and Nunchuk is alpha.
- Save states are tied to the Porpoise and Dolphin version that made them; a
  future update may not load older ones. Your memory card saves always carry
  over.
- Netplay isn't part of Porpoise yet.
- RetroAchievements is softcore only, and its popups need the jailbreak's ELF
  loader to show badges and play the trophy sound.
- Memory-card copy and delete work on saves Dolphin keeps as files (the normal
  `Card A` and `Card B` folders).

## Build from source

Porpoise builds on Linux or WSL with clang 18, Mesa's RADV driver for PS5 and
the PS5 payload SDK. See **[BUILDING.md](BUILDING.md)**.

## Credits

Porpoise was created by **Ruben ([@elripalda](https://ripalda.dev))**: the
design, the launcher, the logo, the menu music and the sound effects.
[ripalda.dev](https://ripalda.dev) · [Discord](https://discord.gg/GgDE5Vynyu)

It stands on the work of many people. The full list, with licenses, is in
**[CREDITS.md](CREDITS.md)**:

- **[Dolphin](https://dolphin-emu.org)** by the Dolphin Emulator Project, the
  emulator itself, and the **[libretro Dolphin core](https://github.com/libretro/dolphin)**.
- **[libretro / RetroArch](https://www.libretro.com)**, whose API Porpoise uses to host Dolphin.
- **[Mihawk (mihawk-99)](https://github.com/mihawk-99)**, who brought the
  Dolphin core to the PS5. His **[PS5 RetroArch](https://github.com/mihawk-99/PS5_RetroArch)**,
  **[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)** and
  **[PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa)** are the PS5 Dolphin port,
  the platform layer and Vulkan on PS5 through **[Mesa](https://mesa3d.org)'s
  RADV** that Porpoise is built on. Porpoise wouldn't exist without them.
- **[ps5-framegen](https://github.com/Swordpdf/ps5-framegen)** by **swordpdf**
  (from PS5SX2 and RPCS3-PS5), with AMD FidelityFX FSR 3's optical flow:
  Frame Generation (beta).
- **[ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk)** by John Törnblom
  and contributors, and **BlackBearReloaded**'s native title pipeline.
- **[GameTDB](https://www.gametdb.com)** and its contributors, for the box art
  and game details.
- **[Zacksly](https://zacksly.itch.io)** ([@_Zacksly](https://twitter.com/_Zacksly)),
  for *PS5 Button Icons and Controls* (CC BY 3.0): the DualSense drawings and the
  button icons, adapted for Porpoise.
- The **[Wii Banner Player Project](https://github.com/jordan-woyak/wii-banner-player)**
  (zlib), for how a Wii disc's banner is read and played, and the
  **[Zstandard](https://github.com/facebook/zstd)** educational decoder, for
  reading `.rvz` discs.
- **[Nunito](https://github.com/googlefonts/nunito)** and Noto Sans
  **[JP](https://github.com/google/fonts/tree/main/ofl/notosansjp)**,
  **[SC](https://github.com/google/fonts/tree/main/ofl/notosanssc)**,
  **[TC](https://github.com/google/fonts/tree/main/ofl/notosanstc)** and
  **[KR](https://github.com/google/fonts/tree/main/ofl/notosanskr)**,
  **[JetBrains Mono](https://github.com/JetBrains/JetBrainsMono)**,
  **[VT323](https://github.com/google/fonts/tree/main/ofl/vt323)**,
  **[Doto](https://github.com/oliverlalan/Doto)**,
  **[Exo 2](https://github.com/googlefonts/Exo-2.0)**,
  **[Lora](https://github.com/cyrealtype/Lora-Cyrillic)** (as Porpoise Serif) and
  **[M PLUS 1](https://github.com/coz-m/MPLUS_FONTS)** (SIL OFL), and **[stb](https://github.com/nothings/stb)** by Sean Barrett.
- **Warped Polygon**, for the GameCube widescreen code collection Porpoise's
  widescreen uses.
- **Admentus64**, for the Enhancement Codes, whose 16:9 codes for Virtual Console N64 games Porpoise offers.
- **[RetroAchievements](https://retroachievements.org)** and its
  **[rcheevos](https://github.com/RetroAchievements/rcheevos)** library (MIT),
  which Dolphin's achievements are built on.
- The PS5 scene: **etaHEN**, **kstuff** and **ShadowMountPlus**, and the front
  ends **PS5SX2** and **ProsperoEden** for the inspiration. Porpoise asks the
  HEN to leave the app sandbox the way PS5SX2 does, and its RetroAchievements
  popups follow **PS5SX2**'s (Spyros, with Gabriel Fonseca's groundwork): the
  trophy-style notifications sent through the ELF loader.

## Legal

> [!IMPORTANT]
> **Porpoise does not condone piracy.** It contains no games and no console
> BIOS, IPL or firmware files, and none will ever be provided or linked to. The
> only keys it carries are the two Wii disc keys (standard and Korean) that
> Dolphin itself includes, used to read a Wii disc's or channel's own banner.
> Play only games you own, as backups you made from your own discs and
> consoles.

Porpoise is an independent, free and open-source project. It is **not
affiliated with, endorsed by or sponsored by Nintendo or Sony Interactive
Entertainment**, and it contains no code, artwork, sounds or other material from
either. *Nintendo*, *GameCube* and *Wii* are trademarks of Nintendo.
*PlayStation* and *PS5* are trademarks of Sony Interactive Entertainment Inc.
These names are used only to describe compatibility.

Emulation is provided by Dolphin (GPL-2.0-or-later). Box art and game details
are downloaded on your console from GameTDB and are not distributed with
Porpoise.

## License

Porpoise is free software, licensed under the
**[GNU General Public License v3.0 or later](LICENSE)**. Parts carried over
from other projects keep their own notices. Every component that ships in the
app, with its license and exact source revision, is listed in
`licenses/README.txt` inside the release and in [CREDITS.md](CREDITS.md).

<p align="center"><sub>Made with care by <a href="https://ripalda.dev">@elripalda</a> · <a href="https://discord.gg/GgDE5Vynyu">Discord</a></sub></p>
