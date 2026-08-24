#!/usr/bin/env python3
"""Contracts for the current menu, modal input, and movement-style payload."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function(source: str, signature: str) -> str:
    match = re.search(signature + r"\s*\{", source)
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    start = match.end() - 1
    depth = 0
    for end in range(start, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start : end + 1]
    raise AssertionError(f"unterminated function: {signature}")


class NestedMenuContracts(unittest.TestCase):
    def test_nested_pages_cover_every_setting_in_their_category_once(self) -> None:
        menu = text("src/menu.cpp")
        settings = re.findall(
            r"X\((SETTING_[A-Z0-9_]+),",
            text("include/susamune/settings_list.h"),
        )
        categories = re.findall(
            r"^(?:SBOOL|SCHOICE)\(.*,\s*(SETTING_CAT_[A-Z_]+)\)\s*$",
            text("src/settings_descs.inc"),
            re.MULTILINE,
        )
        self.assertEqual(len(settings), len(categories))
        setting_category = dict(zip(settings, categories))

        def page_ids(*arrays: str) -> list[str]:
            result: list[str] = []
            for array in arrays:
                match = re.search(
                    rf"const u8 {array}\[\]\s*=\s*\{{([^}}]+)\}}", menu
                )
                self.assertIsNotNone(match, array)
                result.extend(re.findall(r"SETTING_[A-Z0-9_]+", match.group(1)))
            return result

        page_groups = {
            "SETTING_CAT_QOL": (
                "kGameplayCoreSettings",
                "kGameplaySkipSettings",
                "kGameplayWorldSettings",
            ),
            "SETTING_CAT_TIMER": (
                "kTimerDisplaySettings",
                "kTimerFreezeSettings",
            ),
            "SETTING_CAT_UI": (
                "kDisplayMovementSettings",
                "kDisplayOtherSettings",
            ),
            "SETTING_CAT_COSMETIC": (
                "kAppearanceMarioSettings",
                "kAppearanceWorldSettings",
            ),
        }
        for category, arrays in page_groups.items():
            actual = page_ids(*arrays)
            self.assertEqual(len(actual), len(set(actual)), category)
            expected = {
                setting
                for setting, setting_cat in setting_category.items()
                if setting_cat == category
            }
            self.assertEqual(set(actual), expected, category)

    def test_timer_freezes_are_one_nested_page(self) -> None:
        menu = text("src/menu.cpp")
        settings = text("include/susamune/settings_list.h")
        freeze_ids = set(re.findall(r"X\((SETTING_TIMER_FREEZE_[A-Z_]+),", settings))
        page = re.search(
            r"const u8 kTimerFreezeSettings\[\]\s*=\s*\{([^}]+)\}", menu
        )
        self.assertIsNotNone(page)
        self.assertEqual(freeze_ids, set(re.findall(r"SETTING_TIMER_FREEZE_[A-Z_]+", page.group(1))))
        self.assertIn('{"QFT freezes", kTimerFreezeSettings', menu)
        for setting in (
            "SETTING_TIMER_SUNSHINE_VISIBILITY",
            "SETTING_TIMER_QFT_VISIBILITY",
            "SETTING_TIMER_SECTIONS",
            "SETTING_LEVEL_SPLITS",
        ):
            self.assertIn(setting, menu)

    def test_back_unwinds_inner_page_before_settings_hub(self) -> None:
        menu = text("src/menu.cpp")
        self.assertIn("virtual bool back() { return false; }", menu)
        self.assertIn("if (!hasPages() || !mMode) return false;", menu)
        # CategorySettingsTab is the first override; the router contract is
        # checked directly because both classes intentionally share a signature.
        self.assertIn("if (child->back())", menu)
        self.assertIn("mNavInput.begin(JUTGamePad::B);", menu)
        self.assertIn("static_assert(sizeof(CategorySettingsTab) == 8", menu)
        self.assertIn("const SettingPage kTimerPages[]", menu)
        self.assertIn("const SettingPage kRngPages[]", menu)
        self.assertIn("const SettingPage kGameplayPages[]", menu)
        self.assertIn("const SettingPage kDisplayPages[]", menu)
        self.assertIn("const SettingPage kAppearancePages[]", menu)

    def test_settings_hub_is_grouped_coherently(self) -> None:
        menu = text("src/menu.cpp")
        self.assertIn("gameplay, practice, rng, savestate,", menu)
        self.assertIn("timer, display, cosmetics, creation, binds,", menu)
        self.assertIn('return "GAMEPLAY & PRACTICE"', menu)
        self.assertIn('return "TIMING & HUD"', menu)
        self.assertIn('return "LAYOUT & CONTROLS"', menu)
        self.assertIn('return "Layout editor"', menu)
        self.assertIn('return "Button binds"', menu)
        self.assertIn("gInputDisplay.editing()", menu)


class ModalInputContracts(unittest.TestCase):
    def test_retail_pad_is_suppressed_only_around_direct(self) -> None:
        main = text("src/main.cpp")
        suppress = function(main, r"void suppressRetailPad\([^)]*\)")
        restore = function(main, r"void restoreRetailPad\([^)]*\)")
        for field in (
            "mInput",
            "mFrameInput",
            "_8",
            "mRapidInput",
            "mMeaning",
            "mFrameMeaning",
            "_D8",
        ):
            self.assertIn(field, suppress)
            self.assertIn(field, restore)
        self.assertIn("menuOpenBeforeDirect ||", main)
        self.assertIn("gBinds.wasPressed(BIND_MENU_TOGGLE)", main)
        mute = main.index("suppressRetailPad(retailPad, retailInput);")
        direct = main.index("int state = director->direct();", mute)
        unmute = main.index("restoreRetailPad(retailPad, retailInput);", direct)
        self.assertLess(mute, direct)
        self.assertLess(direct, unmute)
        self.assertNotIn("JUTGamePad::mPadStatus", suppress)


class MovementStylePersistenceContracts(unittest.TestCase):
    def test_append_only_payload_stays_before_fixed_mailboxes(self) -> None:
        cfg = text("include/susamune/susamune_cfg.h")
        self.assertIn("SUSAMUNE_CFG_FLAG_MOVEMENT_STYLE 0x4000u", cfg)
        self.assertIn("sizeof(struct SusamuneMovementStyleCfg) == 88", cfg)
        self.assertIn("movementStyle) == 4928", cfg)
        self.assertIn("sizeof(struct SusamuneCfg) == 5016", cfg)
        self.assertIn("SUSAMUNE_STAGE_PLAYLIST_CFG_OFFSET 0x1400u", cfg)
        self.assertLess(5016, 0x1400)

    def test_console_save_and_reload_cover_every_field(self) -> None:
        kernel = text("launcher/kernel/SusamuneCfg.c")
        for prefix in ("rollout", "dust"):
            self.assertIn(f'&cfg->movementStyle.{prefix}', kernel)
            for field in (
                "x",
                "y",
                "scale",
                "text_alpha",
                "background_rgb",
                "background_alpha",
                "text_brightness",
                "padding",
            ):
                self.assertIn(f'"%s_{field}', kernel)
                self.assertIn(f'strcmp(field, "{field}")', kernel)
        self.assertIn('"%s_%u_rgb = %u,%u,%u', kernel)
        self.assertIn('_sprintf(expected, "%u_rgb", i + 1)', kernel)
        self.assertIn("ApplyMovementStyleKey(&cfg->movementStyle", kernel)
        self.assertIn("InitMovementStyleDefaults(&cfg->movementStyle)", kernel)
        self.assertIn("sync_before_read(&cfg->movementStyle", kernel)

    def test_mod_adopts_stages_and_flushes_optional_tail(self) -> None:
        settings = text("src/settings.cpp")
        self.assertIn("SUSAMUNE_CFG_FLAG_MOVEMENT_STYLE", settings)
        self.assertIn("gCreationExtras.adoptMovement(&cfg->movementStyle)", settings)
        self.assertIn("gCreationExtras.stageMovementInto(&cfg->movementStyle)", settings)
        self.assertIn("DCStoreRange((void *)&cfg->movementStyle", settings)

    def test_dolphin_v4_migrates_to_v5(self) -> None:
        emulator = text("src/emulator_persistence.cpp")
        self.assertIn("constexpr u16 kRecordVersion = 5;", emulator)
        self.assertIn("struct RecordV4", emulator)
        self.assertIn("u8 cfg[4928];", emulator)
        self.assertIn("bool validV4", emulator)
        self.assertIn("const bool v4 = !current && validV4(record);", emulator)
        self.assertIn("v4 ? sizeof(((RecordV4 *)0)->cfg)", emulator)
        self.assertIn("if (v1 || v2 || v3) migrateLegacyPBs", emulator)
        self.assertNotIn("if (!current) migrateLegacyPBs", emulator)


if __name__ == "__main__":
    unittest.main()
