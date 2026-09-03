#!/usr/bin/env python3
"""Static contracts for PB Safety and achievement unlock banners."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def braced_block(source: str, anchor: str) -> str:
    """Return the balanced brace block that follows *anchor*."""
    anchor_at = source.find(anchor)
    if anchor_at < 0:
        raise AssertionError(f"anchor not found: {anchor}")
    start = source.find("{", anchor_at + len(anchor))
    if start < 0:
        raise AssertionError(f"opening brace not found after: {anchor}")
    depth = 0
    for end in range(start, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start : end + 1]
    raise AssertionError(f"unterminated brace block after: {anchor}")


class PbSafetyMetadataContracts(unittest.TestCase):
    def test_exact_settings_and_safe_values(self) -> None:
        settings = text("src/settings.cpp")
        table = braced_block(settings, "const IlPbSetting kIlPbSettings[] =")
        entries = re.findall(
            r"\{\s*(SETTING_[A-Z0-9_]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}",
            table,
        )
        self.assertEqual(
            entries,
            [
                ("SETTING_ILING_RECORDING", "1", "0"),
                ("SETTING_STAGE_INTRO_SKIP", "0", "1"),
                ("SETTING_KING_BOO_ALWAYS_FRUIT", "0", "1"),
                ("SETTING_PETEY_NO_TORNADO", "0", "1"),
                ("SETTING_PETEY_ROUTE", "0", "1"),
                ("SETTING_PINNA_HIDDEN_ITEMS", "0", "1"),
                ("SETTING_ENEMY_HURTBOXES", "0", "1"),
                ("SETTING_RICCO_RACE_CHECKPOINTS", "0", "1"),
            ],
        )

        header = text("include/susamune/settings.hxx")
        for declaration in (
            "static int ilPbSettingCount();",
            "static SettingId ilPbSettingAt(int index);",
            "static u8 ilPbSafeValue(SettingId id);",
            "static bool affectsIlPb(SettingId id);",
            "bool blocksIlPb(SettingId id) const;",
            "int ilPbBlockerCount() const;",
        ):
            self.assertIn(declaration, header)

        blocker_count = braced_block(
            settings, "int Settings::ilPbBlockerCount() const"
        )
        self.assertIn("for (int i = 0; i < ilPbSettingCount(); i++)", blocker_count)
        self.assertIn("blocksIlPb((SettingId)kIlPbSettings[i].id)", blocker_count)


class PbSafetyMenuContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.menu = text("src/menu.cpp")
        cls.page = braced_block(cls.menu, "class PBSafetyTab final")

    def test_page_is_first_and_reports_ready_or_blocked(self) -> None:
        self.assertIn('return "PB Safety";', self.page)
        self.assertIn("return gSettings.ilPbBlockerCount();", self.page)

        children = braced_block(self.menu, "MenuTab *settingsChildren[] =")
        self.assertRegex(children, r"\{\s*pbSafety\s*,")
        self.assertIn('if (child == 0) return "PB STATUS";', self.menu)

        root_draw = braced_block(
            self.menu, "void draw(Menu *menu, int x, int y, int w, int h) override"
        )
        # There are several draw overrides, so inspect the nested-menu logic by
        # its unique alert-count block instead of depending on class order.
        alert_at = self.menu.find("const int alertCount = available")
        self.assertGreaterEqual(alert_at, 0)
        alert_render = self.menu[alert_at : alert_at + 900]
        self.assertIn("if (alertCount == 0)", alert_render)
        self.assertIn('value = "Ready";', alert_render)
        self.assertIn('snprintf(alert, sizeof(alert), "%d blocked", alertCount);', alert_render)
        self.assertIn("valueColor = col(255, 72, 72, 255);", alert_render)
        self.assertIn("drawValueRowColored", alert_render)
        self.assertTrue(root_draw)  # balanced-method sanity check

    def test_fix_one_and_fix_all_controls(self) -> None:
        self.assertIn("if (rapid & TMarioGamePad::X)", self.page)
        self.assertIn("for (int i = 0; i < count; i++)", self.page)
        self.assertIn("if (!gSettings.blocksIlPb(id)) continue;", self.page)
        self.assertIn("restart |= Settings::invalidatesIlAttempt(id);", self.page)
        self.assertIn(
            "gSettings.set(id, Settings::ilPbSafeValue(id));", self.page
        )
        self.assertIn('"PB settings fixed - restart the IL"', self.page)
        self.assertIn(': "PB settings fixed"', self.page)

        self.assertIn("else if (rapid & TMarioGamePad::A)", self.page)
        self.assertIn("const SettingId id = Settings::ilPbSettingAt(mSel);", self.page)
        self.assertIn("Settings::invalidatesIlAttempt(id)", self.page)
        self.assertIn('"PB setting fixed', self.page)
        self.assertIn('"Setting is already PB-safe"', self.page)
        self.assertIn(
            'SUSAMUNE_GLYPH_A " Fix   " SUSAMUNE_GLYPH_X\n'
            '                       " Fix all"',
            self.page,
        )
        self.assertNotIn('SUSAMUNE_GLYPH_B " Back"', self.page)

    def test_normal_setting_rows_mark_pb_status(self) -> None:
        marker_at = self.menu.find("const bool pbSetting = i < settings")
        self.assertGreaterEqual(marker_at, 0)
        marker = self.menu[marker_at : marker_at + 1000]
        self.assertIn("Settings::affectsIlPb((SettingId)ids[i])", marker)
        self.assertIn("gSettings.blocksIlPb((SettingId)ids[i])", marker)
        self.assertIn('const char *tag = pbBlocked ? "PB OFF" : "PB";', marker)
        self.assertIn("pbBlocked ? col(255, 72, 72, 255) : cValue()", marker)
        self.assertIn("pbBlocked ? col(255, 72, 72, 255) : cRowDim()", marker)


class AchievementPopupCategoryContracts(unittest.TestCase):
    def test_popup_shows_category_and_tier(self) -> None:
        menu = text("src/menu.cpp")
        banner = braced_block(menu, "void drawAchievementBanner(Menu *menu)")
        self.assertIn("Records::Tier tier = Records::TIER_GOLD;", banner)
        self.assertIn(
            "Records::Category category = Records::CATEGORY_TIMES;", banner
        )
        self.assertIn("tier = desc->tier;", banner)
        self.assertIn("category = desc->category;", banner)
        self.assertIn("const char *tierName = recordTierName(tier);", banner)
        self.assertIn(
            "const char *categoryName = recordText(Records::categoryName(category));",
            banner,
        )
        self.assertIn('Menu::textWidth(" / ", labelSize)', banner)
        self.assertIn('menu->drawText(" / ", slashX', banner)
        self.assertIn("menu->drawText(tierName, tierX", banner)
        self.assertIn("menu->drawText(categoryName,", banner)
        self.assertIn(
            "slashX - Menu::textWidth(categoryName, labelSize)", banner
        )


if __name__ == "__main__":
    unittest.main()
