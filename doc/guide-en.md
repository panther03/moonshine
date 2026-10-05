# Moonshine V2.3.3 Frame By Frame

User guide · English

Moonshine provides tools for studying movement, building TAS recordings and comparing attempts. Handmade level checkpoints measure up to eight timed segments per route, including the finish.

## Install and update

Copy the ZIP's `apps` folder to the SD card root. The launcher belongs in `apps/moonshine_launcher`. Replace the package files, and keep your existing themes, music, settings, records and ghosts. The launcher supports the existing JP, US and PAL disc revisions. Use your own disc or game image. Use the launcher and mod files from the same release. **Public V2.3.1/V2.3.2 states and TAS projects remain compatible with the existing region, episode and state checks. Older development builds still require their matching release.** Settings, records and ghosts remain usable.

The **standard download keeps every Moonshine menu in English**, including when you play JP. It does not change Sunshine's own language. The **Japanese download (日本語版)** uses Japanese launcher menus regardless of the selected game region, and translates Moonshine's in-game menus on JP; US/PAL in-game menus stay English. Some Japanese status messages and the built-in Guide body remain English. Use **System > Moonshine guide** for a short in-game reference.

Keep the supplied **language.txt** beside **boot.dol** when updating: the standard download contains `en`, the Japanese download `ja`. A missing or invalid file selects English. The Japanese download also needs its supplied **ja_ui.bin** for JP game text.

Open Moonshine Launcher from the Homebrew Channel. Choose the matching **Version**, then **Path** to select your ISO/CISO on SD or USB, or **Disc Drive** for a real disc. Choose **Launch Game**. If Auto Boot is enabled, hold B during startup to return to the launcher menu.

Choose **Guide** on the launcher's home screen to read the written guide on your TV. Select a topic with Up/Down and A; use Up/Down to scroll or Left/Right to move a page. B returns to the topics, then to the launcher. The guide is built into the launcher and works without a separate file.

Put **background.png** (1024×480 PNG, up to 2 MiB) and **bgm.mp3** (up to 4 MiB) in **/Moonshine data/theme** at the SD root. A launcher opened from USB uses that folder on USB. The launcher creates a missing folder once its storage device is ready; add your own background and music there. Existing files are preserved. The first launch also moves an existing root theme into this folder.

The launcher loads your theme before the kernel startup screens when its device is available. A USB device that cannot be opened that early is retried after normal storage initialization. Music starts after kernel setup. Startup and error text have explicit drawing state and outlines for dark themes. Startup checks your remembered Sunshine path. It mounts another device only when your selected version or Path needs it; storage messages remain visible during those waits.

Configuration and saved mod data belong to the device the launcher was opened from. For example, a launcher on SD still saves its configuration on SD when the game is on USB. Settings and binds are separate for JP, US and PAL.

## Your data folder

Moonshine keeps its files in **/Moonshine data** on the launcher's device.
The launcher creates the folders and moves existing Moonshine data there on
first launch. Keep your old files when updating; you do not need to rename them.
The app itself stays in `/apps/moonshine_launcher`.

| Location inside Moonshine data | Contents |
| --- | --- |
| `moonshine.ini` | Settings, layouts and button binds |
| `moonshine_*.bin` | Records, achievements and other practice journals |
| `theme/` | `background.png` and `bgm.mp3` |
| `ghosts/` | Your ghosts; incoming files go in `import/`, exports in `share/` |
| `states/` | Named SD savestates |
| `tas/` | Saved TAS projects |
| `layouts/` | Five named layout profiles for each game region |
| `crashes/` | Crash reports; keep each matching text/binary/core set together |
| `backups/` | Preserved files from migration or backup operations |

The Japanese download includes an optional flag at
`Moonshine data/theme/background.png`. Copy it only if you want that background;
keep your existing theme otherwise. The English download supplies no theme.

## Find the controls

Open the mod menu with your configured menu combo (default Y + Start). Use L/R for top-level tabs, the C-stick to move between rows, A to select, and B to go back.

- **Quick:** your Shined favourites. Newer named settings can now be Shined too, including free-camera Movement speed, Reverse sideways, Look sensitivity and Hide all HUD. Your existing stars stay.
- **Practice:** TAS projects with frame controls, Free camera, savestates, practice rules, RNG and gameplay options.
- **Runs:** ILs, playlists/streaks, records, PB Safety, and timer/split controls.
- **Records:** achievements and practice statistics, also reachable from Runs.
- **Ghosts:** race, watch, save and manage ghost tracks.
- **Display:** layout editors, HUD overlays, timer/split display and appearance.
- **System:** button binds and the built-in quick guide.

In **Runs > ILs**, press **Z** on a supported row to choose its starting episode. Use C-stick Up/Down, A to keep or B to cancel. The choice is shown beside its PB and saved separately for JP, US and PAL. This is available for the seven main-course 100-coin ILs, Gelato/Noki/Pianta Hidden, and all ten Full Reds ILs.

A fresh IL/streak attempt checks its own practice settings; a pause in the previous stage should not disqualify it. If a streak attempt is rejected, its counter briefly shows the reason.

**Runs > Timer and splits > Level splits** enables the checkpoint display. New routes include the handmade checkpoints for the remaining levels and Full Reds. A route can have up to eight segments, including its finish. Old attempt counts and records remain; individual segment times are carried forward only when their start and end checkpoints still match.

## Pause and advance

Open **Practice > TAS projects** and select **Pause / Resume** or **Advance one frame**. The selected action shows its shortcut; press **X** to change it there, or **Z** to clear it. System > Button binds also lists these controls.

| Default shortcut | Action |
|---|---|
| D-Down | Toggle practice pause; cancel a buffered pause |
| D-Up | Pause live gameplay, then advance one frame with each further press |
| Unassigned | Free camera and input recording/replay/stop |

These defaults apply to new configurations. Existing custom binds are preserved, including any earlier L-based combos. Z retains its existing function.

You can press Pause or Step during loading, the stage intro, or while Mario cannot be controlled. **Armed** means it will pause as soon as you can control Mario. You do not have to hold the shortcut. Press Pause again to cancel. Pressing Step again while waiting does not add extra steps.

Practice pause stops gameplay and the QFT. Each Step advances one normal game frame, and the QFT advances with it. Music and the game's background clocks keep running. A small **TAS** appears beside the QFT for an assisted attempt. These attempts cannot earn an ordinary PB; restart the stage to begin a fresh attempt.

The Sunshine timer and compact QFT now show the same frame during a practice hold or Step, including after a savestate load. They use different precision: compact `11.845` can appear as `11.85` on the Sunshine timer. Ordinary QFT timing calculations and event hooks have not changed.

With free camera **Off**, hold A and press Step to jump on that frame. You can also start Pause while holding A, and other gameplay buttons work alongside Pause and Step. To press A again on a later step, release it and press it again before stepping. Holding A continuously counts as keeping it held. The Pause or Step shortcut itself does not reach Mario, including any assigned L/R trigger until you release it. When choosing Step or Resume from the menu, release A to continue.

For spins, use the main stick yourself: choose the next direction before each Step. Free camera must be Off so the stick controls Mario.

While practice is paused, Advance takes priority over overlapping shortcuts. Hold A+B and tap Advance to jump-dive; only the Advance buttons are removed from Mario's input. Holding Advance does not repeat it. Outside practice pause, an exact longer reset shortcut still works normally without marking the attempt assisted.

## Free camera

Open **Practice > Free camera** and turn it On. This automatically pauses live gameplay. Move with the main stick, look with the C-stick, and use L/R analog pressure to descend/ascend. **Movement speed** saves a speed from 0.25x to 4x; hold X for a temporary boost. **Look sensitivity** separately sets C-stick turning speed from 0.25x to 4x.

Choose **Resume gameplay** on the same page to run the game at normal speed while keeping free camera On. **Pause gameplay** stops it again. The sticks control the camera in either mode. Both sticks follow every direction, with a gentle response near the centre and full speed at the edge.

**Camera smoothing** eases movement and turning when you press, change direction or release the sticks. Choose **0.1–1.5 seconds** in 0.1-second steps. Longer times give slower starts and stops. The default is **Off**, which responds and stops immediately.

**Hide all HUD** hides game and Moonshine overlays while free camera is On. You can still open the mod menu to change settings. Turn the option or free camera Off to show your normal overlays again.

If main-stick left/right feels backwards, enable **Reverse sideways** on the same page. It changes sideways movement only, leaving C-stick look unchanged; the default is Off. Recenter returns to the retail camera's view. Turning free camera Off restores that view and leaves gameplay paused or running as you had it.

Free camera also works in the ordinary Start pause. It remains usable while stepping, but **Camera On means Mario input Off**: A and the sticks will not control Mario on those steps. Turn it Off before stepping a jump or spin. The camera is temporary drawing state; it is restored before gameplay and savestate operations. It closes on a scene transition.

Practice pause, stepping and free camera also work in Ghost Watch and Watch2. The ghost playhead stays still while paused. You can keep free camera active when resuming Watch. B or Start exits Watch; opening the mod menu with its full combo leaves Watch active.

## Three savestates

Open **Practice > Savestates**. **Save to** chooses which memory slot your Save shortcut writes to. **Load from** independently chooses what your Load shortcut restores. Change either with A or C-stick left/right; choosing a slot does not save or load anything. For example, Save to State 1 and Load from State 2 lets you replace State 1 while continuing to practise from State 2.

Each of the three states shows Saved or Empty. **System > Button binds** has optional **Savestate: cycle save slot** and **Savestate: cycle load slot** shortcuts, both unassigned by default. The older **Savestate: cycle both slots** shortcut remains available for existing binds.

Hold your **Load** shortcut to keep gameplay still after the state restores. Release it when you are ready to move. If you were already using practice pause, releasing Load keeps that pause; use Resume or Step as usual.

If the saved state is in an intro, the intro finishes first. Keep Load held to stop on Mario's first controllable frame, or release it early to let play continue. A previous practice pause still takes effect when Mario becomes controllable.

The three states share **17.938 MiB** of compressed-state memory with this launcher. Their size depends on the scene and the length of any ghost recording included in the state. Nothing is deleted automatically. A replacement can reuse its old state's space once the new save is known to fit. If it cannot fit, all previous states remain, including the state you tried to replace. **Clear save slot** asks for confirmation before clearing the slot shown under Save to; the other states stay saved. Loading still requires the stage and episode where the state was made. Saving and loading can briefly stop the game while it processes the state. Successful save/load messages and savestate errors have separate visibility switches; System messages can also be disabled. If a slot refuses, report its exact error; there is no confirmed problem specific to Japanese State 3.

The three memory slots start empty after closing the game or rebooting. To keep a state, save a separate SD copy before closing the game.

## Keep a state on SD

1. Make a normal memory savestate and choose its slot under **Save to**.
2. Open **Practice > Savestates > SD states > Save memory state to SD**. Give the state a name, confirm it with Start, and wait until saving finishes. It creates a new `.mss` file in `/Moonshine data/states` on the launcher's device. X + Start cancels naming.
3. After rebooting, use the same mod build, game region and launcher setup, then enter the same level and episode. Secret areas also need the same parent episode.
4. Open **SD states > Refresh / first page** and highlight your file. Choose one of the actions below.

| Button on an SD file | Action |
|---|---|
| **Y — Load from** | Select this file for your ordinary Load shortcut. Close the menu and press Load to restore it. Your three memory slots stay intact. |
| **A — Import** | After confirmation, copy the file into the memory slot shown under **Save to**. **Load from** stays unchanged; select the imported slot there when you want to use it. |
| **Start — Rename** | Edit the file's display name. Start finishes; X + Start cancels. |
| **X — Delete** | Ask to delete this SD file. Cancelling keeps it; memory states are unaffected. |

Loading from SD reads the file each time, so it can take longer than a memory load. It can now load large files even when the three memory slots are full, provided one of those slots is a working state for the current scene and setup. That state provides a recovery point if an SD read fails. You do not need to select it as Load from. The three saved slots stay intact.

If a read fails after restoration has started, the game loads that recovery state and tells you which slot it used. If no suitable recovery state exists and temporary space is too small, loading is refused safely. Importing into a memory slot still requires enough room for the imported state.

The game also checks that its loaded resources match the saved state. A matching level name alone may not be enough. Unsupported setups, incompatible files and damaged files are refused before replacing a memory slot. SD states are specific to their build and game setup; they are not cross-region sharing files like ghosts.

Keep the storage device connected until the transfer or its cancellation finishes. A tester has confirmed SD-state restoration after a real Wii reboot on the previous build. Full ghost recording after frame advance, timer alignment and colour persistence were also confirmed. The new file controls still need feedback across more scenes and setups; use the latest section of TESTING.md.

## Record, edit and keep a TAS

Open **Practice > TAS projects**. A TAS keeps Mario's recorded inputs, its beginning and up to two checkpoints together.

1. Choose **New TAS** where you want the recording to begin. Moonshine saves the beginning automatically, including the RNG. Release the A used to confirm. Gameplay stays paused until you use Step or Resume.
2. Hold Mario's buttons and press **Step** to record one frame, or use **Resume** to record normal play. Opening the menu pauses your work.
3. Under **Checkpoints**, choose **Save Checkpoint 1** before a move you might want to redo. Play farther, then choose **Go to Checkpoint 1** to return there. Use Step or Resume to record a new continuation. Checkpoint 2 gives you another place to retry. The Beginning is kept for replay.
4. Choose **Replay** to watch your recorded inputs from the beginning. B or Start stops playback.
5. Choose **Save TAS**, enter a name and confirm with Start. It saves the entire recording, its Beginning and any checkpoints you already made. It does not create a new checkpoint or need another empty slot. Later saves update that same TAS. Wait for the saved message before closing the game.
6. To return later, use the same build, game region and setup, then choose **Open TAS** and its name. If its selected checkpoint or Beginning can load in your current area and episode, it opens there paused and keeps the later recorded inputs for Replay. Otherwise, the recording opens without moving Mario: you can save it again, or enter its Beginning area before Replay.

**Go to Beginning, Go to Checkpoint and Open TAS leave you paused and ready to edit.** The later inputs remain available for Replay until your first new Step or Resume records a replacement continuation. You do not have to select Continue first. Save TAS keeps whichever recording is currently in memory; saving or opening alone does not shorten it. The SD copy changes only when you save.

**Continue stays paused** so you can arrange the next input before Step or Resume. After explicitly stopping the take or finishing Replay, choose Continue when you want to record again; ordinary Steps do not silently edit a stopped take.

Saving over an existing checkpoint asks before replacing it, whether you use the menu or a shortcut. A confirms; B keeps it. **Save TAS** still updates the named SD project directly.

Select Continue, Replay, Beginning or a checkpoint action and press **X** to assign its shortcut; **Z** clears it. These seven TAS shortcuts start unassigned. The name at the top shows the recorded frame count and limit, for example **200/4096**.

In **Open TAS**, highlight a name and press **Start** to rename it or **X** to delete its SD copy, with confirmation. Deleting the SD copy keeps the checkpoints currently in memory; save again if you want to keep that work after closing the game.

TAS projects use the same three memory slots as ordinary savestates. A new TAS uses an empty slot when possible. If it needs an occupied ordinary slot, it asks which state you want to replace; cancel to keep it. Its Beginning and two checkpoints have clear names in the TAS screen, so you do not need to manage their slot numbers or import separate files. **Save Checkpoint** keeps a retry point in memory; **Save TAS** keeps the whole project on SD, under `/Moonshine data/tas`. Only the SD save survives closing the game or rebooting. The ordinary SD states menu remains separate.

Normal area changes, such as entering a secret, keep the recording. Inputs during intros, fades, conversations and movies are recorded too, including skip presses and FLUDD's movie sequence. Loading waits use no input frames, but ready intro and movie frames count toward the **4096-frame limit**. Up to **32 area/movie transitions** fit in a take. If a transition is unsupported or a limit is reached, recording stops and the take is kept so you can save it.

Intros and movies run in real time. Press Pause or Step during one to stop when Mario becomes controllable; the movie itself is not frame-advanced.

Your usual Close shortcut works inside Checkpoints and the other TAS submenus. While recording a new shortcut, those buttons belong to the recorder. Finish or cancel that edit, release the buttons, then press Close again.

A checkpoint marked **Other area** is still saved. Enter its matching area and episode before loading it. For **Replay** or **Go to Beginning**, return to the area and episode where the TAS began; Moonshine does not take you there automatically.

Replay is experimental. If the game first differs from the recording, **DESYNC fN** shows that frame and playback continues with the remaining inputs. The warning means the result may no longer match the original; it does not repair the replay. B or Start still stops playback. Damaged input files, incompatible gameplay settings and invalid area/state loads remain protected. Timer layout, metadata, ghost inputs and camera presentation can still be adjusted. TAS projects cannot be shared across regions or builds. Use an exported ghost to share a finished attempt.

**TAS banner** starts On. Find it in **Practice > TAS projects** or **Display > Other HUD**, or Shine it to Quick. Off hides the large progress and help banner. A small DESYNC warning remains during a mismatched replay even with the banner Off. **f0** means the starting state already differed.

If Replay reaches the expected loading zone early or late, it warns and keeps the remaining inputs in order. Checkpoint editing can be temporarily unavailable while the current area no longer matches that point in the recording. **Save TAS still keeps the full take**; return to an existing compatible checkpoint to edit there. An unexpected destination is still refused.

## Ghost inputs and splits

Personal ghosts and imports now use paged lists with no 45/12-entry or ten-hour library cap. Select **Personal page** or **Imported page** and use C-stick left/right to change pages; A opens the next page. Choose **Save latest ghost** to create a new personal file. Selecting an empty personal ghost row also offers **Save latest ghost**, with confirmation. Existing ghosts stay available, and Watch2 selections remain attached to their files when you browse another page.

Available storage limits the library. New ghosts record up to 10 minutes and include FLUDD, nozzle and spray observations. Older ghosts, including recordings longer than 10 minutes, remain readable; they do not gain spray data that was never recorded. Imported files stay in the import folder; sharing and deleting still act on the file you selected.

New ghost files can include the inputs actually consumed during the attempt, plus timestamps for existing supported split endpoints. Earlier pose-only ghost files remain readable and show unavailable input data honestly.

In **Ghosts > Ghost inputs**, choose Off, Ghost, or **Both ghosts**. The same control is available in Display > Layout editor > Controller inputs and Display > HUD and displays > Other HUD. Both ghosts shows both ghost controllers in Watch2, or the live and ghost controller while racing. Turning Ghost display Off also hides ghost inputs. Turning it back On restores your chosen input display; your separate controller overlay stays independent. Ghost input is a teaching overlay; imported tracks do not control Mario.

Ghost recordings made with practice pause, free camera or stepping are marked **TAS**. Their playback omits paused time, so arranging a camera or planning the next input does not create a long pause in the saved ghost. The QFT also stops during practice pause. TAS ghosts are for practice and cannot earn an ordinary PB.

Ghost Watch skips movies automatically and keeps the ghost's movement timing. Watch remains active when gameplay returns; Pause, Step and free camera are still available there.

Saving a state during ghost recording now includes the recording from the level's start to that moment. Loading restores that opening and replaces everything recorded after it with your new continuation. Finish, save and export the resulting full-level TAS ghost as usual. Loading a state made without an active recording does not invent an opening or start a new ghost automatically. TAS projects save editable inputs; ghosts save the finished attempt for watching, racing and sharing.

In Display > Timer and splits > Timer and splits, choose the comparison: **Off → PB → SOB → Ghost**. SOB means the cumulative sum of your best recorded segments. Ghost uses the selected race target's compatible split timestamps. Missing or incompatible timestamps show `--`; no checkpoint timing is guessed.

These controls are also under Runs > Timer and splits. **Level splits** is the overlay toggle on the first page.

Exported shareable ghosts live under `Moonshine data/ghosts/share/` on the launcher's device. Put incoming `.smsghost` files in `Moonshine data/ghosts/import/`, then import them from Ghosts. Keep internal `.sgh` files in their existing folders. The included Full Reds ILs remain available through Runs: choose the full-level route when you want the approach and secret reds timed together.

## Layout and colours

**Display > Layout profiles** saves five named layouts for each game region. Press **Y** to save the current layout, enter a name and press **Start**. While typing, the keyboard owns the buttons even if they are also bound to Close or Pause; **X+Start** cancels. Replacing a saved profile asks first. Press **A** to apply a saved profile. This includes overlays, custom text, timer and menu styles, Mario/FLUDD colours and visual options; binds, practice rules and records stay separate. Profiles survive reboot on the launcher's storage device or Dolphin's Slot B memory card. Applying a profile also saves it as your current layout.

Display > Layout editor has separate groups for Timers, Controller inputs, Metadata, Native HUD colours, Custom text, Practice feedback, and Menu and notifications. Rollout and dust editors are also beside their settings in HUD and displays > Movement displays.

**Timers > Sunshine timer** opens the full editor: position, size, opacity, brightness, all 13 characters, TIME/TEMPO and the streak. Its position range spans the full screen.

The first option, **Appearance**, lets you choose **Original** or **Custom**. Leave the target on All to change the whole timer, or press Start to choose one character or image. Original keeps the game's shading and lets you tint it; Custom uses your chosen colours more directly. Colour editing keeps the appearance mode you selected. To restore the whole timer's normal colours, choose **All > Appearance > Original**, then reset Hue, Saturation and Lightness individually with **Z** and confirmation. Position, size and the other style controls stay as they were.

The shared colour editor uses **Hue, Saturation and Lightness (HSL)**. Hue chooses the colour around a 0–359 degree wheel. Saturation runs from grey at 0% to full colour at 100%; Lightness runs from black at 0% to white at 100%. Your existing colours are kept when you update. Hold **Y** while adjusting with the C-stick for increments of 1 instead of 4. A keeps edits, B discards, and Z resets the selected option, with confirmation.

**Native HUD colours** includes separate Health counter colour and Underwater air colour controls. Reset restores the retail colours.

**Display > Appearance > Mario appearance** contains **Mario colours** and **FLUDD colours**, both using the same Creation editor. Press Start to select All or one part. Choose Original/Custom independently for each part; changing HSL selects Custom. Original keeps the stored custom colour for later. Keep/Discard/Reset and Y for one-unit HSL adjustments work here too.

| Editor | Parts |
|---|---|
| Mario colours — 7 | Cap, shirt, overalls, gloves, shoes, sunglasses, Sunshine shirt |
| FLUDD colours — 10 | Body paint, metal, straps, tank, spray nozzle, hover nozzle, rocket nozzle, turbo nozzle, sprayed water, water highlights |

Mario's skin stays unchanged. Magenta on the cap and shirt no longer produces red specks when texture colours round to the same value. Sprayed water covers the stream, outlet mist and impact splashes; sea water and Yoshi juice keep their normal colours. Keep the edits to save the colours and each part's Original/Custom choice for the next boot.

**Metadata** adds Field gap, Row gap, Fields per row and Value widths. C-stick left/right decreases or increases these values. Choose horizontal layout to arrange several fields per row; Fields per row limits the number before wrapping. Auto wraps at the screen edge. Stable widths keep values aligned as digits change; Compact reduces empty space. Per-character colours remain attached to their original fields.

## Movement timing displays

Open **Display > HUD and displays > Movement displays** to enable **Jump display** or **GB skip timing**. Use the Edit rows on that page, or **Layout editor > Practice feedback**, to change GB timing, Jump timing and Buttslide separately. Each has its own position, size, opacity, background and colours.

**Jump display** measures game frames from your most recent landing to your next jump. It shows **1f–6f**, then **Late**, alongside the takeoff's quarter-frame phase. `1f qf2` means you jumped one rendered game frame after landing, on quarter-frame phase 2. Paused frames do not count, and changing areas or loading a state clears the result.

**Jump display** and **Buttslide display** have separate On/Off toggles and can be shown together. Buttslide shows a compact **Ready / Waiting** cue during your slide, using the current surface and slide state. It also works while frame advancing. It does not press or queue A for you.

**GB skip timing** checks the B press after an ordinary full A jump for the no-hover GB skip. The supplied target is 9 airborne frames with Y approximately 404 and vertical speed approximately 6. It displays Early, On time, Late or Check jump with those values. It does not check X/Z or confirm that Mario clipped through.

In **Fast Any%**, Pinna routes start on the beach and Noki 3 starts outside the bottle. Dying in a secret and then finishing still advances the loader. That retry does not count as a clean full-level PB or playlist best. Ordinary standalone IL starts are unchanged.

## Dolphin

Use the matching regional BPS patch with a clean ISO; the Wii launcher ZIP is for the Homebrew Channel. All standard Dolphin patches keep Moonshine menus English, including JP. Choose the separate Japanese JP patch for translated JP menus; its font and text are included in the patch. Sunshine's own language is unchanged. For mod settings persistence, enable a memory card in slot B. Set Texture Cache Accuracy to Safe so savestate loads restore goop correctly. Keep ordinary Sunshine saves and Moonshine's slot-B settings file when updating.

Use a current Dolphin release for frame tools. Dolphin 5.0 JIT can leave paused-menu input stuck; use a newer release for these controls.

The SD states menu requires Moonshine Launcher's storage service; standalone Dolphin BPS builds do not provide it. Their three memory slots still work. The BPS patch now has a 640 KiB disc storage extent; this does not increase the game's MEM1 reservation.

## Reports and limitations

Include the game region, scene, settings, reproduction steps and displayed build checksum when reporting problems. Crash history is stored in `/Moonshine data/crashes`, with up to 16 recent reports. Keep the matching text, `.bin` and `.core` files from the same report together; older reports are preserved during migration. The optional `tools/decode_crash.py` script reads the binary reports with Python 3. Crash reporting attempts to preserve a minimal record even if a larger report cannot be completed; storage is still required for a file to survive shutdown.

The mod reserves 768 KiB of MEM1, 256 KiB more than V2.2. Fixed timer scratch, attachment heap and savestate ownership remain separate. This reduces the game heap's capacity by 256 KiB; the remaining free space depends on the scene and needs console measurement.
