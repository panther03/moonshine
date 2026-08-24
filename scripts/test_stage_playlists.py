#!/usr/bin/env python3
"""Host contract tests for Stage Loader playlist journals."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include" / "susamune" / "susamune_cfg.h"
KERNEL = ROOT / "launcher" / "kernel" / "SusamuneCfg.c"
STAGE_LOADER = ROOT / "src" / "stage_loader.cpp"
ILING = ROOT / "src" / "iling.cpp"
WARP_WHEEL = ROOT / "src" / "warp_wheel.cpp"
MENU = ROOT / "src" / "menu.cpp"
SETTINGS = ROOT / "include" / "susamune" / "settings_list.h"
SETTING_DESCS = ROOT / "src" / "settings_descs.inc"

MAGIC = 0x53504C46
VERSION_V1 = 1
VERSION = 2
SLOTS = 7
BUILTINS = 3
TOTAL = BUILTINS + SLOTS
REGIONS = 3
CAPACITY = 120
ROUTES = 122
ACTION_BYTES = CAPACITY // 8
ACTION_SCHEMA = 1
ACTION_ROUTES = frozenset((0, 25, 82))
BEST_UNSET = 0xFFFFFFFF
V1_SIZE = 896
V2_SIZE = 1184

V1_COUNTS_OFFSET = 32
V1_ENTRIES_OFFSET = 39
V2_COUNTS_OFFSET = 32
V2_ENTRIES_OFFSET = 39
V2_ACTIONS_OFFSET = 879
V2_REVISIONS_OFFSET = 984
V2_HASHES_OFFSET = 1012
V2_BESTS_OFFSET = 1052

_V1_HEADER = struct.Struct(">IHBBII16s")
_V2_HEADER = struct.Struct(">IHBBIIBBBB12s")


def hash_word(value: int, word: int) -> int:
    return ((value ^ word) * 16777619) & 0xFFFFFFFF


def hash_byte(value: int, byte: int) -> int:
    return ((value ^ byte) * 16777619) & 0xFFFFFFFF


def generation_is_newer(candidate: int, current: int) -> bool:
    delta = (candidate - current) & 0xFFFFFFFF
    return delta != 0 and delta < 0x80000000


def action_at(actions: bytes | bytearray, position: int) -> bool:
    return bool(actions[position >> 3] & (1 << (position & 7)))


def set_action(actions: bytearray, position: int) -> None:
    actions[position >> 3] |= 1 << (position & 7)


def content_hash(
    playlist_id: int,
    revision: int,
    count: int,
    entries: bytes | bytearray,
    actions: bytes | bytearray,
) -> int:
    value = 2166136261
    value = hash_word(
        value,
        (ACTION_SCHEMA << 24) | ((playlist_id & 0xFF) << 16) | count,
    )
    value = hash_word(value, revision)
    for position in range(CAPACITY):
        value = hash_byte(value, entries[position])
        value = hash_byte(value, int(action_at(actions, position)))
    return value


def v1_checksum(generation: int, counts: bytes, entries: bytes) -> int:
    value = 2166136261
    value = hash_word(value, (VERSION_V1 << 16) | (SLOTS << 8) | CAPACITY)
    value = hash_word(value, generation)
    for slot in range(SLOTS):
        value = hash_byte(value, counts[slot])
        start = slot * CAPACITY
        for entry in entries[start:start + CAPACITY]:
            value = hash_byte(value, entry)
    return value


def v2_checksum(
    generation: int,
    counts: bytes,
    entries: bytes,
    actions: bytes,
    revisions: tuple[int, ...],
    hashes: tuple[int, ...],
    bests: tuple[tuple[int, ...], ...],
) -> int:
    value = 2166136261
    value = hash_word(value, (VERSION << 16) | (SLOTS << 8) | CAPACITY)
    value = hash_word(value, generation)
    value = hash_word(
        value,
        (BUILTINS << 24)
        | (REGIONS << 16)
        | (ACTION_BYTES << 8)
        | ACTION_SCHEMA,
    )
    for slot in range(SLOTS):
        value = hash_byte(value, counts[slot])
        entry_start = slot * CAPACITY
        for entry in entries[entry_start:entry_start + CAPACITY]:
            value = hash_byte(value, entry)
        action_start = slot * ACTION_BYTES
        for action in actions[action_start:action_start + ACTION_BYTES]:
            value = hash_byte(value, action)
        value = hash_word(value, revisions[slot])
    for digest in hashes:
        value = hash_word(value, digest)
    for region in bests:
        for best in region:
            value = hash_word(value, best)
    return value


def pack_routes(playlists: list[list[int]]) -> tuple[bytes, bytes]:
    if len(playlists) != SLOTS:
        raise ValueError("wrong playlist count")
    counts = bytearray(SLOTS)
    entries = bytearray(SLOTS * CAPACITY)
    for slot, playlist in enumerate(playlists):
        if len(playlist) > CAPACITY:
            raise ValueError("playlist too long")
        counts[slot] = len(playlist)
        start = slot * CAPACITY
        entries[start:start + len(playlist)] = bytes(playlist)
    return bytes(counts), bytes(entries)


def build_v1(
    playlists: list[list[int]], *, generation: int = 1,
    version: int = VERSION_V1,
) -> bytes:
    counts, entries = pack_routes(playlists)
    digest = v1_checksum(generation, counts, entries)
    raw = _V1_HEADER.pack(
        MAGIC, version, SLOTS, CAPACITY, generation, digest, bytes(16)
    ) + counts + entries + bytes(17)
    if len(raw) != V1_SIZE:
        raise AssertionError("V1 playlist layout changed")
    return raw


@dataclass(frozen=True)
class V2Payload:
    counts: bytes
    entries: bytes
    actions: bytes
    revisions: tuple[int, ...]
    hashes: tuple[int, ...]
    bests: tuple[tuple[int, ...], ...]


def make_v2_payload(
    playlists: list[list[int]],
    *,
    action_positions: dict[int, tuple[int, ...]] | None = None,
    revisions: tuple[int, ...] | None = None,
    builtin_hashes: tuple[int, ...] | None = None,
    bests: tuple[tuple[int, ...], ...] | None = None,
) -> V2Payload:
    counts, entries = pack_routes(playlists)
    action_data = bytearray(SLOTS * ACTION_BYTES)
    for slot, positions in (action_positions or {}).items():
        for position in positions:
            offset = slot * ACTION_BYTES
            view = action_data[offset:offset + ACTION_BYTES]
            set_action(view, position)
            action_data[offset:offset + ACTION_BYTES] = view
    revision_values = revisions or (0,) * SLOTS
    if len(revision_values) != SLOTS:
        raise ValueError("wrong revision count")
    hash_values = list(builtin_hashes or (0,) * BUILTINS)
    if len(hash_values) != BUILTINS:
        raise ValueError("wrong built-in hash count")
    for slot in range(SLOTS):
        entry_start = slot * CAPACITY
        action_start = slot * ACTION_BYTES
        hash_values.append(
            content_hash(
                BUILTINS + slot,
                revision_values[slot],
                counts[slot],
                entries[entry_start:entry_start + CAPACITY],
                action_data[action_start:action_start + ACTION_BYTES],
            )
        )
    best_values = bests or tuple(
        tuple(BEST_UNSET for _ in range(TOTAL)) for _ in range(REGIONS)
    )
    if len(best_values) != REGIONS or any(
        len(region) != TOTAL for region in best_values
    ):
        raise ValueError("wrong best-time dimensions")
    return V2Payload(
        counts,
        entries,
        bytes(action_data),
        tuple(revision_values),
        tuple(hash_values),
        tuple(tuple(region) for region in best_values),
    )


def build_v2(
    payload: V2Payload,
    *,
    generation: int = 1,
    version: int = VERSION,
    metadata: tuple[int, int, int, int] = (
        BUILTINS, REGIONS, ACTION_BYTES, ACTION_SCHEMA
    ),
) -> bytes:
    digest = v2_checksum(
        generation,
        payload.counts,
        payload.entries,
        payload.actions,
        payload.revisions,
        payload.hashes,
        payload.bests,
    )
    raw = b"".join((
        _V2_HEADER.pack(
            MAGIC,
            version,
            SLOTS,
            CAPACITY,
            generation,
            digest,
            *metadata,
            bytes(12),
        ),
        payload.counts,
        payload.entries,
        payload.actions,
        struct.pack(">7I", *payload.revisions),
        struct.pack(">10I", *payload.hashes),
        struct.pack(">30I", *(best for region in payload.bests for best in region)),
        bytes(12),
    ))
    if len(raw) != V2_SIZE:
        raise AssertionError("V2 playlist layout changed")
    return raw


def parse_v2(raw: bytes) -> tuple[int, V2Payload] | None:
    if len(raw) != V2_SIZE:
        return None
    (
        magic,
        version,
        slots,
        capacity,
        generation,
        digest,
        builtins,
        regions,
        action_bytes,
        action_schema,
        reserved0,
    ) = _V2_HEADER.unpack_from(raw)
    if (magic, version, slots, capacity) != (MAGIC, VERSION, SLOTS, CAPACITY):
        return None
    if (builtins, regions, action_bytes, action_schema) != (
        BUILTINS, REGIONS, ACTION_BYTES, ACTION_SCHEMA
    ):
        return None
    if any(reserved0) or any(raw[-12:]):
        return None
    counts = raw[V2_COUNTS_OFFSET:V2_ENTRIES_OFFSET]
    entries = raw[V2_ENTRIES_OFFSET:V2_ACTIONS_OFFSET]
    actions = raw[V2_ACTIONS_OFFSET:V2_REVISIONS_OFFSET]
    revisions = struct.unpack_from(">7I", raw, V2_REVISIONS_OFFSET)
    hashes = struct.unpack_from(">10I", raw, V2_HASHES_OFFSET)
    flat_bests = struct.unpack_from(">30I", raw, V2_BESTS_OFFSET)
    bests = tuple(
        tuple(flat_bests[region * TOTAL:(region + 1) * TOTAL])
        for region in range(REGIONS)
    )
    payload = V2Payload(counts, entries, actions, revisions, hashes, bests)
    if digest != v2_checksum(
        generation, counts, entries, actions, revisions, hashes, bests
    ):
        return None
    for slot, count in enumerate(counts):
        if count > CAPACITY:
            return None
        entry_start = slot * CAPACITY
        action_start = slot * ACTION_BYTES
        slot_entries = entries[entry_start:entry_start + CAPACITY]
        slot_actions = actions[action_start:action_start + ACTION_BYTES]
        for position in range(count):
            if slot_entries[position] >= ROUTES:
                return None
            if action_at(slot_actions, position) and (
                slot_entries[position] not in ACTION_ROUTES
            ):
                return None
        if any(slot_entries[count:]):
            return None
        if any(action_at(slot_actions, position) for position in range(count, CAPACITY)):
            return None
        if hashes[BUILTINS + slot] != content_hash(
            BUILTINS + slot,
            revisions[slot],
            count,
            slot_entries,
            slot_actions,
        ):
            return None
    for region in range(REGIONS):
        for playlist_id in range(TOTAL):
            best = bests[region][playlist_id]
            if best != BEST_UNSET and best == 0:
                return None
            if (
                playlist_id >= BUILTINS
                and counts[playlist_id - BUILTINS] == 0
                and best != BEST_UNSET
            ):
                return None
    return generation, payload


def parse_v1(raw: bytes) -> tuple[int, bytes, bytes] | None:
    if len(raw) != V1_SIZE:
        return None
    magic, version, slots, capacity, generation, digest, reserved0 = (
        _V1_HEADER.unpack_from(raw)
    )
    if (magic, version, slots, capacity) != (
        MAGIC, VERSION_V1, SLOTS, CAPACITY
    ):
        return None
    if any(reserved0) or any(raw[-17:]):
        return None
    counts = raw[V1_COUNTS_OFFSET:V1_ENTRIES_OFFSET]
    entries = raw[V1_ENTRIES_OFFSET:V1_ENTRIES_OFFSET + SLOTS * CAPACITY]
    if digest != v1_checksum(generation, counts, entries):
        return None
    for slot, count in enumerate(counts):
        if count > CAPACITY:
            return None
        start = slot * CAPACITY
        if any(entry >= ROUTES for entry in entries[start:start + count]):
            return None
        if any(entries[start + count:start + CAPACITY]):
            return None
    return generation, counts, entries


def import_newest_v1(candidates: tuple[bytes | None, bytes | None]) -> V2Payload | None:
    newest: tuple[int, bytes, bytes] | None = None
    for raw in candidates:
        parsed = parse_v1(raw) if raw is not None else None
        if parsed is None:
            continue
        if newest is None or generation_is_newer(parsed[0], newest[0]):
            newest = parsed
    if newest is None:
        return None
    _, counts, entries = newest
    playlists = [
        list(entries[slot * CAPACITY:slot * CAPACITY + counts[slot]])
        for slot in range(SLOTS)
    ]
    return make_v2_payload(playlists)


def identity_change_valid(current: V2Payload, staged: V2Payload) -> bool:
    for playlist_id in range(TOTAL):
        changed = current.hashes[playlist_id] != staged.hashes[playlist_id]
        if playlist_id >= BUILTINS:
            slot = playlist_id - BUILTINS
            changed = changed or (
                current.revisions[slot] != staged.revisions[slot]
            )
        had_best = any(
            current.bests[region][playlist_id] != BEST_UNSET
            for region in range(REGIONS)
        )
        if changed and had_best and any(
            staged.bests[region][playlist_id] != BEST_UNSET
            for region in range(REGIONS)
        ):
            return False
    return True


class PlaylistFormatTests(unittest.TestCase):
    def test_streak_auto_reset_off_waits_for_success_and_target_miss(self) -> None:
        stage_loader = STAGE_LOADER.read_text(encoding="utf-8")
        wheel = WARP_WHEEL.read_text(encoding="utf-8")
        menu = MENU.read_text(encoding="utf-8")
        settings = SETTINGS.read_text(encoding="utf-8")
        descs = SETTING_DESCS.read_text(encoding="utf-8")

        self.assertIn(
            'X(SETTING_STREAK_AUTO_RESET,            "streak_auto_reset")',
            settings,
        )
        self.assertIn(
            'SBOOL("Streak auto-reset", 1, SETTING_CAT_CUSTOM)', descs
        )
        success = stage_loader[
            stage_loader.index("void queueSuccess"):
            stage_loader.index("u16 modalHeld()")
        ]
        self.assertRegex(
            success,
            r"normalShine\s*&&\s*\n\s*!gSettings\.getBool\("
            r"SETTING_STREAK_AUTO_RESET\)",
        )
        self.assertIn("sRuntime.state = STATE_RETRY_SAVEBOX;", success)
        failure = stage_loader[
            stage_loader.index("void queueFailure"):
            stage_loader.index("void finishPlaylistBest")
        ]
        self.assertIn("outcome == OUTCOME_TARGET_MISS", failure)
        self.assertIn("gpMarioOriginal->mState == kMarioWinDemoState", failure)
        self.assertIn(
            "!gSettings.getBool(SETTING_STREAK_AUTO_RESET)", failure
        )
        self.assertIn(
            "waitForSavebox ? STATE_RETRY_SAVEBOX : STATE_RETRY_DELAY",
            failure,
        )
        post_save = stage_loader[
            stage_loader.index("bool holdPostSaveDeparture()"):
            stage_loader.index("bool copyDeathRetryDest")
        ]
        self.assertIn("requestCurrentPostSave()", post_save)
        self.assertIn("STATE_WAITING_POST_SAVE", post_save)
        on_directed = wheel[wheel.index("s32 onDirected(s32 appState)") :]
        on_directed = on_directed[: on_directed.index("namespace WarpWheel")]
        self.assertRegex(
            on_directed,
            r"appState > TApplication::CONTEXT_DIRECT_MAIN_LOOP\s*&&\s*"
            r"StageLoader::holdPostSaveDeparture\(\)",
        )
        self.assertRegex(
            on_directed,
            r"(?s)if \(sCourseGuard\).*?if \(holdPostSave\).*?"
            r"prepareArmedDeparture\(\)",
        )
        self.assertIn("if (holdPostSave) return 0;", on_directed)
        self.assertIn('name = "Streak auto-reset";', menu)
        self.assertIn("gSettings.cycle(SETTING_STREAK_AUTO_RESET, 1);", menu)
        self.assertIn(
            "return mStreaking ? OPTION_BUILTIN + 1 : OPTION_AUTO_RESET;",
            menu,
        )
        self.assertRegex(
            menu,
            r"mStreaking\s*&&\s*row == OPTION_BUILTIN\s*\?\s*"
            r"OPTION_AUTO_RESET",
        )
        update = stage_loader[
            stage_loader.index("void update()"):
            stage_loader.index("void onILAttemptStarted")
        ]
        self.assertRegex(
            update,
            r"(?s)SETTING_DISABLE_WARPS\).*?STATE_RETRY_SAVEBOX.*?"
            r"STATE_WAITING_POST_SAVE",
        )
        self.assertIn("sRuntime.state = STATE_BLOCKED;", update)

    def test_final_modal_waits_for_shine_tail_and_pinna_exit_is_preserved(self) -> None:
        stage_loader = STAGE_LOADER.read_text(encoding="utf-8")
        readiness = stage_loader[
            stage_loader.index("bool resultPresentationReady"):
            stage_loader.index("bool requestCurrent()")
        ]
        self.assertIn("liveResultDirector(director)", readiness)
        self.assertIn("sRuntime.modalWaitForShineDemo", readiness)
        self.assertIn("(director->mGameState & 0x1) == 0", readiness)
        self.assertIn("director->mDemoState != 0", readiness)
        self.assertIn("return false;", readiness)
        self.assertEqual(stage_loader.count("resultPresentationReady("), 3)
        queue_success = stage_loader[
            stage_loader.index("void queueSuccess"):
            stage_loader.index("u16 modalHeld()")
        ]
        self.assertLess(
            queue_success.index("const bool normalShine"),
            queue_success.index("clearShinePublishLatch()"),
        )
        self.assertEqual(
            queue_success.count("sRuntime.modalWaitForShineDemo = normalShine;"),
            2,
        )
        result_input = stage_loader[
            stage_loader.index("bool resultOwnsInput()"):
            stage_loader.index("bool resultPending()")
        ]
        self.assertNotIn("resultPresentationReady", result_input)

        wheel = WARP_WHEEL.read_text(encoding="utf-8")
        guard = wheel[wheel.index("u8 guardExitArea(u8 nextState)"):]
        guard = guard[:guard.index("void update(TMarioGamePad *pad)")]
        preserve = guard.index("ILing::preserveRetailExitArea()")
        cancel = guard.index("cancelExplicitPractice()", preserve)
        self.assertLess(preserve, cancel)
        self.assertIn("sExplicitRetailExit = true;", guard[preserve:cancel])

        iling = ILING.read_text(encoding="utf-8")
        self.assertIn("bool preserveRetailExitArea()", iling)
        self.assertIn("scene.mAreaID == 0x3A && scene.mEpisodeID == 1", iling)
        self.assertIn("isPinnaOneRouteScene(scene)", iling)

    def test_death_retry_waits_for_retail_tail_and_preserves_session(self) -> None:
        wheel = WARP_WHEEL.read_text(encoding="utf-8")
        iling = ILING.read_text(encoding="utf-8")
        self.assertIn(
            "if (sQueuedSessionDeathRestart || sWaitForRetailDeathTail) return state;",
            wheel,
        )
        self.assertRegex(
            wheel,
            r"sDeathSequence\s*&&\s*"
            r"appState\s*<=\s*TApplication::CONTEXT_DIRECT_MAIN_LOOP\s*&&\s*"
            r"\(\(sQueuedSessionDeathRestart \|\| sWaitForRetailDeathTail\)\s*\|\|\s*"
            r"\(!sArmed && !sTailPending\)\)",
        )
        self.assertIn(
            "if (sDeathSequence && restartPromptPending()) return 0;",
            wheel,
        )
        self.assertIn(
            "if (sDeathSequence && sQueuedSessionDeathRestart && !sArmed) return 0;",
            wheel,
        )
        kick = wheel[wheel.index("u8 kick(TMarDirector *director, u8 state)"):]
        kick = kick[:kick.index("s32 onDirected(s32 appState)")]
        self.assertNotIn("sDeathSequence", kick)
        on_directed = wheel[wheel.index("s32 onDirected(s32 appState)"):]
        on_directed = on_directed[:on_directed.index("namespace WarpWheel")]
        self.assertRegex(
            on_directed,
            r"appState\s*<=\s*TApplication::CONTEXT_DIRECT_MAIN_LOOP\s*&&\s*"
            r"saveFlowActive\(\)",
        )
        self.assertRegex(
            on_directed,
            r"(?s)sArmed && sWaitForSave.*?getLastStatus\(\)\s*==\s*"
            r"CARD_ERROR_BUSY",
        )
        self.assertRegex(
            wheel,
            r"armWarp\(retry, false, true\);\s*"
            r"sSource = retry;\s*"
            r"sWaitForRetailDeathTail = true;",
        )
        self.assertRegex(
            wheel,
            r"\(deathExit \|\| saveDialog\)\s*&&\s*"
            r"\(fullRestart \|\| instantRestart\)",
        )
        self.assertRegex(
            wheel,
            r"queueDeathRestart\s*=\s*\n\s*deathExit && "
            r"StageLoader::deferRestartInput\(\);",
        )
        self.assertRegex(
            wheel,
            r"(?s)if \(queueDeathRestart && sArmed\) "
            r"cancelArmedCourseWarp\(\);.*?"
            r"requestOrDeferRestart\(fullRestart, saveDialog\);.*?"
            r"sQueuedSessionDeathRestart = queueDeathRestart;",
        )
        self.assertRegex(
            wheel,
            r"(?s)!afterResult && !StageLoader::acceptDeferredRestart\(\).*?"
            r"sQueuedSessionDeathRestart = false;.*?return;",
        )
        retry_resolver = wheel[wheel.index("void resolveDeferredRestart()") :]
        retry_resolver = retry_resolver[: retry_resolver.index("bool resumePBPrompt")]
        consume = retry_resolver.index("StageLoader::retryOwnsDeparture()")
        shine_latch = retry_resolver.index("StageLoader::departureResultPending()", consume)
        self.assertLess(consume, shine_latch)
        self.assertRegex(
            retry_resolver,
            r"!sQueuedSessionDeathRestart\s*&&\s*"
            r"StageLoader::retryOwnsDeparture\(\)",
        )
        self.assertRegex(
            retry_resolver,
            r"(?s)StageLoader::retryOwnsDeparture\(\).*?"
            r"sDeferredRestart = DEFERRED_RESTART_NONE;.*?"
            r"sDeferredRestartAfterResult = false;.*?return;",
        )
        stage_loader = STAGE_LOADER.read_text(encoding="utf-8")
        self.assertRegex(
            stage_loader,
            r"(?s)bool retryOwnsDeparture\(\).*?"
            r"STATE_REQUESTING.*?STATE_WAITING.*?"
            r"STATE_RETRY_DELAY.*?STATE_RETRY_PENDING",
        )
        self.assertRegex(
            wheel,
            r"(?s)\(queueDeathRestart \|\| saveDialog\).*?"
            r"toast\(\"Restart queued\"\)",
        )
        death_update = wheel[wheel.index("const u8 state = gpMarDirector->mCurState;"):]
        death_update = death_update[:death_update.index("bool closeForCommand")]
        self.assertNotIn("suppressClassicInstantUntilRelease();", death_update)
        self.assertIn("sClassicInstantHeld = true;", death_update)
        parent_retry = iling[
            iling.index("bool copySessionDeathRetryDest"):
        ]
        parent_retry = parent_retry[:parent_retry.index("void restoreWarpStartSnapshot")]
        self.assertRegex(
            parent_retry,
            r"if \(!sceneMatches\(scene, sAttemptStart\)\) return false;",
        )
        self.assertNotIn(
            "isInternalScene(sAttemptStart, scene)", parent_retry
        )
        reset_path = iling[iling.index("const bool sessionChildReset ="):]
        reset_path = reset_path[:reset_path.index("if (sCarryRestorePending")]
        self.assertRegex(
            reset_path,
            r"validEntry\(sSelectedEntry\) && StageLoader::active\(\) &&\s*"
            r"isInternalScene\(sAttemptStart, scene\)",
        )
        self.assertRegex(
            reset_path,
            r"sceneMatches\(scene, sAttemptStart\) \|\| sessionChildReset",
        )
        self.assertIn(
            "sChildRetryContinuation = sessionChildReset;", reset_path
        )
        self.assertRegex(
            reset_path,
            r"sRecordsEligible\s*=\s*!sessionChildReset\s*&&",
        )
        self.assertNotIn("StageLoader::cancel();", reset_path)

    def test_internal_child_retry_cannot_publish_parent_result(self) -> None:
        iling = ILING.read_text(encoding="utf-8")
        result = iling[iling.index("void recordResult(int entry, s32 qf)"):]
        result = result[:result.index("}  // namespace")]
        suppression = result[
            result.index("if (sChildRetryContinuation)"):
            result.index("sRecentQf[sRecentNext]")
        ]
        self.assertIn("StageLoader::onILResult(entry, qf, false);", suppression)
        self.assertIn("return;", suppression)
        self.assertNotIn("Records::onILResult", suppression)
        self.assertNotIn("recordPB", suppression)
        self.assertNotIn("SplitStats::onILResult", suppression)
        self.assertLess(
            result.index("if (sChildRetryContinuation)"),
            result.index("Records::onILResult"),
        )
        self.assertLess(
            result.index("if (sChildRetryContinuation)"),
            result.index("recordPB(entry, qf)"),
        )

    def test_fast_any_start_result_alias_contract(self) -> None:
        source = STAGE_LOADER.read_text(encoding="utf-8")
        match = re.search(
            r"constexpr\s+u8\s+kFastAny\[\]\s*=\s*\{(.*?)\};",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        assert match is not None
        entries = tuple(int(value) for value in re.findall(r"\d+", match.group(1)))
        self.assertEqual(
            entries,
            (
                1, 2, 5, 6, 121, 34, 78, 79, 80, 81, 82, 85, 86, 38, 39,
                42, 43, 46, 49, 13, 14, 16, 17, 20, 21, 22, 7, 10, 52, 53,
                56, 57, 60, 61, 62, 65, 66, 67, 68, 70, 71, 74, 92,
            ),
        )
        self.assertNotIn("kFastAnyBiancoPosition", source)
        self.assertNotIn("kFastAnyGelatoPosition", source)
        self.assertIn("kFastAnyPv5Position = 10", source)
        self.assertIn(
            "return preset == 0 && position == kFastAnyPv5Position;", source
        )
        self.assertIn(
            "if (entry == SUSAMUNE_STAGE_PLAYLIST_ACTION_BIANCO_1) return 1;",
            source,
        )
        self.assertIn(
            "if (entry == SUSAMUNE_STAGE_PLAYLIST_ACTION_GELATO_1) return 121;",
            source,
        )

    def test_exact_v1_and_v2_layouts(self) -> None:
        v1 = build_v1([[] for _ in range(SLOTS)])
        v2 = build_v2(make_v2_payload([[] for _ in range(SLOTS)]))
        self.assertEqual(len(v1), V1_SIZE)
        self.assertEqual(len(v2), V2_SIZE)
        self.assertIsNotNone(parse_v1(v1))
        self.assertIsNotNone(parse_v2(v2))
        self.assertEqual(
            (
                V2_COUNTS_OFFSET,
                V2_ENTRIES_OFFSET,
                V2_ACTIONS_OFFSET,
                V2_REVISIONS_OFFSET,
                V2_HASHES_OFFSET,
                V2_BESTS_OFFSET,
            ),
            (32, 39, 879, 984, 1012, 1052),
        )

    def test_v1_full_slot_and_duplicates_remain_valid(self) -> None:
        playlist = [120, 0, 0] + list(range(117))
        self.assertEqual(len(playlist), CAPACITY)
        self.assertIsNotNone(parse_v1(build_v1([playlist] + [[]] * 6)))

    def test_v2_accepts_only_contextual_actions(self) -> None:
        payload = make_v2_payload(
            [[0, 25, 82]] + [[]] * 6,
            action_positions={0: (0, 1, 2)},
        )
        self.assertIsNotNone(parse_v2(build_v2(payload)))

        bad = make_v2_payload([[1]] + [[]] * 6, action_positions={0: (0,)})
        self.assertIsNone(parse_v2(build_v2(bad)))

    def test_v2_rejects_inactive_actions_and_bad_custom_hash(self) -> None:
        payload = make_v2_payload([[0]] + [[]] * 6)
        actions = bytearray(payload.actions)
        set_action(actions, 1)
        tampered = V2Payload(
            payload.counts,
            payload.entries,
            bytes(actions),
            payload.revisions,
            payload.hashes,
            payload.bests,
        )
        self.assertIsNone(parse_v2(build_v2(tampered)))

        hashes = list(payload.hashes)
        hashes[BUILTINS] ^= 1
        tampered = V2Payload(
            payload.counts,
            payload.entries,
            payload.actions,
            payload.revisions,
            tuple(hashes),
            payload.bests,
        )
        self.assertIsNone(parse_v2(build_v2(tampered)))

    def test_region_bests_are_independent_and_zero_is_invalid(self) -> None:
        bests = [list(BEST_UNSET for _ in range(TOTAL)) for _ in range(REGIONS)]
        bests[0][0], bests[1][0], bests[2][0] = 100, 200, 300
        payload = make_v2_payload(
            [[] for _ in range(SLOTS)],
            builtin_hashes=(11, 12, 13),
            bests=tuple(tuple(region) for region in bests),
        )
        parsed = parse_v2(build_v2(payload))
        self.assertIsNotNone(parsed)
        assert parsed is not None
        self.assertEqual(tuple(region[0] for region in parsed[1].bests), (100, 200, 300))

        bests[2][0] = 0
        bad = make_v2_payload(
            [[] for _ in range(SLOTS)],
            bests=tuple(tuple(region) for region in bests),
        )
        self.assertIsNone(parse_v2(build_v2(bad)))

    def test_empty_custom_playlist_cannot_keep_a_best(self) -> None:
        bests = [list(BEST_UNSET for _ in range(TOTAL)) for _ in range(REGIONS)]
        bests[0][BUILTINS] = 123
        payload = make_v2_payload(
            [[] for _ in range(SLOTS)],
            bests=tuple(tuple(region) for region in bests),
        )
        self.assertIsNone(parse_v2(build_v2(payload)))

    def test_overwrite_identity_requires_clearing_every_regional_best(self) -> None:
        old_bests = [list(BEST_UNSET for _ in range(TOTAL)) for _ in range(REGIONS)]
        for region, value in enumerate((100, 200, 300)):
            old_bests[region][BUILTINS] = value
        current = make_v2_payload(
            [[0]] + [[]] * 6,
            revisions=(4,) + (0,) * 6,
            bests=tuple(tuple(region) for region in old_bests),
        )

        stale_bests = make_v2_payload(
            [[25]] + [[]] * 6,
            revisions=(5,) + (0,) * 6,
            bests=tuple(tuple(region) for region in old_bests),
        )
        self.assertFalse(identity_change_valid(current, stale_bests))

        cleared = make_v2_payload(
            [[25]] + [[]] * 6,
            revisions=(5,) + (0,) * 6,
        )
        self.assertTrue(identity_change_valid(current, cleared))

        first_bests = [list(BEST_UNSET for _ in range(TOTAL)) for _ in range(REGIONS)]
        first_bests[0][BUILTINS + 1] = 456
        first_identity = make_v2_payload(
            [[], [82]] + [[]] * 5,
            revisions=(4, 1) + (0,) * 5,
            bests=tuple(tuple(region) for region in first_bests),
        )
        self.assertTrue(identity_change_valid(current, first_identity))

    def test_v2_torn_header_payload_and_reserved_bytes_are_rejected(self) -> None:
        raw = bytearray(build_v2(make_v2_payload([[0, 25]] + [[]] * 6)))
        raw[V2_ENTRIES_OFFSET] ^= 0x40
        self.assertIsNone(parse_v2(raw))
        self.assertIsNone(parse_v2(raw[:-1]))

        raw = bytearray(build_v2(make_v2_payload([[] for _ in range(SLOTS)])))
        raw[20] = 1
        self.assertIsNone(parse_v2(raw))

    def test_v2_metadata_and_future_versions_are_rejected(self) -> None:
        payload = make_v2_payload([[] for _ in range(SLOTS)])
        self.assertIsNone(parse_v2(build_v2(payload, version=3)))
        self.assertIsNone(parse_v2(build_v2(payload, metadata=(3, 3, 14, 1))))

    def test_generation_wrap_order(self) -> None:
        self.assertTrue(generation_is_newer(0, 0xFFFFFFFF))
        self.assertFalse(generation_is_newer(0xFFFFFFFF, 0))
        self.assertFalse(generation_is_newer(7, 7))

    def test_v1_migration_uses_newest_and_initialises_v2_fields(self) -> None:
        old = build_v1([[1, 1]] + [[]] * 6, generation=0xFFFFFFFF)
        new_routes = [[0, 0, 25, 82], [120]] + [[]] * 5
        new = build_v1(new_routes, generation=0)
        migrated = import_newest_v1((old, new))
        self.assertIsNotNone(migrated)
        assert migrated is not None
        self.assertEqual(migrated.counts[:2], bytes((4, 1)))
        self.assertEqual(migrated.entries[:4], bytes(new_routes[0]))
        self.assertFalse(any(migrated.actions))
        self.assertEqual(migrated.revisions, (0,) * SLOTS)
        self.assertTrue(
            all(best == BEST_UNSET for region in migrated.bests for best in region)
        )
        for slot in range(SLOTS):
            start = slot * CAPACITY
            action_start = slot * ACTION_BYTES
            self.assertEqual(
                migrated.hashes[BUILTINS + slot],
                content_hash(
                    BUILTINS + slot,
                    0,
                    migrated.counts[slot],
                    migrated.entries[start:start + CAPACITY],
                    migrated.actions[action_start:action_start + ACTION_BYTES],
                ),
            )

    def test_shared_constants_offsets_and_safe_journal_paths(self) -> None:
        header = HEADER.read_text(encoding="utf-8")
        kernel = KERNEL.read_text(encoding="utf-8")
        expected = {
            "SUSAMUNE_STAGE_PLAYLIST_VERSION_V1": VERSION_V1,
            "SUSAMUNE_STAGE_PLAYLIST_VERSION": VERSION,
            "SUSAMUNE_STAGE_PLAYLIST_COUNT": SLOTS,
            "SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT": BUILTINS,
            "SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT": REGIONS,
            "SUSAMUNE_STAGE_PLAYLIST_CAPACITY": CAPACITY,
            "SUSAMUNE_STAGE_PLAYLIST_ROUTE_COUNT": ROUTES,
            "SUSAMUNE_STAGE_PLAYLIST_ACTION_SCHEMA": ACTION_SCHEMA,
        }
        for name, value in expected.items():
            match = re.search(rf"#define\s+{name}\s+(\d+)u", header)
            self.assertIsNotNone(match, name)
            assert match is not None
            self.assertEqual(int(match.group(1)), value, name)
        self.assertRegex(
            header,
            r"SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET\s+0xFFFFFFFFu",
        )
        for offset in (1216, 896, 1184, 911, 1016, 1044, 1084, 879, 984, 1012, 1052):
            self.assertIn(f"== {offset}", header)
        self.assertIn("susamune_stage_playlists_v2_a.bin", kernel)
        self.assertIn("susamune_stage_playlists_v2_b.bin", kernel)
        self.assertIn("susamune_stage_playlists_v1_a.bin", kernel)
        self.assertIn("susamune_stage_playlists_v1_b.bin", kernel)
        self.assertIn("ReadStagePlaylistV1File", kernel)
        self.assertIn("playlists->saveSeq = 1", kernel)
        self.assertIn("f_sync(&f)", kernel)
        self.assertIn("PB_READ_UNSAFE", kernel)
        self.assertIn("StagePlaylistContentHash", kernel)
        self.assertIn("StagePlaylistIdentityChangesValid", kernel)
        self.assertIn("StagePlaylistRememberIdentities", kernel)
        self.assertIn("StagePlaylistV1Checksum", kernel)
        self.assertIn("StagePlaylistChecksum", kernel)
        self.assertIn("sizeof(*playlists) - __builtin_offsetof(", kernel)


if __name__ == "__main__":
    unittest.main(verbosity=2)
