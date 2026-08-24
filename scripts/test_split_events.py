#!/usr/bin/env python3
"""Host contracts for the descriptor-driven IL checkpoint observers."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "split_events.cpp"
HEADER = ROOT / "include" / "susamune" / "split_events.hxx"
SPLIT_STATS = ROOT / "src" / "split_stats.cpp"
ILING_ENTRIES = ROOT / "src" / "iling_entries.inc"
GAMEPLAY = ROOT / "src" / "gameplay_polish.cpp"
MAIN = ROOT / "src" / "main.cpp"
SAVESTATE = ROOT / "src" / "savestate.cpp"
QFT = ROOT / "src" / "qft_timer.cpp"
SETTINGS = ROOT / "include" / "susamune" / "settings_list.h"
PATCHES_PATH = ROOT / "scripts" / "patches.py"
SCHEMA_PATH = ROOT / "scripts" / "split_checkpoint_schema.py"

spec = importlib.util.spec_from_file_location("split_event_patches", PATCHES_PATH)
assert spec and spec.loader
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)

schema_spec = importlib.util.spec_from_file_location(
    "split_checkpoint_schema_events", SCHEMA_PATH
)
assert schema_spec and schema_spec.loader
checkpoint_schema = importlib.util.module_from_spec(schema_spec)
schema_spec.loader.exec_module(checkpoint_schema)


STATIC_HOOKS = {
    "susamuneSplitCoinRedTaken": (
        patches.PatchType.B,
        (0x801962C8, 0x801BE428, 0x801B62E0),
    ),
    "susamuneSplitPiantaRecoverNerve": (
        patches.PatchType.BL,
        (0x80177CF0, 0x8021358C, 0x8020B470),
    ),
    "susamuneSplitEmitHappyEffect": (
        patches.PatchType.B,
        (0x8017A274, 0x80215B0C, 0x8020D9F0),
    ),
}

DYNAMIC_HOOKS = {
    "kChangePlayerStatus": (
        "sChangePlayerStatusTrampoline",
        "susamuneSplitChangePlayerStatus",
        (0x80133424, 0x80254034, 0x8024BDC0),
        0x7C0802A6,
    ),
    "kSpineUpdate": (
        "sSpineUpdateTrampoline",
        "susamuneSplitSpineUpdate",
        (0x80111BD0, 0x8003C8A8, 0x8003C6F8),
        0x7C0802A6,
    ),
    "kStartDemo": (
        "sStartDemoTrampoline",
        "susamuneSplitStartDemo",
        (0x800ED7C0, 0x8029A23C, 0x802920D4),
        0x9421FFC8,
    ),
    "kOpenTalk": (
        "sOpenTalkTrampoline",
        "susamuneSplitOpenTalk",
        (0x80214CF0, 0x80153824, 0x80148758),
        0x7C0802A6,
    ),
    "kRailCheck": (
        "sRailCheckTrampoline",
        "susamuneSplitRailCheck",
        (0x801C8CB4, 0x801F1428, 0x801E9300),
        0x7C0802A6,
    ),
    "kBathtubQuake": (
        "sBathtubQuakeTrampoline",
        "susamuneSplitBathtubQuake",
        (0x801D3248, 0x801FB674, 0x801F3558),
        0x7C0802A6,
    ),
}

CAPTURED_HOOKS = {
    "kPeteyHipDrop": (
        "sPeteyHipDropTrampoline", "susamuneSplitPeteyHipDrop",
        (0x802A8790, 0x80095A0C, 0x8008F0AC),
    ),
    "kGessoTentacleDamage": (
        "sGessoTentacleDamageTrampoline",
        "susamuneSplitGessoTentacleDamage",
        (0x8028B4F8, 0x800786A0, 0x80071D40),
    ),
    "kEelToothMessage": (
        "sEelToothMessageTrampoline", "susamuneSplitEelToothMessage",
        (0x802E9194, 0x800D7080, 0x800D0720),
    ),
    "kFenceMessage": (
        "sFenceMessageTrampoline", "susamuneSplitFenceMessage",
        (0x801C60EC, 0x801EE7C4, 0x801E669C),
    ),
    "kRevolvingFenceMessage": (
        "sRevolvingFenceMessageTrampoline",
        "susamuneSplitRevolvingFenceMessage",
        (0x801C5AB0, 0x801EE188, 0x801E6060),
    ),
}

CARRY_ROWS = (
    ("ROUTE_RICCO_1", 3, 0, 0x3B, 0),
    ("ROUTE_RICCO_2", 3, 1, 0x1E, 0),
    ("ROUTE_RICCO_4", 3, 3, 0x30, 0),
    ("ROUTE_BIANCO_3_FULL", 2, 2, 0x2F, 0),
    ("ROUTE_BIANCO_6_FULL", 2, 5, 0x2E, 0),
    ("ROUTE_PIANTA_5_FULL", 8, 4, 0x2A, 0),
    ("ROUTE_PINNA_1", 0x0D, 6, 0x3A, 1),
    ("ROUTE_PINNA_2_FULL", 5, 1, 0x32, 0),
    ("ROUTE_PINNA_EYG", 5, 2, 0x29, 0),
    ("ROUTE_SIRENA_2_FULL", 6, 1, 7, 0),
    ("ROUTE_SIRENA_2_FULL", 7, 0, 0x33, 0),
    ("ROUTE_SIRENA_3", 6, 2, 7, 1),
    ("ROUTE_SIRENA_4_FULL", 6, 3, 7, 2),
    ("ROUTE_SIRENA_4_FULL", 7, 2, 0x0E, 0),
    ("ROUTE_SIRENA_4_FULL", 0x0E, 0, 0x28, 0),
    ("ROUTE_SIRENA_5", 6, 4, 7, 2),
    ("ROUTE_SIRENA_5", 7, 2, 0x0E, 1),
    ("ROUTE_SIRENA_5", 0x0E, 1, 0x38, 0),
    ("ROUTE_SIRENA_7", 6, 6, 7, 3),
    ("ROUTE_NOKI_4_FULL", 9, 3, 0x39, 0),
    ("ROUTE_NOKI_6_FULL", 9, 5, 0x1F, 0),
    ("ROUTE_CORONA", 0x34, 0, 0x3C, 0),
)

ROUTE_COUNTS = tuple(map(len, checkpoint_schema.CHECKPOINTS))


def source_text(path: Path = SOURCE) -> str:
    return path.read_text(encoding="utf-8")


class SplitEventContractTests(unittest.TestCase):
    def test_frozen_route_layout_tiles_all_ils(self) -> None:
        text = source_text(SPLIT_STATS)
        table = text.split("const RouteDesc kRoutes", 1)[1].split("};", 1)[0]
        rows = [
            tuple(map(int, match))
            for match in re.findall(r"\{(\d+),\s*(\d+),\s*(\d+)\}", table)
        ]
        self.assertEqual(len(rows), 122)
        self.assertEqual(tuple(row[2] for row in rows), ROUTE_COUNTS)
        self.assertEqual(
            tuple(row[1] for row in rows), checkpoint_schema.ROUTE_ENTRIES
        )
        self.assertEqual(
            tuple(map(len, checkpoint_schema.CHECKPOINTS)), ROUTE_COUNTS
        )
        self.assertEqual(sum(ROUTE_COUNTS), 153)
        self.assertEqual(sum(count + 1 for count in ROUTE_COUNTS), 275)
        self.assertEqual(
            checkpoint_schema.schema_hash(),
            checkpoint_schema.EXPECTED_SCHEMA_HASH,
        )
        cursor = 0
        for first, _, count in rows:
            self.assertEqual(first, cursor)
            cursor += count + 1

    def test_manifest_keeps_only_the_three_legacy_static_hooks(self) -> None:
        for symbol, (kind, addresses) in STATIC_HOOKS.items():
            rows = [row for row in patches.patches if row.get("sym") == symbol]
            self.assertEqual(len(rows), 1, symbol)
            self.assertEqual(rows[0]["type"], kind)
            self.assertEqual(
                (rows[0]["jp"], rows[0]["us"], rows[0]["pal"]), addresses
            )
        split_rows = [
            row for row in patches.patches
            if row.get("sym", "").startswith("susamuneSplit")
        ]
        self.assertEqual({row["sym"] for row in split_rows}, set(STATIC_HOOKS))
        for _, wrapper, _, _ in DYNAMIC_HOOKS.values():
            self.assertFalse(any(row.get("sym") == wrapper for row in patches.patches))

    def test_dynamic_hooks_have_exact_regions_and_displaced_words(self) -> None:
        text = source_text()
        for constant, (trampoline, wrapper, addresses, displaced) in (
            DYNAMIC_HOOKS.items()
        ):
            address_text = ", ".join(f"0x{address:08X}u" for address in addresses)
            self.assertIn(f"const u32 {constant} =", text)
            self.assertIn(f"SUSAMUNE_MEM1_ADDR({address_text})", text)
            self.assertRegex(
                text,
                rf"u32 {trampoline}\[2\] = \{{0x{displaced:08X}u, 0\}};",
            )
            self.assertRegex(
                text,
                rf"installEntryHook\({constant},\s*"
                rf"reinterpret_cast<const void \*>\(\s*&{wrapper}\),\s*{trampoline}\);",
            )

        for constant, (trampoline, wrapper, addresses) in CAPTURED_HOOKS.items():
            address_text = ", ".join(f"0x{address:08X}u" for address in addresses)
            self.assertIn(f"SUSAMUNE_MEM1_ADDR({address_text})", text)
            self.assertIn(f"u32 {trampoline}[2] = {{0, 0}};", text)
            self.assertRegex(
                text,
                rf"installCapturedEntryHook\({constant},\s*"
                rf"reinterpret_cast<const void \*>\(\s*&{wrapper}\),\s*"
                rf"{trampoline}\);",
            )

    def test_lifecycle_wraps_one_retail_direct_and_invalidates_savestates(self) -> None:
        header = source_text(HEADER)
        for api in (
            "void init();",
            "void beforeStageSetup();",
            "void onStageSetup(TMarDirector *director);",
            "void beginFrame();",
            "void update();",
            "void onSavestateLoaded();",
        ):
            self.assertIn(api, header)
        main = source_text(MAIN)
        begin = main.index("SplitEvents::beginFrame();")
        direct = main.index("director->direct();", begin)
        update = main.index("SplitEvents::update();", direct)
        self.assertLess(begin, direct)
        self.assertLess(direct, update)
        self.assertLess(main.rindex("gQFTTimer.update();", direct, update), update)
        savestate = source_text(SAVESTATE)
        self.assertLess(
            savestate.index("gQFTTimer.onSavestateLoaded();"),
            savestate.index("SplitEvents::onSavestateLoaded();"),
        )
        source = source_text()
        restored = source[source.index("void onSavestateLoaded()") :]
        restored = restored[:restored.index("extern \"C\"")]
        self.assertLess(
            restored.index("sAttemptSerial = gQFTTimer.attemptSerial();"),
            restored.index("sAttemptInvalid = true;"),
        )

    def test_demo_wrapper_freezes_only_after_retail_accepts_it(self) -> None:
        text = source_text()
        self.assertGreaterEqual(
            text.count("const JDrama::TFlagT<u16> *demoFlag"), 2
        )
        self.assertIn(
            "JDrama::TActor *, const JDrama::TFlagT<u16> *);", text
        )
        self.assertNotIn("JDrama::TActor *, u16 demoFlag", text)
        wrapper = text.rsplit('extern "C" void susamuneSplitStartDemo', 1)[1]
        wrapper = wrapper.split('extern "C" void susamuneSplitOpenTalk', 1)[0]
        self.assertIn("+ 0x24C", wrapper)
        self.assertIn("if (after == before) return;", wrapper)
        self.assertLess(wrapper.index("if (after == before) return;"),
                        wrapper.index("gQFTTimer.freezeEvent();"))
        self.assertIn("captureDemoEvent(", wrapper)
        self.assertIn(
            "SplitStats::onRouteEvent(candidateRoute, candidateEvent, candidateQf)",
            wrapper,
        )
        self.assertNotIn("publishEvent", wrapper)
        capture = text.split("bool captureDemoEvent", 1)[1].split(
            "void updateTransitions", 1
        )[0]
        self.assertIn(
            "SplitStats::routeActive(SplitStats::ROUTE_BIANCO_2)", capture
        )
        self.assertIn("director->mAreaID != 2", capture)
        self.assertIn("director->mEpisodeID != 0", capture)
        self.assertIn("*event = 1;", capture)
        self.assertNotIn("DIRECT(SETTING_TIMER_FREEZE_DEMO", source_text(QFT))
        setup = text[text.index("void beforeStageSetup()") :]
        setup = setup[:setup.index("void onStageSetup(")]
        self.assertIn(
            "const TGameSequence &previous = gpApplication.mPrevScene;", setup
        )
        self.assertIn(
            "const TGameSequence &current = gpApplication.mCurrentScene;", setup
        )

    def test_carry_table_is_exact_and_known_transitions_arm_it(self) -> None:
        text = source_text()
        carry_table = text.split("const CarryDesc kCarryRoutes[]", 1)[1].split("};", 1)[0]
        parsed = tuple(
            (route, *(int(value, 0) for value in values))
            for route, *values in re.findall(
                r"\{SplitStats::(ROUTE_\w+),\s*(0x[0-9A-Fa-f]+|\d+),\s*"
                r"(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+),\s*"
                r"(0x[0-9A-Fa-f]+|\d+)\}",
                carry_table,
            )
        )
        self.assertEqual(parsed, CARRY_ROWS)
        publish = text.split("bool publishTransition", 1)[1].split(
            "bool isSpinStatus", 1
        )[0]
        self.assertIn(
            "gQFTTimer.transitionEntryQf(&qf, &capturedTarget)", publish
        )
        self.assertNotIn("SUSAMUNE_ADDR_QFT_TRANSITION_QF", publish)
        self.assertNotIn("gQFTTimer.currentQf", publish)
        self.assertLess(
            publish.index("publishEventAt(route, event, qf)"),
            publish.index("sArmedCarryRoute = route"),
        )
        qft = source_text(QFT)
        entry = qft.split("bool QFTTimer::transitionEntryQf", 1)[1].split(
            "bool QFTTimer::consumeTransition", 1
        )[0]
        self.assertIn("*qf = frozenDisplayQf();", entry)
        frozen = qft.split("s32 frozenDisplayQf()", 1)[1].split(
            "s32 compactQf()", 1
        )[0]
        self.assertIn("*sTransitionTarget != 0xFFFF", frozen)
        self.assertIn(
            "sState->offsetQf + *sTransitionQf - 4", frozen
        )
        self.assertIn("sState->offsetQf + sState->freezeQf", frozen)
        compact = qft.split("s32 compactQf()", 1)[1].split(
            "s32 qfToMillis", 1
        )[0]
        self.assertGreaterEqual(compact.count("frozenDisplayQf()"), 2)
        self.assertIn("*sTransitionTarget != 0xFFFF", compact)
        capture = qft.split("void captureSection()", 1)[1].split(
            "s32 qfToRoundedCentis", 1
        )[0]
        self.assertIn("const s32 current = frozenDisplayQf();", capture)
        finish = qft.split("bool QFTTimer::consumeTransition", 1)[1].split(
            "static bool consumeStop", 1
        )[0]
        self.assertIn("sState->offsetQf + *sTransitionQf", finish)
        self.assertNotIn("- 4", finish)
        self.assertIn("sBlockNextAttempt = true;", text)
        self.assertIn("sAttemptInvalid = sBlockNextAttempt;", text)
        self.assertIn("void armCarryTransition()", text)
        self.assertIn(
            "sActiveRoute == SplitStats::ROUTE_PINNA_1", text
        )
        self.assertIn(
            "sArmedCarryRoute == SplitStats::ROUTE_PINNA_1", text
        )
        self.assertIn(
            "routeScene(sActiveRoute, 0x0D, 6)", text
        )
        self.assertIn(
            "routeScene(SplitStats::ROUTE_PINNA_1, 0x3A, 1)", text
        )
        transitions = text.split("void updateTransitions()", 1)[1].split(
            "bool crossedAbove", 1
        )[0]
        corona = transitions.split("case SplitStats::ROUTE_CORONA:", 1)[1]
        corona = corona.split("break;", 1)[0]
        self.assertIn("publishTransition(sActiveRoute, 2, 0x3C)", corona)

    def test_pinna_one_arms_each_exact_retail_or_loading_transition(self) -> None:
        text = source_text()
        header = source_text(HEADER)
        warp = source_text(ROOT / "src" / "warp_wheel.cpp")
        self.assertIn("void armPinnaOneRetailExit();", header)
        guard = warp.split("u8 guardExitArea", 1)[1].split(
            "void update(TMarioGamePad", 1
        )[0]
        preserve = guard.split("ILing::preserveRetailExitArea()", 1)[1]
        self.assertLess(
            preserve.index("SplitEvents::armPinnaOneRetailExit();"),
            preserve.index("return nextState;"),
        )
        arm = text.split("void armPinnaOneRetailExit()", 1)[1].split(
            "void onSavestateLoaded()", 1
        )[0]
        self.assertIn("sRetailDirectOpen", arm)
        self.assertIn("area == 0x0D && episode == 0", arm)
        self.assertIn("area == 0x3A && episode == 1", arm)
        middle = text.split("void armCarryTransition()", 1)[1].split(
            "bool isSpinStatus", 1
        )[0]
        self.assertIn("routeScene(sActiveRoute, 0x0D, 6)", middle)
        self.assertIn("mNextScene.mAreaID == 0x3A", middle)
        self.assertLess(
            middle.index("routeScene(sActiveRoute, 0x0D, 6)"),
            middle.index("SUSAMUNE_ADDR_QFT_TRANSITION_TARGET"),
        )

    def test_gbs_is_physical_g1_and_legacy_alias_is_25_to_121(self) -> None:
        text = source_text()
        self.assertIn("routeScene(SplitStats::ROUTE_GELATO_8_GBS, 4, 0)", text)
        self.assertIn("held->mObjectID == TResetFruit::COCONUT", text)
        self.assertIn("sPreHeldObject == sCoconut", text)
        self.assertIn("sCoconutThrowArmed", text)
        self.assertIn("sCoconut->mTranslation.z > 10000.0f", text)
        stats = source_text(SPLIT_STATS)
        self.assertIn("activeRouteMatches(25, 121)", stats)
        self.assertNotIn("activeRouteMatches(25, 35)", stats)

    def test_buffered_status_hook_and_requested_thresholds_are_exact(self) -> None:
        text = source_text()
        for status in (
            "kMarioDiveStatus = 0x0080088Au",
            "kMarioSpinLeftStatus = 0x00000895u",
            "kMarioSpinRightStatus = 0x00000896u",
            "kMarioBounceStatus = 0x00000884u",
            "kMarioSurfStatus = 0x00810446u",
            "kMarioHangRoofStatus = 0x08200348u",
            "kMarioRolloutStatus = 0x02000889u",
            "kMarioLedgeGrabStatus = 0x3800034Bu",
            "kMarioWallKickStatus = 0x02000886u",
            "kMarioThrowObjectStatus = 0x820008ABu",
        ):
            self.assertIn(status, text)
        wrapper = text.split('extern "C" int susamuneSplitChangePlayerStatus', 1)[1]
        self.assertLess(wrapper.index("sChangePlayerStatusTrampoline"),
                        wrapper.index("noteMarioStatus"))
        self.assertIn("if (changed) noteMarioStatus(player, player->mState);", wrapper)
        self.assertIn("return changed;", wrapper)
        b2 = text.split("case SplitStats::ROUTE_BIANCO_2:", 1)[1].split(
            "break;", 1
        )[0]
        self.assertIn("status == kMarioRolloutStatus", b2)
        self.assertIn("mario->mTranslation.y >= 3200.0f", b2)
        self.assertIn("publishEvent(sActiveRoute, 0)", b2)
        entries = ILING_ENTRIES.read_text(encoding="utf-8")
        self.assertIn(
            'SHINE("Bianco 1", 2, 0, 0, 0, GROUP_BIANCO)', entries
        )
        self.assertIn(
            'SHINE("Bianco 2", 2, 0, 0, 1, GROUP_BIANCO)', entries
        )
        self.assertNotIn("promoteBiancoTwo", text)
        petey = text.split("void notePeteyDamage", 1)[1].split(
            "void notePetey(", 1
        )[0]
        self.assertIn("publishEvent(sActiveRoute, sPeteyHits + 1)", petey)
        wake = text.split("void notePetey(", 1)[1].split(
            "void noteBossGesso", 1
        )[0]
        self.assertIn("kPeteyBreakSleepVtable", wake)
        self.assertNotIn("ROUTE_BIANCO_2", wake)
        self.assertIn("publishEvent(sActiveRoute, 0)", wake)
        for threshold in (2, 5, 7, 10):
            self.assertIn(f"sCleanedPiantaCount == {threshold}", text)
        self.assertIn("count == 1", text)
        self.assertIn("count == 5", text)
        self.assertIn("count == 8", text)
        self.assertIn("routeScene(sActiveRoute, 0x0D, 6)", text)

    def test_actor_and_polling_primitives_cover_bosses_and_eel_edges(self) -> None:
        text = source_text()
        for addresses in (
            "0x803DFDF4u, 0x803BB71Cu, 0x803B353Cu",
            "0x803D8E0Cu, 0x803B45D4u, 0x803AC3F4u",
            "0x803D87C0u, 0x803B3F88u, 0x803ABDA8u",
            "0x803DA9C0u, 0x803B6178u, 0x803ADF98u",
            "0x803DBF54u, 0x803B770Cu, 0x803AF52Cu",
            "0x803DA2E0u, 0x803B5AA8u, 0x803AD8C8u",
            "0x803DC364u, 0x803B7B1Cu, 0x803AF93Cu",
            "0x803DCFFCu, 0x803B8924u, 0x803B0744u",
            "0x803DD4F4u, 0x803B8E1Cu, 0x803B0C3Cu",
        ):
            self.assertIn(addresses, text)
        self.assertIn("nerveBefore == kBossEelWaitAppearVtable", text)
        self.assertIn("nerveAfter != kBossEelWaitAppearVtable", text)
        self.assertIn("before <= 1 || after != 1", text)
        self.assertIn("eelToothAlreadyCounted(tooth)", text)

        self.assertIn("reinterpret_cast<const u8 *>(sTinKoopa) + 0x1C8", text)
        self.assertIn("reinterpret_cast<const u8 *>(manager) + 0x8C", text)
        self.assertIn("degree < 18768", text)
        self.assertIn("kGetNumGripsDead", text)

    def test_shadow_mario_finds_manager_free_conductor_actor(self) -> None:
        text = source_text()
        finder = text.split("void *findStandaloneActor", 1)[1].split(
            "bool isShadowRoute", 1
        )[0]
        self.assertIn("gpConductor->_30.begin()", finder)
        self.assertIn("gpConductor->_30.end()", finder)
        self.assertIn("objectVtable(actor) == actorVtable", finder)

        update = text.split("void updateShadowMario()", 1)[1].split(
            "void updateBowser()", 1
        )[0]
        self.assertIn("findStandaloneActor(kEmarioVtable)", update)
        self.assertLess(
            update.index("findStandaloneActor(kEmarioVtable)"),
            update.index("findManagedActor(kEmarioManagerVtable, kEmarioVtable)"),
        )

    def test_ricco_one_uses_exact_tentacle_call_and_post_direct_health(self) -> None:
        text = source_text()
        self.assertIn(
            "0x8028B4F8u, 0x800786A0u, 0x80071D40u", text
        )
        hook = text.rsplit(
            'extern "C" void susamuneSplitGessoTentacleDamage', 1
        )[1].split("bool eelToothAlreadyCounted", 1)[0]
        self.assertIn("hookScene(SplitStats::ROUTE_RICCO_1, 3, 0)", hook)
        self.assertLess(hook.index("currentQf(&qf)"),
                        hook.index("sGessoTentacleDamageTrampoline"))
        self.assertLess(hook.index("sGessoTentacleDamageTrampoline"),
                        hook.index("publishEventAt"))
        self.assertIn("sArmedCarryRoute = SplitStats::ROUTE_RICCO_1", hook)
        poll = text.split("void updateBossGesso()", 1)[1].split(
            "void updateManta()", 1
        )[0]
        self.assertIn("sBossGesso->mHealth", poll)
        self.assertIn("noteBossGesso(sBossGessoHealth, health)", poll)
        self.assertIn("routeScene(SplitStats::ROUTE_RICCO_1, 0x3B, 0)", poll)
        carry = text.split("void armCarryTransition()", 1)[1].split(
            "bool isSpinStatus", 1
        )[0]
        self.assertIn("routeScene(sActiveRoute, 3, 0)", carry)
        self.assertIn("mNextScene.mAreaID == 0x3B", carry)
        setup = text.split("void beforeStageSetup()", 1)[1].split(
            "void onStageSetup", 1
        )[0]
        self.assertIn("sActiveRoute == SplitStats::ROUTE_RICCO_1", setup)
        self.assertIn("sceneMatches(current, 0x3B, 0)", setup)
        update = text.split("void update()", 1)[1].split(
            "void onYoshiMounted()", 1
        )[0]
        self.assertIn("updateBossGesso();", update)
        wrapper = text.rsplit('extern "C" void susamuneSplitStartDemo', 1)[1]
        wrapper = wrapper.split('extern "C" void susamuneSplitOpenTalk', 1)[0]
        self.assertNotIn("ROUTE_RICCO_1", wrapper)
        self.assertNotIn("GessoBeakMessage", text)

    def test_exact_rail_start_drives_freeze_and_sirena_checkpoints(self) -> None:
        text = source_text()
        rail = text.rsplit('extern "C" bool susamuneSplitRailCheck', 1)[1].split(
            'extern "C" void susamuneSplitBathtubQuake', 1
        )[0]
        self.assertIn("rail->mControlState", rail)
        self.assertIn("!(before & 2) || (after & 2) || !(after & 1)", rail)
        self.assertIn("SETTING_TIMER_FREEZE_MOVING_PLATFORM", rail)
        self.assertIn("gQFTTimer.freezeEvent();", rail)
        self.assertIn("mTranslation.y < 4500.0f", rail)
        self.assertIn("mControlState) == 0x140", text)
        self.assertNotIn("mPriorityCollisionOwner", text)
        self.assertNotIn("0x400002BD", text)
        self.assertIn(
            'X(SETTING_TIMER_FREEZE_MOVING_PLATFORM, "timer_freeze_moving_platform")',
            source_text(SETTINGS),
        )
        self.assertIn("void QFTTimer::freezeEvent()", source_text(QFT))

    def test_spine_hook_has_inactive_and_non_actor_route_fast_path(self) -> None:
        text = source_text()
        wrapper = text.rsplit('extern "C" void susamuneSplitSpineUpdate', 1)[1].split(
            'extern "C" void susamuneSplitStartDemo', 1
        )[0]
        fast = wrapper.split("TLiveActor *actor", 1)[0]
        self.assertIn("!sRetailDirectOpen || !routeUsesSpine(sActiveRoute)", fast)
        self.assertIn("sSpineUpdateTrampoline", fast)
        self.assertIn("return;", fast)

    def test_yoshi_and_nozzle_callbacks_are_wired_to_existing_wrappers(self) -> None:
        header = source_text(HEADER)
        self.assertIn("void onYoshiMounted();", header)
        self.assertIn("void onNozzleCollected();", header)
        gameplay = source_text(GAMEPLAY)
        self.assertIn('#include "susamune/split_events.hxx"', gameplay)
        self.assertIn("SplitEvents::onYoshiMounted();", gameplay)
        self.assertIn("SplitEvents::onNozzleCollected();", gameplay)
        text = source_text()
        yoshi = text.split("void onYoshiMounted()", 1)[1].split(
            "void onNozzleCollected()", 1
        )[0]
        self.assertIn("ROUTE_PINNA_EYG", yoshi)
        self.assertNotIn("ROUTE_RICCO_6", yoshi)
        self.assertIn("ROUTE_CORONA", text.split("void onNozzleCollected()", 1)[1])

    def test_expected_event_only_and_fixed_storage_contracts(self) -> None:
        stats = source_text(SPLIT_STATS)
        self.assertIn("local != sState->expectedEvent", stats)
        text = source_text()
        self.assertIn("TBaseNPC *sRecoveredPiantas[kPiantaCount];", text)
        self.assertIn("TLiveActor *sDeadFireWanwans[3];", text)
        self.assertNotRegex(text, r"\b(new|malloc|calloc|realloc)\b")
        self.assertNotIn("PendingEvent", text)
        self.assertIn("gQFTTimer.attemptSerial() == sAttemptSerial", text)


if __name__ == "__main__":
    unittest.main()
