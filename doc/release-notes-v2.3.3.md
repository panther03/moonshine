# Moonshine V2.3.3 Frame By Frame

FLUDD ghosts, a regular Mario ghost appearance, smoother streak practice and faster savestates. Ordinary QFT timing is unchanged.

## Choose your download

| Download | Supported games | Moonshine menu language |
| --- | --- | --- |
| **ENGLISH-MENUS Launcher** | US, PAL and JP | English throughout |
| **JAPANESE-MENUS Launcher** | US, PAL and JP | Japanese launcher; Japanese in-game menus on JP, English on US/PAL |
| **ENGLISH-MENUS Dolphin** | US, PAL and JP | English throughout |
| **JAPANESE-MENUS Dolphin** | JP only | Japanese |

The Launcher ZIP is for the Homebrew Channel and works with your own disc or game image. Dolphin ZIPs contain BPS patches for your own clean ISO. Sunshine's own language is unchanged. Some Japanese status messages and the built-in Guide body remain English.

## Ghosts

- New recordings show **FLUDD**, nozzle changes, upper-body animation, spray, droplets, floor splashes and ground shadows. Water is a visual approximation and cannot affect gameplay.
- Added **Mario** alongside Shadow Mario and Piantissimo. Corrected ghost lighting, FLUDD attachment and resting shapes, spray speed and hover outlets.
- **Watch one ghost** now activates standing platforms, including the final Sirena 4 secret platform. Racing remains visual only; Watch 2 does not simulate two separate riders.
- Fixed the ghost library staying on **Not scanned** after a save, and made list loading and page changes faster.
- New ghosts record up to **10 minutes**. Older ghosts remain readable, including longer recordings; files without FLUDD data keep their previous behavior.

## Streaks and practice

- **Stageloader** and **Streaking** have separate entries under Runs. The duplicate Records shortcut is removed.
- RNG controls now allow streaks. Petey and King Boo controls also allow ordinary PBs; other RNG controls keep their individual PB restrictions.
- Loading a savestate during a streak lets you practise without adding to or breaking the streak. Restart normally to resume counting clean attempts.
- Added **Gelato Enter (GE)** under Plaza movement, from the Noki Bay exit to Gelato Beach. Its selector uses Plaza-state names such as **Peaceful** and **Yoshi unlock**.
- Fixed IL launches from native pause/save screens. Warps and RAM/SD state loads work while the save box is open, after any active memory-card write finishes.
- Save/load shortcuts accept other held gameplay buttons, including A+B during frame advance, without repeating while held.
- Fixed manual streak death restarts waiting for the death animation. Console soft reset ends the active streak or stageloader session.
- Rejected streak finishes now explain the reason. Reports of missed **Sandbird/Noki 6 Full Reds** streak finishes remain unconfirmed; please include the displayed reason and practice settings if one fails.

## Savestates and loading

- Faster state checks and less copying reduce savestate work. Saving a third state avoids repeated compression/checksum work while retaining all existing slots and integrity checks.
- In user Wii testing, Plaza's third save fell from **6.674 to 1.435 seconds** during this update's development. The final change saved a further reported **1.6 seconds** on third saves in both **Pinna 3 and Bianco 5**. Results depend on the scene and occupied slots.
- Fixed Mario and FLUDD disappearing for the first restored frame after loading a state.
- More efficient disc/ISO reads and caching reduce storage work. User Wii testing found shorter Sirena 4 resets and transitions; gains vary by setup.
- The launcher caches ghost model assets in **Moonshine data/cache**, avoiding repeat extraction on later launches, including after power-off.
- Fixed settings files above 32 KB being rejected; launcher and game now share a 64 KB limit.

## Displays and menus

- The failure banner now uses the **Creation editor** for position, size, colour and background.
- **Timer and splits > Cosmetics** provides quick access to both timer appearances. Fixed overlapping QFT text beneath a level split.
- Position loads mark the attempt **TAS**, with a visible bottom-left label when QFT is hidden.
- Savestate errors and system messages have separate visibility controls from PB and achievement popups.
- Fixed PAL Fast Text's missing-message error, freecam Hide all HUD leaving coin sparkles visible, and Visible goop's appearance after state loads.

## Updating

Use the launcher and mod files from the same ZIP. Keep your settings, layouts, records, ghosts, saved states and TAS projects. **Public V2.3.1/V2.3.2 states and TAS projects remain compatible**, subject to the existing region, episode and state checks. Older development builds still require their matching release. Standalone Dolphin keeps its three RAM state slots; SD file storage requires the launcher.
