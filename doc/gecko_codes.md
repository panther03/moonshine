In general, we try to make it clear from which code the functionality comes from using **bold text**. Sometimes there are multifunction codes like DPad functions; we name exactly the feature we are talking about within that code when that is the case.   

# Simple On/Off Toggles or Select Options

The following Gecko codes either force some behavior statically, that should become built into the mod and toggleable dynamically, or they have some in-game settings to turn on and off with a bind that should instead become a toggle in the menu alongside the static codes. When it is not obvious we describe what those menu options should be. None of these will have binds even if the original gecko codes do.

- **Any Fruit Opens Yoshi Eggs**
- **D-Pad functions**: Fast Text/Restore Dialog Boxes
    - D-Pad Up replaces all text with "!!!" and D-Pad down reverts to normal text. This should just be one toggle in the menu and no longer have a bind.
- **D-Pad functions**: FLUDD in secrets 
    - X+D-Pad Left forces no FLUDD in secrets, X + D-Pad Right forces FLUDD in all secrets, and X+D-Pad Down makes FLUDD appear in completed secrets (default). Each of these FLUDD setting keybinds should become options in a 3-state setting in the menu.
- **Deathless Blooper Surfing**
- **Disable Blue Coin Flag**
- **Enable Exit Area Everywhere**
- **FMV Skips**
- **Fast Piantissimo**
- **Force Plaza Events**
- **Free Pause**
- **Fruit Never Time Out**
- **Infinite Juice**
- **Infinite lives**
- **Mute Background Music**
- **Never Pause IGT**
- **No Shine Get Animation**
- **Nozzle lock**
    - B + D-Pad Left locks to the Rocket Nozzle; B + D-Pad Right locks to the Turbo Nozzle; B + D-Pad Up locks to the Hover Nozzle; and B + D-Pad Down releases the lock. 
    - This should instead be one setting in the menu "Nozzle Lock" with 4 options: Unlocked, Rocket, Turbo, Hover.
- **Replace episode names with their ID**
- **Respawn one-time shines**
- **Shadow Mario HP Meter**
- **Shine Outfit**
- **Shiny Shines**
- **Stage Intro Skip**
- **Unlock Nozzles**
- **Unlock yoshi**

# Simple Actions/Binds

These are codes that just do something in-game when you press a button like spawning an object. They don't have any menu settings, but their bind should be configurable in the menu. We list the default button binding they should have.

- **D-Pad Functions**: Regrab Last Held Object (Default X + D-Pad Up)
- **Spawn Yoshi**: 
    - One bind for each color of yoshi. Defaults:
        - Orange -> Y + D-Pad Left
        - Purple -> Y + D-Pad Right
        - Pink -> Y + D-Pad Down
        - Green -> Y + D-Pad Up 
- **Fast Forward**: Default binds:
    - 4x speed -> B + D-Pad Left
    - 8x speed -> B + D-Pad Right

# Complex Features

These features may have binds, menu settings, and/or their own GUI elements, and may take bits from multiple codes.

## Warp Wheel ({Instant} Level Select)

A separate menu (triggered by Z by default) that opens a wheel with 8 slices, 9 including a region in the center, showing warp options. The warps are organized into a hierarchy of a root wheel which selects roughly speaking the world for the warp, and then each sub-wheel has different warps for the various episodes and subareas in the worlds. Pointing the control stick in one of the 8 regions selects that region. Directions go from up notch (1) clockwise. Each region has some indicative text (some of it can be shortened to fit, like Bianco Hills -> Bianco ; Gelato Beach -> Gelato, etc. As long as it's indicative.) By default, when you select a world and press A, it will then show the standard episodes of that world. For all the worlds in the root wheel except for Delfino and Sirena (Inside), this is simply the standard episodes 1 - 8. Selecting an episode and pressing A warps you to it. If X is pressed while in the menu, then "subarea mode" is enabled and then instead you see the subareas you can warp to in that world. Some usage examples:

To go to Pianta Village, Episode 4:
- Press Z
- Hold downleft notch on the control stick and press A
- Hold downright notch on the control stick and press A

To go to Pachinko secret:
- Press Z 
- Press A with the control stick neutral
- Press X, then hold upright notch and press A 

Root Wheel
1. Bianco Hills
2. Ricco Harbor
3. Gelato Beach
4. Pinna Park
5. Sirena Beach
6. Pianta Village 
7. Noki Bay
8. Sirena (Inside)
Center: Delfino 

Bianco: [subareas]
2. Windmill
3. Bianco 3
6. Bianco 6

Ricco: [subareas]
1. Blooper battle
2. Blooper race
4. Ricco 4
5. Blooper battle

Gelato: [subareas]
1. Gelato 1
4. Sand Bird

Pinna: [subareas]
1. Mecha-Bowser
2. Pinna 2
6. Pinna 6
8. Balloons

Sirena: [subareas]
2. -> Sirena 2
4. -> Sirena 4 
5. -> King Boo

Sirena (Inside): [main]
1. -> Ep. 3 Hotel
2. -> Ep. 4 Hotel
3. -> Ep. 4 Casino
4. -> Ep. 5 Hotel
5. -> Ep. 5 Casino
6. -> Ep. 7 Hotel
7. -> Ep. 8 Hotel Red Coins 

Pianta: [subareas]
5. Pianta 5

Noki: [subareas]
3. Bottle
4. Eel
6. Noki 6
8. Red Coin Fish

Delfino: [main]
1. Bianco Plant
2. Bianco Chase
3. Ricco/Gelato Plants
4. Peaceful
5. Pinna Cutscene
6. Yoshi Unlock
7. Flooded Plaza

Delfino: [secrets]
1. Beach Pipe
2. Pachinko
3. Grass Pipe
4. Lilypad
5. Jail
6. Airstrip
7. Airstrip reds
8. Bowser
Center: Corona

Note that some stages are sparse. This means that there should just be no slices drawn at the corresponding angles. 

## Level Restart

Three ways to restart, as actions configurable with binds:

1. Instant Restart: B + D-Pad Up -- restart the current area keeping the respawn position Mario arrived at
2. Full Restart: Z + B + D-Pad Up -- restart the current area, respawn position reset to the area's default
3. Warp to Last Selected: Y + B + D-Pad Up -- warp to the _last selected warp_

All three are the same warp with a different destination; Instant Restart additionally points `mCurrentScene` at `mPrevScene` first, which is what preserves the respawn position. Upstream's separate **Instant Restart** gecko code is not ported -- it claims the same thing but blanks `mCurrentScene` instead, which is precisely what loses the respawn position.

In addition, you should implement the Area Lock feature of **Instant Level Select**. This will make all warps restart the current area instead of sending Mario to other areas. (Read the documentation of the code for more details). This should be an on/off toggle setting in the menu.

## Pattern Selector

TODO

- Pattern Selector -> change the RNG pattern in the settings menu. Also an option to turn on and off a HUD outside the settings (This is customizable with the webpage same as currently.) 

## Attempt Counter

TODO

binds:
- Increase Stage Attempt Counter by one
- Increase and Decrease Manual Attempt Counter by one 

## Lite Savestate

TODO

Absorbs all the various 'savestate' codes (and the savestate function in D-Pad functions). Each of these flags is an on/off toggle in the setting. 

- Lite Savestate: Position
- Coin 
- Red Coin
- IGT 
- become flags
- Lite savestate has its own bind

## QFT & QSFT

**Implemented** — `src/qftimer.cpp`; see doc/gecko_porting.md, "Quarterframe
Timer + Section Timer", for the site-by-site derivation. Visibility, the section
timer's reset behaviour and the eighteen freeze triggers are settings (the
`Timer` and `QF Freeze` menu tabs); appearance is the pinned config block the
web configurator overwrites with a Gecko code (`site/FORMAT.md`).

**It came to ~3.3 KB, against a 1536-byte `.gct` for the same two codes with
every trigger on.** `qftimer.cpp` itself is 2561 bytes; the rest is the 21
settings (descriptors + labels, ~540) and the two menu tabs. The excess over the
gct is almost exactly the machinery the gct does not need, because a `.gct` is
static and this is not:

| | bytes |
|---|---|
| `gSites` + `qfTimerApply` + `installCaves` — install/uninstall a site per trigger toggle | 860 |
| everything else in `qftimer.cpp` (caves, state machine, both renderers, the config block) | 1701 |

1701 against the gct's 1536 is the like-for-like comparison, and the 336 bytes of
caves are *less* than the asm the gct carries. Collapsing the eighteen triggers
to a single on/off would recover most of the 860.

The original requirement follows.

Reimplement quarterframe timer gecko code exactly. It is very important that this code is implemented faithfully, because it is used for timing IL runs. Slight mismatches in behavior could ruin trust in the mod. Freezing behavior is controlled by the following toggles (from the original gecko code), which have entries in their own settings tab:

- When a yellow coin is collected	
- When a red coin is collected	
- When a blue coin is collected	
- When an item (e.g. nozzle) is collected	
- When dialogue starts	
- When a cutscene starts	
- When an NPC is cleaned	
- When a platform is destroyed in the Bowser fight	
- When Yoshi is mounted	
- When Mario holds an object	
- When Mario throws an object	
- When Mario puts down an object	
- When Mario triple jumps	
- When Mario spin jumps	
- When Mario ledge grabs	
- When Mario wall kicks	
- When Mario bounces (e.g. on a roof)	
- When Mario jumps from a rope

Quarterframe section timer is behind a toggle in the settings menu as well to hide it. It adds an entry every time the timer freezes. There will be another setting to control the reset behavior of the quarterframe section timer; normally it clears the entries every time you restart the level, but should be toggleable so that it never resets.
Should also be able to toggle the quarterframe timer to hide it. 

The implementation of these features should not add more than 2KB total to the mod.

## Controller Display & Customized Display

These both have a toggle code in the settings and implement custom GUIs.

TODO

# Miscellaneous

- **Fix Manta Splitting**: This code fixes a bug in Nintendont for the manta episode. This should just be built into the mod and always on.
- **Force {ANSI,SJIS} Memory Card Encoding**: Ideally, the game could just autodetect memory card encoding and be compatible with both. Study these codes and determine, before implementing, if it is possible to implement this autodetection and inter-compatibility feature.