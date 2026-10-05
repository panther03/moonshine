# Moonshine V2.3.3 Frame By Frame

## Ghosts with FLUDD

- Fixed the ghost library staying on **Not scanned** after saving a new ghost. Existing saved files do not need replacing.
- Faster ghost-list loading and page changes: file headers are read together, and nearby pages reuse the checked list. Refresh still reads the SD again.
- New ghosts record the selected nozzle, spray direction and when water actually fires. Ghosts show FLUDD and visual spray, with droplets and ground splashes that cannot affect gameplay.
- Corrected FLUDD's attachment to the ghost's chest and replaced the diamond-shaped water with Sunshine's own water textures. Individual droplet paths and splashes are still visual approximations; they do not reproduce every original particle or collision.
- Restored FLUDD's closed tank and resting pump shape. Spray is larger and uses separate water and highlight passes, and ghosts now cast ground shadows.
- Watching one ghost can activate platforms that start when Mario stands still on them. Racing remains visual only; Watch 2 does not simulate two separate riders.
- Added **Mario** as a ghost appearance alongside Shadow Mario and Piantissimo, using the existing model memory.
- New recordings last up to **10 minutes**. Existing ghosts remain readable, including older recordings over 10 minutes. Older files keep their recorded movement and inputs; missing FLUDD data is not guessed.

## Streaks and routes

- **Stageloader** and **Streaking** now have separate entries under Runs.
- RNG controls no longer disqualify streaks. Petey and King Boo controls also allow ordinary PBs and no longer turn the HUD red. Other RNG controls retain their individual PB restrictions.
- Savestates work during streak practice. Loading a state lets you practise the attempt without adding to, or breaking, the streak; restart normally to resume counting clean attempts.
- Added **Gelato Enter (GE)** under Plaza movement: start at the Noki Bay exit and finish when entering Gelato Beach. Choose the Plaza state using its State selector, including the Yoshi Plaza introduction. Fast Any% keeps its own route.
- Warps, level selection and RAM/SD state loads are available while the save box is open. An actual memory-card write still has to finish first.
- Fixed IL launches remaining stuck in native pause/save screens. Save/load shortcuts also accept other held gameplay buttons, including A+B during frame advance.
- Gelato Enter shows its chosen Plaza state by name, such as **Peaceful** or **Yoshi unlock**, instead of an ambiguous episode number.
- Rejected streak finishes now explain the reason, such as intro skip, frame advance or restarting inside a full-level route. The reported Full Reds streak misses have not been reproduced yet; these messages should help identify the cause.

## Displays and menus

- Position loads mark the attempt **TAS**. If QFT is hidden, the TAS label now sits farther inside the bottom-left corner so it stays visible.
- Fixed the overlapping QFT text beneath a level split.
- The failure banner uses the usual Creation editor for position, size, colour and background. Visibility and duration remain separate controls.
- **Timer and splits > Cosmetics** gives quick access to Sunshine timer and QFT appearance.
- Removed the duplicate Records entry inside Runs; use the Records tab.
- Savestates has an error-message toggle. System messages can also be disabled separately from PB and achievement popups.
- Fixed Visible goop's appearance after loading a state made with a different setting. The code review found no change to Sirena 6's gameplay goop when the option is Off.

## Loading and memory

- Read-speed unlocking now applies to the early boot reads too. The disc cache retains unaffected data when it wraps instead of discarding everything; console loading-time gains still need measurement.
- The launcher caches checked ghost assets in **Moonshine data/cache**. After the first successful extraction, later launches avoid decoding the same two level archives again. This targets the wait after Launch Game; it does not establish faster level loads or resets.
- Removed unused compression paths without changing savestate output or reducing the space available for states.

## Updating

Use the launcher and mod files from the same ZIP. Keep your settings, layouts, records, ghosts and saved states. Public V2.3.1/V2.3.2 state compatibility remains, subject to the existing region, episode and state checks.

The **English-Menus** launcher supports **US, PAL and JP** games. **Japanese-Menus** is the separate Japanese-language download; its in-game translation applies to JP Sunshine.
