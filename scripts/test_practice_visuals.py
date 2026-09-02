"""Host contracts for global practice markers and HUD collision fixes."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PracticeVisualContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.setting_ids = (ROOT / "include/susamune/settings_list.h").read_text()
        cls.descs = (ROOT / "src/settings_descs.inc").read_text()
        cls.settings = (ROOT / "src/settings.cpp").read_text()
        cls.menu = (ROOT / "src/menu.cpp").read_text()
        cls.source = (ROOT / "src/practice_visuals.cpp").read_text()
        cls.main = (ROOT / "src/main.cpp").read_text()
        cls.patches = (ROOT / "scripts/patches.py").read_text()
        cls.mem2 = (ROOT / "include/susamune/mem2_map.h").read_text()
        cls.addresses = (ROOT / "include/susamune/addresses.hxx").read_text()

    def test_existing_values_keep_their_pr5_meanings(self) -> None:
        rows = re.findall(r"X\((SETTING_[A-Z0-9_]+),", self.setting_ids)
        self.assertLess(rows.index("SETTING_PINNA_HIDDEN_ITEMS"),
                        rows.index("SETTING_RNG_FAVORITES"))
        self.assertLess(rows.index("SETTING_ENEMY_HURTBOXES"),
                        rows.index("SETTING_RNG_FAVORITES"))
        self.assertEqual(rows[-3:], [
            "SETTING_HIDDEN_ITEM_LABELS",
            "SETTING_HURTBOX_TARGET",
            "SETTING_RICCO_RACE_CHECKPOINTS",
        ])
        self.assertIn("0, 45, 46, 47,       // hidden items", self.settings)
        self.assertIn("0, 48, 49, 50,       // hurtbox mode", self.settings)
        self.assertIn(
            'SCHOICE("Hidden fruit and coins", 0, CHOICES_HIDDEN_ITEMS, '
            'SETTING_CAT_UI)', self.descs)
        self.assertIn(
            'SCHOICE("Enemy hurtboxes", 0, CHOICES_HURTBOX_MODE, '
            'SETTING_CAT_UI)', self.descs)
        self.assertIn(
            'SBOOL("Hidden item labels", 1, SETTING_CAT_UI)', self.descs)
        self.assertIn("SUSAMUNE_CONFIG_SETTINGS_SIZE         0x00000088u",
                      self.mem2)
        self.assertIn("SUSAMUNE_CONFIG_BINDS_OFFSET          0x00000088u",
                      self.mem2)
        self.assertIn("SUSAMUNE_CONFIG_BINDS_SIZE            0x00000038u",
                      self.mem2)
        self.assertIn("SUSAMUNE_CONFIG_INPUT_OFFSET          0x000000C0u",
                      self.mem2)

    def test_visual_rows_are_grouped_in_other_hud(self) -> None:
        other_hud = self.menu.split("const u8 kDisplayOtherSettings[]", 1)[1]
        other_hud = other_hud.split("};", 1)[0]
        for setting in (
            "SETTING_PINNA_HIDDEN_ITEMS",
            "SETTING_HIDDEN_ITEM_LABELS",
            "SETTING_ENEMY_HURTBOXES",
            "SETTING_HURTBOX_TARGET",
            "SETTING_RICCO_RACE_CHECKPOINTS",
        ):
            self.assertIn(setting, other_hud)

    def test_hidden_display_is_global_and_filters_fruit_and_coins(self) -> None:
        self.assertNotIn("TGameSequence::AREA_PINNABEACH", self.source)
        self.assertIn('strcmp(actor->mRegisterName, "WaterHitHideObj")',
                      self.source)
        self.assertIn('strcmp(actor->mRegisterName, "MapSmoke")', self.source)
        self.assertIn("hide->mAllowReveal", self.source)
        self.assertIn("hide->mHiddenObj", self.source)
        self.assertIn("HIDDEN_ITEMS_FRUIT", self.source)
        self.assertIn("HIDDEN_ITEMS_COINS", self.source)
        self.assertIn("SETTING_HIDDEN_ITEM_LABELS", self.source)
        self.assertIn("if (labels)", self.source)
        for label in ("COIN", "COCONUT", "PAPAYA", "PINEAPPLE", "DURIAN",
                      "BANANA", "PEPPER"):
            self.assertIn(f'"{label}"', self.source)

    def test_hurtbox_modes_keep_eely_teeth_useful(self) -> None:
        self.assertIn("HURTBOX_WIREFRAME", self.source)
        self.assertIn("HURTBOX_TRANSPARENT", self.source)
        self.assertIn("HURTBOX_SOLID", self.source)
        self.assertIn("menu->fillPoly(lower, 8, color)", self.source)
        self.assertIn("actor->mReceiveRadius", self.source)
        self.assertIn("actor->mReceiveHeight", self.source)
        self.assertNotIn("actor->mAttackRadius", self.source)
        self.assertIn("HURTBOX_EELY_TEETH", self.source)
        self.assertIn("actor->mObjectID != kEelTooth", self.source)
        self.assertIn("SUSAMUNE_VT_BOSS_EEL", self.source)
        self.assertIn("SUSAMUNE_VT_BOSS_EEL_BODY_COLLISION", self.source)
        self.assertIn("0x803dd4f4u, 0x803b8e1cu, 0x803b0c3cu",
                      self.addresses)
        self.assertIn("0x803dd80cu, 0x803b9134u, 0x803b0f54u",
                      self.addresses)

    def test_ricco_2_draws_the_live_retail_checkpoint_cubes(self) -> None:
        self.assertIn("TGameSequence::AREA_RICOEX0", self.source)
        self.assertIn("gpCubeFastA", self.source)
        self.assertIn("gpCubeFastB", self.source)
        self.assertIn("gpCubeFastC", self.source)
        self.assertIn("manager->getCubeInfo<TCubeGeneralInfo>()", self.source)
        self.assertIn("info->mChildren.begin()", self.source)
        self.assertIn('char label[] = "CP A"', self.source)

    def test_visual_assists_invalidate_before_retail_finishes(self) -> None:
        self.assertIn("ILing::invalidateForAssist()", self.source)
        direct = self.main.index("int state = director->direct();")
        self.assertLess(self.main.index("PracticeVisuals::update();"), direct)
        self.assertIn("SETTING_RICCO_RACE_CHECKPOINTS", self.source)

    def test_pinna_balloon_shift_brackets_the_retail_hud_draw(self) -> None:
        draw = self.source.split("void drawHudScreen", 1)[1]
        draw = draw.split("}\n", 1)[0]
        self.assertLess(draw.index("shiftPinnaBalloonPanel(screen)"),
                        draw.index("screen->draw(x, y, context)"))
        self.assertGreater(draw.index("shifted->add(0, -kBalloonPanelShift)"),
                           draw.index("screen->draw(x, y, context)"))
        self.assertIn("balloon->add(0, kBalloonPanelShift)", self.source)
        self.assertIn("TGameSequence::AREA_PINNABOSS", self.source)
        self.assertIn("screen->search('\\0b_0')", self.source)
        self.assertIn("screen->search('\\0t_0')", self.source)
        self.assertIn("'jp': 0x80206770", self.patches)
        self.assertIn("'us': 0x80143f50", self.patches)
        self.assertIn("'pal': 0x80138b8c", self.patches)
        self.assertIn("'sym': 'susamuneDrawHudScreen'", self.patches)
        qft = (ROOT / "src/qft_timer.cpp").read_text()
        self.assertNotIn("kBalloonPanelShift", qft)

    def test_visuals_are_drawn_only_over_live_gameplay(self) -> None:
        self.assertIn("TApplication::CONTEXT_DIRECT_STAGE", self.source)
        self.assertIn("gpMarDirector->_260 == 0", self.source)
        self.assertIn("TMarDirector::STATE_GAME_STARTING", self.source)
        self.assertIn("PracticeVisuals::draw(gMenu)", self.main)
        self.assertIn("!WarpWheel::shown()", self.main)


if __name__ == "__main__":
    unittest.main()
