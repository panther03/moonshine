# Moonshine Tutorial

A player-facing guide to **Moonshine V2.3.2 "Frame By Frame"**, a speedrun-practice mod for *Super Mario Sunshine*. It supports JP 1.0 (GMSJ01), US (GMSE01) and PAL (GMSP01).

The guide covers installation, the in-game menu, every default button bind, every practice code, and the larger features: savestates, frame advance, free camera, TAS projects, ILs, ghosts and layout editing.

> **The one thing to remember:** press **Y + Start** to open or close the Moonshine menu.

---

## Contents

1. [Installing](#1-installing)
2. [Launcher basics](#2-launcher-basics)
3. [Navigating the Moonshine menu](#3-navigating-the-moonshine-menu)
4. [Default button binds](#4-default-button-binds)
5. [Changing binds](#5-changing-binds)
6. [Savestates](#6-savestates)
7. [Practice pause and frame advance](#7-practice-pause-and-frame-advance)
8. [Free camera](#8-free-camera)
9. [TAS projects](#9-tas-projects)
10. [Warping and restarting](#10-warping-and-restarting)
11. [ILs, playlists, streaks and PBs](#11-ils-playlists-streaks-and-pbs)
12. [Ghosts](#12-ghosts)
13. [Timers, QFT and splits](#13-timers-qft-and-splits)
14. [Movement timing displays](#14-movement-timing-displays)
15. [Layouts, colours and HUD editing](#15-layouts-colours-and-hud-editing)
16. [Every code and setting, explained](#16-every-code-and-setting-explained)
17. [Your data folder](#17-your-data-folder)
18. [Dolphin notes](#18-dolphin-notes)
19. [Tips, gotchas and troubleshooting](#19-tips-gotchas-and-troubleshooting)

---

## 1. Installing

### Wii (Homebrew Channel)

1. Download the launcher ZIP:
   - **ENGLISH-MENUS** keeps every Moonshine menu in English, on all regions.
   - **JAPANESE-MENUS (日本語版)** gives Japanese launcher menus, and Japanese in-game Moonshine menus when you play **JP** Sunshine. US and PAL in-game menus stay English.
2. Copy the ZIP's `apps` folder to the root of your SD card. The launcher should end up at `/apps/moonshine_launcher/boot.dol`.
3. Keep `language.txt` next to `boot.dol` (and `ja_ui.bin` too for the Japanese download).
4. **Updating:** replace the app files. Your settings, records, ghosts, achievements and playlists stay. If you still have an old `apps/susamune_launcher`, rename it to `apps/moonshine_launcher` so the Homebrew Channel doesn't show two entries.

> SD savestates and TAS projects only work with the build that made them. After updating, make new ones.

### Dolphin

Apply the matching `.bps` patch to a **clean** ISO with a BPS patcher such as Floating IPS. See [Dolphin notes](#18-dolphin-notes) for the settings Dolphin needs.

| Region | Patch | Clean CRC32 |
|---|---|---|
| JP 1.0 (GMSJ01) | `moonshine_jp.bps` (`moonshine_jp_ja.bps` for Japanese menus) | `C3B17583` |
| US (GMSE01) | `moonshine_us.bps` | `771AD977` |
| PAL (GMSP01) | `moonshine_pal.bps` | `4C1D3641` |

> **Warning:** Don't run Gecko codes alongside Moonshine. Moonshine already includes nearly all the GCT-generator practice codes, and separate Gecko codes will break it or be broken by it. For example, the Level Select code breaks Moonshine's instant restart.

---

## 2. Launcher basics

Open **Moonshine Launcher** from the Homebrew Channel.

| Menu item | What it does |
|---|---|
| **Launch Game** | Boots the selected version with Moonshine injected. |
| **Version** | Chooses JP, US or PAL. It must match your disc or image. |
| **Path** | Picks your ISO/CISO on SD or USB, or **Disc Drive** for a real disc. Each version remembers its own path. |
| **Settings** | Nintendont options such as progressive scan, the retail PAL language, Auto Boot and Unlock Read Speed (on by default). |
| **Guide** | A built-in offline guide. Up/Down and A pick a topic, Left/Right turn pages, B goes back. |

- **Auto Boot** skips the menu and launches straight into the game. **Hold B** while the launcher starts to get back to the menu.
- **Themes:** put `background.png` (1024×480, up to 2 MiB) and `bgm.mp3` (up to 4 MiB) in `/Moonshine data/theme/`.
- Settings are saved on the device the launcher was opened from, even if the game image is on a different device.

---

## 3. Navigating the Moonshine menu

Press **Y + Start** in-game to open the menu. Press it again to close.

| Input | Action |
|---|---|
| **L / R** | Switch top-level tabs |
| **C-stick Up / Down** | Move between rows |
| **C-stick Left / Right** | Adjust a value, change pages, or jump between sections in lists |
| **A** | Select, or cycle a setting's value |
| **B** | Go back |
| **X** (on a setting) | **Shine** it: add it to or remove it from your **Quick** favourites |
| **X** (on an action) | Rebind that action's shortcut |
| **Z** (on an action) | Clear that action's shortcut |

Closing the menu after changing something saves your settings automatically, to SD on Wii or to the slot-B memory card in Dolphin.

### Menu map

| Tab | What's inside |
|---|---|
| **Quick** | Every setting you've Shined (starred) with X. |
| **Practice** | TAS projects, Free camera, Savestates, Practice tools, RNG controls, Gameplay and QoL |
| **Runs** | ILs, Stage Loader (playlists and streaks), Records, PB Safety, Timer and splits |
| **Records** | Achievements and practice statistics (also reachable from Runs) |
| **Ghosts** | Record, save, race, watch, import and export ghosts |
| **Display** | Layout editor, Layout profiles, HUD and displays, Timer and splits, Appearance |
| **System** | Button binds and a short in-game Moonshine guide |

---

## 4. Default button binds

A bind is a combo of up to 4 buttons, and it has to match **exactly**: holding extra buttons stops it from firing. Binds work even while the game has disabled your controller, for example during dialogue. Settings and binds are stored **separately for JP, US and PAL**.

### Menu, savestates and practice

| Default | Action | Notes |
|---|---|---|
| **Y + Start** | Open / close Moonshine menu | Also works during ghost Watch. |
| **D-Left** | Savestate: **save** | Saves into the "Save to" slot. |
| **D-Right** | Savestate: **load** | Loads the "Load from" slot or SD file. Keep holding to stay frozen after the load. |
| **D-Down** | Practice: pause / resume | Also cancels a buffered ("Armed") pause. |
| **D-Up** | Practice: advance one frame | The first press pauses live gameplay. Each later press steps one frame. |

### Warps and restarts

| Default | Action | Notes |
|---|---|---|
| **Z** | Open warp wheel | Moonshine's level select. See [Warping](#10-warping-and-restarting). |
| **B + D-Up** | Instant restart | Restarts the current area and keeps the entry point you arrived from (same pipe or door). |
| **Z + B + D-Up** | Full restart | Restarts the current area from its default spawn. |
| **Y + B + D-Up** | Warp to last selected | Repeats your last warp destination. |

### Gecko-style action codes

| Default | Action | Notes |
|---|---|---|
| **X + D-Up** (hold) | Regrab last held object | Puts the last object Mario held back in his hands. |
| **Y + D-Up** | Spawn Yoshi: green | |
| **Y + D-Left** | Spawn Yoshi: orange | |
| **Y + D-Right** | Spawn Yoshi: purple | |
| **Y + D-Down** | Spawn Yoshi: pink | |
| **B + D-Left** (hold) | Fast forward 4× | Only active while held. |
| **B + D-Right** (hold) | Fast forward 8× | Only active while held. |

### Attempt counter (needs Practice tools > Attempt counter On)

| Default | Action | Notes |
|---|---|---|
| **R + X + D-Left** | Attempt −1 | |
| **R + X + D-Right** | Attempt +1 | |
| **R + X + D-Down** | Success −1 | |
| **R + X + D-Up** | Success +1 | |
| **D-Left** | Show attempt counter | Only with **In-stage counter controls** On. It shares D-Left with savestate save. |
| **D-Right** | Add attempt | Only with **In-stage counter controls** On. It shares D-Right with savestate load. |

### Unassigned by default (set them in System > Button binds)

| Action | What it does |
|---|---|
| Toggle input display | Shows or hides the controller overlay |
| Position: save / load | Lite savestate that only saves and restores Mario's position |
| Free camera: toggle | Turns free camera on or off |
| Inputs: record from savestate / replay recording / Practice: stop | Controls for recording and replaying inputs |
| Savestate: cycle both slots | Older control that moves Save to and Load from together |
| Savestate: cycle save slot | Moves Save to to the next slot |
| Savestate: cycle load slot | Moves Load from to the next slot |
| TAS: go to Beginning | Returns to the TAS start point |
| TAS: save / go to Checkpoint 1 | Sets or returns to Checkpoint 1 |
| TAS: save / go to Checkpoint 2 | Sets or returns to Checkpoint 2 |
| TAS: continue | Arms recording again after Stop or Replay |
| TAS: replay | Plays the TAS from its Beginning |

> "Spin inputs: clockwise / counterclockwise" still appear in the bind list, but they do nothing now. Automatic spin generation was removed, so you do spins with the stick yourself.

---

## 5. Changing binds

1. Open **System > Button binds**. C-stick Left/Right jumps between sections.
2. Highlight an action and press **A** to record a combo.
3. Hold the buttons you want, then release one to set the combo. It also sets automatically when you press a 4th button. Press the **C-stick** to cancel.
4. Press **X** on a bind to clear it.

The pause, step, free camera and TAS actions can also be rebound from their own pages: press **X** to rebind and **Z** to clear.

The C-stick and main stick can't be part of a bind. The menu uses the C-stick for navigation.

---

## 6. Savestates

**Practice > Savestates** gives you three compressed memory slots, similar to emulator savestates, on real Wii hardware.

- **Save to** picks which slot the Save bind (D-Left) writes into.
- **Load from** picks which slot, or which SD file, the Load bind (D-Right) restores.
- The two choices are independent. For example, you can Save to State 1 while you keep practising from State 2.
- **Hold Load** to keep the game frozen after the restore, then release when you're ready. If the state was saved during an intro, the intro plays out first. Keep holding to stop on Mario's first controllable frame.
- A state only loads in the **same stage and episode** it was saved in.
- If a new save doesn't fit, **nothing is overwritten**. All older states stay.
- **Clear save slot** empties the Save to slot, after asking you to confirm.
- Memory slots are **wiped when you close the game or reboot**.
- With **Save RNG state** On (the default), loading also rewinds the RNG, so things like King Boo's fruit, manta patterns and enemy RNG repeat exactly.

### Keeping a state on SD (Wii only)

1. Save a normal memory state and make sure it's the **Save to** slot.
2. Go to **Savestates > SD states > Save memory state to SD**. Type a name, press **Start** to confirm (**X + Start** cancels), and wait for it to finish.
3. Later, with the same build, region and setup, enter the same level and episode. Open **SD states > Refresh / first page** and highlight your file:

| Button | Action |
|---|---|
| **Y** | Makes this file your **Load from** target. Your normal Load bind then restores it. |
| **A** | Imports it into the **Save to** memory slot, after confirmation. |
| **Start** | Renames it |
| **X** | Deletes it, after confirmation |

---

## 7. Practice pause and frame advance

| Default | Action |
|---|---|
| **D-Down** | Pause or resume gameplay |
| **D-Up** | Pause, then advance one frame per press |

- Practice pause freezes gameplay **and the QFT**. Each Step advances one game frame, and the timer moves with it. Music keeps playing.
- You can press Pause or Step during a load, an intro or a cutscene. The display shows **Armed**, and the game pauses as soon as you can control Mario.
- **To jump on a Step:** hold A, then tap Step. To press A again on a later frame, release A and press it again before stepping.
- **Jump-dive:** hold A+B and tap Step. Only the Step buttons are hidden from Mario.
- **Spins:** move the main stick yourself before each Step. Free camera has to be **Off**.
- Holding Step doesn't auto-repeat.
- Any attempt that uses pause, step, free camera, fast forward or similar assists is marked **TAS** next to the QFT. It **can't set an ordinary PB**. Restart the stage for a clean attempt.

---

## 8. Free camera

Open **Practice > Free camera** and turn it On. This pauses gameplay automatically.

| Input | Camera action |
|---|---|
| Main stick | Move |
| C-stick | Look |
| L / R (analog) | Descend / ascend |
| Hold X | Speed boost |

Options on that page:
- **Movement speed:** 0.25× to 4× (default 1×)
- **Look sensitivity:** 0.25× to 4×
- **Camera smoothing:** Off (default), or 0.1 to 1.5 s of easing
- **Reverse sideways:** flips main-stick left/right
- **Hide all HUD:** hides game and Moonshine overlays while the camera is on, which is useful for filming
- **Resume gameplay / Pause gameplay:** lets the game run at normal speed while the camera stays free
- **Recenter:** goes back to the retail camera view

> **With the camera On, Mario gets no input.** Turn it Off before you step a jump or a spin.

---

## 9. TAS projects

**Practice > TAS projects** records Mario's inputs frame by frame. A project has a **Beginning** and up to **two checkpoints**.

1. **New TAS:** saves the Beginning automatically, including RNG, and stays paused.
2. Hold Mario's buttons and press **Step** to record one frame, or **Resume** to record real-time play.
3. **Checkpoints > Save Checkpoint 1** marks a spot. **Go to Checkpoint 1** takes you back there to redo what came after it. Checkpoint 2 works the same way.
4. **Replay** plays the recording from the Beginning. **B** or **Start** stops it. If playback drifts from the original, **DESYNC fN** shows the first frame that differed.
5. **Save TAS** stores the whole project on SD under `/Moonshine data/tas/`, with its name confirmed by **Start**. **Open TAS** loads it again later. In the Open list, **Start** renames and **X** deletes.

Limits: **4096 frames** and **32 area or movie transitions** per take. Cutscenes and movies play in real time, so you can't frame-advance inside them. For Replay and Go to Beginning, you have to go back to the starting area yourself.

The header shows your frame count, for example **200/4096**. **TAS banner** (on by default) shows recording progress under the game screen.

---

## 10. Warping and restarting

### The warp wheel (default: Z)

Press **Z** to open the wheel. The stage freezes while it's open.

| Input | Action |
|---|---|
| **Main stick** | Point at a slice (8 directions clockwise from Up, plus the centre) |
| **A** | Choose a world, then choose a destination |
| **X** | Switch between episodes and **subareas** |
| **B** | Go back, or close the wheel |

**Root wheel** (clockwise from Up): Bianco, Ricco, Gelato, Pinna, Sirena, Pianta, Noki, Hotel (Up-Left), with **Delfino** in the centre.

- Each world's main wheel shows **episodes 1–8**. Press X for its subareas, such as Windmill, Blooper Race, Sand Bird, Balloons, King Boo, the Bottle, the Eel and Red Fish.
- **Hotel** has Sirena hotel and casino scenarios (Ep2–Ep8 hotel, Ep4 and Ep5 casino, Ep8 reds).
- **Delfino** has plaza states (Bianco Plant, Bianco Chase, Ricco/Gelato Plants, Peaceful, Pinna Cutscene, Yoshi, Flooded, Post-Corona). Press X for **secrets**: Beach Pipe, Pachinko, Grass Pipe, Lilypad, Jail, Airstrip, Airstrip Reds, Bowser, with Corona in the centre.

**Examples**
- *Pianta Village, Episode 4:* Z → point Down-Left, A → point Down-Right, A.
- *Pachinko secret:* Z → A with the stick centred (Delfino) → X → point Up-Right, A.

### Classic "Instant Level Select" chart

If you learned the Gecko Instant Level Select, the chart still works. **Hold B + D-Up**, point the **C-stick**, and add modifier buttons:

| C-stick direction | World |
|---|---|
| Up | Bianco |
| Up-Right | Ricco |
| Right | Gelato |
| Down-Right | Pinna |
| Down | Sirena |
| Down-Left | Pianta |
| Left | Noki |
| Up-Left | Secrets |
| Centre | Special / restart |

| Modifier | Episode |
|---|---|
| none | 1 |
| L | 2 |
| R | 3 |
| L+R | 4 |
| Z | 5 |
| Z+L | 6 |
| Z+R | 7 |
| Z+L+R | 8 |

**X** switches to the chart's sub-level row, which goes clockwise from Up: Windmill, Blooper Race, Sand Bird, Balloons, Casino, Noki Bottle, Eel. **Y** switches to the Delfino plaza states. **X + Z** picks Pinna Park scenarios and **Y + Z** picks Sirena hotel scenarios. With the C-stick centred, **B + D-Up** is an instant restart, adding **Z** makes it a full restart, and adding **Y** warps to your last destination.

### Restarts

- **B + D-Up**: Instant restart. Mario comes back out of the same entrance.
- **Z + B + D-Up**: Full restart from the area's default spawn.
- **Y + B + D-Up**: Warp to last selected.
- If you press a restart during a death or a save dialog, it's **queued** and shows a "Restart queued" popup.
- **Area lock** (Gameplay > World rules) turns every exit into a restart of the current area.

---

## 11. ILs, playlists, streaks and PBs

### ILs (Runs > ILs)

ILs doubles as a warp list and a personal-best tracker. It covers all 96 non-blue-coin Shines, 10 no-FLUDD secret splits, and 11 **Any% plaza segments** laid out the way they appear in a run.

- Pick a level to warp to it. Finishing it records a PB, with a popup and a **fanfare** when you beat your old time.
- Press **Z** on a supported row to choose its **starting episode**. This works for the seven 100-coin routes, Gelato/Noki/Pianta Hidden, and all ten Full Reds routes.
- The settings at the top of the tab control PB recording, the popup, the fanfare, recent-IL history and short names.

### Stage Loader (Runs > Stage Loader)

- **Playlists:** build a custom list of levels, save it, then **Run playlist** to play them back to back. Built-in presets are included.
- **Streaks:** choose a level, set a target time and a number of finishes, then **Start streak** to practise consistency. **Streak auto-reset** restarts the level for you. If an attempt doesn't count, the counter briefly says why.
- **Fast Any%:** Pinna routes start on the beach, and Noki 3 starts outside the bottle.

### PB Safety (Runs > PB Safety)

This page lists every setting that would stop an IL PB from counting, such as Stage intro skip, forced boss patterns, hidden-item reveals, hurtboxes, Ricco checkpoints, or Record IL PBs turned off. Press **X** to fix them all at once. The root tab shows an alert count while anything is blocking.

### Records

**Records** shows achievements and practice statistics: attempts, finishes, play time and split history.

---

## 12. Ghosts

In **Ghosts**, highlight a saved ghost to act on it:

| Button | Action |
|---|---|
| **A** | Race |
| **Y** | Watch |
| **X** | Delete |
| **Z** | Export to a shareable `.smsghost` |

- **Save latest ghost** saves your most recent run as a personal ghost. The library is split into pages (C-stick Left/Right) and is limited only by storage space. Each ghost can be about 15 minutes long.
- **Watch2** shows two ghosts at once. **B** or **Start** leaves Watch. The full menu combo opens the menu without ending Watch.
- Pause, Step and free camera all work while you watch.
- **Ghost inputs:** Off / Ghost / **Both ghosts**. This shows the ghost's controller, or both controllers side by side.
- To share ghosts, look in `Moonshine data/ghosts/share/` for your exports. Put ghosts from other people in `Moonshine data/ghosts/import/`.
- Ghosts recorded with practice pause, step, free camera or state loads are marked **TAS**. Paused time is cut out of them.

---

## 13. Timers, QFT and splits

Find these in **Runs > Timer and splits** or **Display > Timer and splits**.

- **Sunshine timer:** Always / Shine only / Hidden. The game's native timer, which you can fully restyle in the layout editor.
- **Bottom-left QFT:** Always / **On freeze** (default) / Hidden. A compact quick-frame timer.
- **Freeze duration:** Off, 0.5 s, **1 s** (default), 2 s, 3 s or 5 s. How long the QFT holds on a freeze event.
- **QFT freezes page:** choose which events freeze the QFT, such as coins, items, talking, jumps, dives, ledge grabs, wall kicks, Yoshi, and boss or event triggers. Most are On by default. Yellow coin is Off.
- **QFT section history:** shows a list of recent section times.
- **Level splits:** On by default. Shows handmade checkpoint splits, up to 8 segments per route.
- **Split comparison:** Off / **PB** (default) / SOB (sum of best) / Ghost. `--` means there's no data to compare against.

---

## 14. Movement timing displays

In **Display > HUD and displays > Movement displays**:

| Display | What it shows |
|---|---|
| **Wallkick display** | Timing of your last wall kick |
| **Rollout display** | How many frames A was effectively held during a rollout |
| **Dust display** | Frames from landing to the rollout input |
| **Jump display** | Frames from landing to your next jump (`1f`–`6f`, then `Late`) plus the quarter-frame phase, for example `1f qf2` |
| **Buttslide display** | A **Ready / Waiting** cue for the buttslide jump |
| **GB skip timing** | The B press after a full A jump for the no-hover GB skip (target 9 f, Y≈404, V≈6): **Early / On time / Late / Check jump** |

Each display has its own style editor under **Layout editor > Practice feedback**.

---

## 15. Layouts, colours and HUD editing

### Layout editor (Display > Layout editor)

Groups: Timers, Controller inputs, Metadata, Native HUD colours, Custom text, Practice feedback, and Menu and notifications.

| Input | Action |
|---|---|
| C-stick Up / Down | Pick an option |
| C-stick Left / Right | Adjust it |
| **Start / X + Start** | Next / previous target (All, or one character or element) |
| Hold **Y** while adjusting | Change HSL values by 1 instead of 4 |
| **A** | Keep your edits (asks to confirm) |
| **B** | Discard your edits (asks to confirm) |
| **Z** | Reset the selected option (asks to confirm) |

- Colours use **Hue / Saturation / Lightness**.
- The **Sunshine timer** editor covers position, size, opacity, all 13 characters, TIME/TEMPO and the streak counter. Each can be set to **Original** (keeps the retail shading, which you can tint) or **Custom**.
- **Metadata** has field gap, row gap, fields per row and value widths.
- **Custom text** gives you up to three custom text overlays, typed on a controller keyboard.

### Layout profiles (Display > Layout profiles)

There are five named profiles per region. Press **Y** to save the current layout under a name (**Start** saves, **X + Start** cancels), and **A** to apply one. A profile includes overlays, custom text, timer and menu styles, Mario and FLUDD colours, and visual options. It **does not** include binds, practice rules or records.

### Mario and FLUDD colours (Display > Appearance > Mario appearance)

- **Mario colours:** cap, shirt, overalls, gloves, shoes, sunglasses and Sunshine shirt.
- **FLUDD colours:** body paint, metal, straps, tank, spray, hover, rocket and turbo nozzles, sprayed water, and water highlights.

---

## 16. Every code and setting, explained

Defaults are in **bold**. Most of these are native versions of the GCT generator's practice Gecko codes.

### Practice > Gameplay and QoL

**General gameplay**

| Setting | Default | What it does |
|---|---|---|
| Fast text | **Off** | Speeds up dialogue and message boxes. |
| Infinite lives | **On** | Mario's life count never goes down. |
| Infinite juice | **Off** | Yoshi's juice meter never drains. |
| Free pause | **On** | Lets you pause during normally locked moments. |
| Exit area everywhere | **On** | "Exit Area" appears anywhere you can pause. |

**Skips and unlocks**

| Setting | Default | What it does |
|---|---|---|
| FMV skips | **On** | Lets you skip supported pre-rendered movies. |
| Intro skip | **On** | Skips the boot logos. Takes effect on the next launch. |
| Unlock Yoshi | **Off** | Yoshi is available without story progress. |
| Unlock nozzles | **Off** | Rocket and Turbo nozzle boxes are available. |
| Respawn one-time shines | **On** | One-time Shines come back so you can practise them again. |

**World rules**

| Setting | Default | What it does |
|---|---|---|
| FLUDD in secrets | **Completed** / No FLUDD / All secrets | Controls when FLUDD is taken away in secret stages. |
| Area lock | **Off** | Every stage exit becomes a restart of the same area. |
| Disable blue coin flag | **On** | Blue coins don't mark themselves as collected, so they respawn. |
| Force Piantissimo pattern | **Off** / Slowest / Fastest | Forces a Piantissimo race speed. |
| Disable 3rd Chomplet aggro | **Off** | Stops the third Chomplet from targeting Mario early. |
| Yoshi/nozzle save prompt | **Off** | Warns you before Yoshi or nozzle progress is saved. |
| Disable retail pause | **Off** | Uses only Moonshine's pause handling. |

### Practice > Practice tools

| Setting | Default | What it does |
|---|---|---|
| Nozzle lock | **Unlocked** / Rocket / Turbo / Hover | Forces Mario to keep the chosen nozzle. |
| Force plaza events | **On** | Keeps Delfino Plaza story events available. |
| Never pause IGT | **On** | In-game time keeps running while paused. |
| Shadow Mario HP meter | **Off** | Shows Shadow Mario's remaining health. |
| Stage intro skip | **Off** | Skips the camera flyover at the start of a stage. *(Blocks IL PBs.)* |
| Deathless blooper surfing | **Off** | Blooper surfing crashes don't kill Mario. |
| No shine get animation | **Off** | Skips the Shine-get animation. |
| Fruit never times out | **Off** | Loose fruit never disappears. |
| Disable Z Menu | **On** | Stops Z opening the retail menu, so Z can open the warp wheel. |
| Disable Moonshine Warps | **Off** | Turns off Moonshine's quick stage warps. |
| Attempt counter | **Off** | Counts attempts and successes for the current area. |
| In-stage counter controls | **Off** | Lets the bare D-Left/D-Right counter binds work inside a stage. |
| Force Box Game | **Off** / 1 / 2 | Forces a Delfino crate-game layout. Your real save flags aren't changed. |
| Free camera speed | **1×** | Free camera movement speed. |

### Practice > Savestates

| Setting | Default | What it does |
|---|---|---|
| Save RNG state | **On** | Savestates include the RNG seed, so patterns repeat. |
| Savestate feedback | **On** | Shows a popup after each save or load. Errors always show. |

### Practice > RNG controls

| Setting | Default | What it does |
|---|---|---|
| Pattern selector | **Off** | Makes patterns repeatable for supported practice. |
| Any fruit opens Yoshi eggs | **Off** | Any fruit colour hatches a Yoshi egg. |
| King Boo fruit cycle | **Off** | Forces King Boo's valid-fruit and no-fruit cycle. *(Blocks IL PBs.)* |
| Disable Petey tornado | **Off** | Petey never uses his tornado. *(Blocks IL PBs.)* |
| Petey flight route | **Retail** / N1-S1-S2-S3 | Forces Petey's flight route. *(Blocks IL PBs.)* |
| Crane speed | **Retail** / Slowest / Slow / Medium / Fast / Fastest | Keeps Ricco's crane speed inside the chosen band. |
| Fruit machine | **Retail** / Durians only | Makes the Ricco fruit machine give durians. |
| Gelato 6 red-coin fish | **Retail** / Pattern 1–4 | Repeatable fish pattern (testing). |
| Blue-coin birds | **Retail** / Pattern 1–4 | Repeatable bird pattern (testing). |

### Runs > ILs / Stage Loader

| Setting | Default | What it does |
|---|---|---|
| Record IL PBs | **On** | Completed ILs can update your saved PB. |
| PB popup | **On** | Shows a popup when you set a PB. |
| PB fanfare | **On** | Plays a fanfare when you set a PB. |
| Recent IL history | **Off** | Shows your recently completed ILs. |
| Short recent IL names | **On** | Uses shorter names in the recent list. |
| Stage session display | Full notification / **Counter** / Off | How streak and session progress is shown. |
| Streak auto-reset | **On** | Resets the level automatically during streaks. |

### Display > HUD and displays

| Setting | Default | What it does |
|---|---|---|
| Wallkick / Rollout / Dust display | **Off** | Movement timing readouts (see [section 14](#14-movement-timing-displays)). |
| Jump display / Buttslide display | **Off** | Landing-to-jump timing and the buttslide Ready cue. |
| GB skip timing | **Off** | B-press timing for the no-hover GB skip. |
| Hidden fruit and coins | **Off** / Both / Fruit / Coins | Shows where spray-hidden fruit and coins are. *(Blocks IL PBs.)* |
| Hidden item labels | **On** | Adds "Fruit" and "Coin" labels to those markers. |
| Enemy hurtboxes | **Off** / Wireframe / Transparent / Solid | Draws enemy damage volumes. *(Blocks IL PBs.)* |
| Hurtbox targets | **All enemies** / Eely teeth only | Which hurtboxes to draw. |
| Ricco 2 checkpoints | **Off** | Shows the Ricco race's checkpoints in order. *(Blocks IL PBs.)* |
| TAS banner | **On** | Shows TAS progress and help text. |
| Ghost inputs | **Off** / Ghost / Both ghosts | Shows the ghost's controller. |
| Show BGM slot counter | **Off** | Audio diagnostic that shows free music slots. |
| Restart queued popup | **On** | Tells you when a restart has been queued. |

### Display > Appearance

| Setting | Default | What it does |
|---|---|---|
| Shine outfit | **Off** | Mario wears his Shine celebration outfit. |
| Helmet / Cap / Shades / Shine shirt | **Default** / Always / Never | Controls when each piece of Mario's costume shows. |
| Mute background music | **Off** | Mutes music but keeps sound effects. |
| Episode names as IDs | **Off** | Shows internal IDs instead of episode names. |
| Shiny shines | **Off** | Gives Shine Sprites a stronger gleam. |
| Visible goop | **Off** | Makes normally hidden goop easier to see. |

### Built in, always on

- **Fix Manta Splitting:** fixes a Nintendont bug in the manta episode.
- **Automatic memory-card encoding:** picks ANSI or SJIS to match the version you launched.

---

## 17. Your data folder

Everything lives in **`/Moonshine data`** on the launcher's device. The launcher moves older files there on first launch.

| Path | Contents |
|---|---|
| `moonshine.ini` | Settings, layouts and binds (`[settings_jp]`, `[binds_us]` and so on) |
| `moonshine_*.bin` | Records, achievements and journals |
| `theme/` | `background.png` and `bgm.mp3` |
| `ghosts/` | Ghosts, plus `import/` and `share/` |
| `states/` | Named SD savestates (`.mss`) |
| `tas/` | Saved TAS projects |
| `layouts/` | Layout profiles |
| `crashes/` | Crash reports (the newest 16 are kept) |
| `backups/` | Files preserved during migration |

Binds in `moonshine.ini` can be edited by hand, for example `savestate_save = DLeft` or `regrab_object = X+DUp`.

---

## 18. Dolphin notes

- Put a **memory card in slot B**. Moonshine stores its settings there, separately from your normal Sunshine save.
- Set **Graphics > Hacks > Texture Cache Accuracy** to **Safe**, or goop won't restore correctly when you load a savestate.
- Use a **current Dolphin release**. Dolphin 5.0 can leave input stuck when the menu is paused.
- SD states and SD TAS files need the Wii launcher. The three memory slots still work in Dolphin.

---

## 19. Tips, gotchas and troubleshooting

- **Bind overlaps:** the attempt counter's "Show" and "Add" binds share D-Left/D-Right with savestates. That's why they only work when *In-stage counter controls* is On.
- **Z and the warp wheel:** leave *Disable Z Menu* On, or Z also opens the retail menu.
- **Binds need an exact match.** If a bind won't fire, check that you aren't holding an extra button.
- **"TAS" next to the timer** means the attempt used an assist (practice pause, step, free camera, fast forward, regrab, a state load and so on). Restart the stage to get a PB-eligible attempt.
- **PB not saving?** Check **Runs > PB Safety**.
- **A state won't load?** You must be in the same stage and episode, on the same build and setup. The error stays on screen even if feedback popups are off.
- **Reporting a problem:** include the region, scene, settings, steps to reproduce and the build checksum shown in-game. Keep the matching crash files from `/Moonshine data/crashes` together.
- Press **System > Moonshine guide** for a short reference inside the game.
