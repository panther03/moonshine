# Moonshine V2.3.2 Frame By Frame

## Fast Any% stage loader

- Pinna routes in Fast Any% now begin on the beach.
- Noki 3 in Fast Any% now begins outside the bottle.
- After dying in a secret, finishing it still advances Fast Any% to the next level. A retry does not earn a clean full-level PB or playlist best.
- Standalone IL starting points and records stay separate from these Fast Any% changes. The revised Fast Any% route starts a new playlist-best comparison.

## Movement timing

- **Jump display** shows **1f–6f**, then **Late**, from your most recent landing to your next jump. The takeoff's quarter-frame phase stays alongside it: for example, `1f qf2`.
- Turn on **Buttslide display** to see a compact **Ready / Waiting** cue. **Jump display** has its own On/Off toggle; both can stay visible together. The cue follows the game's jump rules, including the surface and slide state.
- **GB skip timing** checks the B press after an ordinary full A jump. It reports Early, On time, Late or Check jump, with the airborne frame count, height and vertical speed. The supplied target is 9 frames, Y404 and vertical speed 6. It does not check horizontal position or prove that the skip succeeded.
- All three displays are optional, under **Display > HUD and displays > Movement displays**. Their Edit rows, also in Layout editor > Practice feedback, give GB, Jump and Buttslide independent position, size, opacity, background and colour controls.

## Five named layout profiles

- **Display > Layout profiles** stores five layouts for each game region. Give each one a name, press **Y** to save the current layout, or **A** to apply a saved profile. Replacing a saved profile asks first. Name entry keeps D-pad and typing buttons separate from Menu Close and practice shortcuts; Start saves and X+Start cancels.
- Profiles include your overlays, custom text, timer and menu styles, Mario/FLUDD colours and visual options. Button binds, practice rules and records are separate.
- Launcher profiles survive reboot in **Moonshine data/layouts**. Dolphin stores them on its Slot B memory card alongside the existing settings system.

## Memory and compatibility

- Moved about 14 KB of live split statistics from MEM1 into unused space in an existing allocation. The game's memory reservation, savestate pool and ghost capacity are unchanged.
- Use the launcher and mod files from this build together. Settings, records and ghosts remain usable. This compatibility update also accepts SD states and TAS projects from the public V2.3.1 and V2.3.2 releases, in their original game region and episode. Saving an older TAS creates an updated copy and keeps the original. Older development builds still require their matching release.
- The English-menu launcher supports US, PAL and JP. The Japanese-menu download remains separate; it translates the launcher and JP mod menus without changing Sunshine's own language.
- The Japanese download uses the runner-supplied translations for 52 achievement names. Achievement requirements are unchanged.
