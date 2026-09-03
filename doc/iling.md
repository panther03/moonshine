# ILs

ILs is the Y+Start menu's level-select and PB system. Its catalog contains all
96 non-blue-coin Shines, 10 independent no-FLUDD secret-stage splits, 11
Any% Plaza segments, and three route-specific variants:

- 56 episode Shines
- 14 course bonus Shines
- 8 hundred-coin Shines
- 15 Delfino Plaza Shines
- 2 Airstrip Shines
- Corona Mountain

The 24 blue-coin-shop Shines are deliberately excluded.

`src/iling_entries.inc` remains the reviewable source of all 120 labels.
`ILing::label()` handles the first 90 regular-course labels (Bianco through
Pianta) without a stored pool. Ordinary names are derived procedurally into an
18-byte static buffer, while the three route variants use fixed names. The remaining 30
Airstrip, Corona, Delfino and Any% labels live in one ordered NUL pool. A
generated label is borrowed only until the next `ILing::label()` call.

The Any% group is the final menu group. Its rows are Bianco Plant, Delfino
Shadow Mario, Travel Skip, Gelato Plant, Pianta Enter, Honey Skip, Ricco Enter,
Bianco II Enter, Sirena Enter, Noki Enter and Corona Enter. PB recording,
popup, fanfare, profile, and Recent 5 controls live at the top of this tab.

## Timing and identity

A Shine PB ends at QFT's Shine-stop event: the first frame of the Shine Get
cutscene, after `TMario::winDemo` finishes its initial jump. It does not end on
raw `TShine::touchPlayer`. Corona Mountain ends at QFT's Bowser-stop event.
Any% portal splits end on QFT's loading-zone capture. The hook publishes the
target with the time, so the PB appears on the touch frame even when an entry
demo later replaces QFT's visual freeze. Honey Skip ends on the game-over flag
write, and Bianco Plant ends on the final damaging hit rather than the later
death animation.

Scenes cannot identify a result. An episode Shine, a hidden Shine and a
100-coin Shine can all exist in the same director, and Plaza has many Shines in
one scene. `onFireGetStar` therefore publishes `TShine::mMapObjID` (the decomp's
event id at `TShine+0x134`) before the Shine event serial. No Shine Get
Animation's cave publishes the same id. ILs accepts a result only when its
event id and its configured attempt origin both match.

QFT accumulates time through normal parent-to-secret and parent-to-boss loading
zones. ILs carries the attempt through those directors. The no-FLUDD secret
episodes have separate Full, Secret and Reds rows. QFT's attempt serial changes
on a real restart; resetting inside the child cancels the Full attempt and arms
that child as a fresh Secret or Reds attempt instead.

The ten standalone Secret entries latch their exact PB identity when armed.
They are intended to lose PB, Records and achievement credit after FLUDD use,
but V2.0.3 hardware testing shows that invalidation is not reliable. Do not
treat a saved Secret time as proof of a no-FLUDD run. Reds in the same child
scene and parent-to-secret Full runs are separate identities and remain
eligible.

Both QFT displays hold the captured split while entering a loading zone, but
the clock continues through the load. When a retail mission timer owns the
large Sunshine panel, Susamune leaves it untouched and keeps the full-level QFT
visible in the compact display. Ricco 2 Full accepts either retail race result
id from its parent-stage origin while the direct Race row keeps its own PB.
Gelato 8 also accepts a Gelato 1 origin for Gelato Beach Skip, and Bianco 2
accepts its Shine from a Bianco 1 origin. Bianco 2's segment route publishes
the accepted `02:00` to `37:00` Windmill transition as its FMV checkpoint and
carries that attempt into the Petey fight. Pinna 6 Full
starts inside the park; the separate Pinna Park EYG row starts on the Episode 3
beach and keeps its own PB.

Noki 3 starts directly inside the bottle. Noki 4 Eel Only and Gelato 4 Inside
are separate PB rows; Gelato Inside starts on the Sand Bird with Rocket.
Pinna 1 keeps its selected attempt through the double Exit Area cutscene skip.

## Starts and temporary progression

Ordinary entries use `LevelWarp::warpTo`. Any% entries use
`LevelWarp::warpFrom` so Plaza sees the configured previous scene and selects
the retail post-Shine return point, orientation and animation. In particular,
Noki to flooded Plaza uses the game's own falling Corona-facing return.

Some direct secret and Plaza entries need temporary progression flags. ILs
records the values it changes and applies them after the old director has
finished. Full and Secret keep the primary Shine clear through `setMario()` so
it removes FLUDD; Reds keeps it set through the same point. Full Reds keeps the
main Shine set for the whole parent stage because retail chooses replay-secret
mode only when Mario enters the portal. It restores the flag after the child
secret has loaded. Launching Full or Secret also selects the global No FLUDD
mode; launching Reds or Full Reds selects All secrets, and the menu saves that
choice normally. Other temporary flags are restored before playable frames.
This prevents a pause-menu or blue-coin card save from serialising
practice-only progression. Restoration is conflict-aware: a real gameplay
write that replaced a temporary value wins.

Plaza scenarios also snapshot the four packed story bits that Force Plaza
Events changes. Their route profile remains active only as long as the segment
needs it, while unrelated bits in the same byte are preserved. Pause, card-save,
wrong-exit and context-abort paths restore the original profile.

The route-specific 100-coin starts are:

| Course | Episode |
|---|---:|
| Bianco Hills | 6 |
| Ricco Harbor | 3 |
| Gelato Beach | 3 |
| Pinna Park | 8 |
| Sirena Beach | 7 |
| Noki Bay | 2 |
| Pianta Village | 5 |
| Delfino | Plaza |

## Persistence

PB slots are stable across catalog reordering: ordinary rows use retail Shine
event ids, slots 70-79 hold the ten independent Secret splits, and the Any%
rows use 80-85, 108-111 and 120. Pinna Park EYG uses slot 121 so it remains
independent from Pinna 6 (Full). These override slots are deliberately outside
the Shine rows exposed by ILs.

Noki 4 Eel Only and Gelato 4 Inside use slots 122 and 123 respectively. Noki 3
keeps its retail slot 52 despite its new bottle start.

Four PB-only profiles are available: fixed `Any%` and `120 Shines` banks, plus
two custom banks with 15-character user names. Switching profiles changes only
the PB bank; recording/popup/fanfare and Recent 5 settings remain global. The
legacy single bank migrates into `Any%`.

The Any% profile also computes a theoretical best from the agreed 55 route
slots. It stays `Incomplete` until every slot has a PB; the displayed sum is
computed live and is not separately persisted.

The PPC and ARM kernel exchange all four 128-slot banks, the active profile,
and the two custom names through `SusamuneILingProfilesCfg`. Its control,
acknowledgement, and payload cache lines are independent from settings saves.
The earlier `SusamuneILingPbCfg` mailbox stays in place solely so its offsets
remain compatible and its journal can be migrated.

The console build keeps its four live 128-value banks in the final 2 KiB of the
reserved config window. The ARM never touches that PPC-only mirror, so the
mailbox payload stays immutable while a save is in flight and later PB changes
can queue safely for the next write.

The kernel writes two fixed binary generations per game region on the device
that launched Susamune:

```text
/susamune_pbs_v2_<region>_a.bin
/susamune_pbs_v2_<region>_b.bin
```

Each file includes magic, version, region game id, generation and checksum. A
save overwrites only the inactive generation, so an interrupted write leaves
the previous valid profile set recoverable. PB files are intentionally separate from
`susamune.ini` and its text copy-through size ceiling. The format version is
also part of the filename so an older launcher cannot overwrite a newer
journal after a downgrade. On first profile-aware boot, a valid V1 journal is
copied into `Any%` and written to the V2 pair without deleting the V1 files.

Savestates keep enough attempt bookkeeping to restore temporary practice flags,
but loading one disarms PB recording for the rest of that attempt. A level
reset or fresh stage load arms PBs again. Savestates never rewind the PB list
or its save transaction.

## Console test matrix

Do not test all 96 entries for every change. Cover the distinct mechanisms:

1. A normal main-stage Shine after natural entry and level reset.
2. Bianco 3 from parent stage through its secret.
3. One Full/Secret/Reds trio, including a reset and FLUDD state. Record the
   known V2.0.3 failure if the standalone Secret PB remains eligible; Reds and
   Full must remain eligible.
4. Delfino Box Game 2 or another one-time Plaza object.
5. Airstrip 1; its first-visit scenario may need more progression flags.
6. Corona through the Bowser stop.
7. Bianco Plant's final hit and Honey Skip's death endpoint.
8. One Any% portal split, plus the Noki-to-Corona falling spawn.
9. Delete confirmation, fanfare Off, and PB survival across a reboot.
