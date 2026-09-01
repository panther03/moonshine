"""Host contracts for the Pinna markers and enemy hurtbox display."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PracticeVisualContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.setting_ids = (ROOT / "include/susamune/settings_list.h").read_text()
        cls.descs = (ROOT / "src/settings_descs.inc").read_text()
        cls.menu = (ROOT / "src/menu.cpp").read_text()
        cls.source = (ROOT / "src/practice_visuals.cpp").read_text()
        cls.main = (ROOT / "src/main.cpp").read_text()
        cls.mem2 = (ROOT / "include/susamune/mem2_map.h").read_text()
        cls.addresses = (ROOT / "include/susamune/addresses.hxx").read_text()

    def test_settings_are_append_only_persisted_ui_rows(self) -> None:
        self.assertIn('SETTING_PINNA_HIDDEN_ITEMS,            "pinna_hidden_items"',
                      self.setting_ids)
        self.assertIn('SETTING_ENEMY_HURTBOXES,               "enemy_hurtboxes"',
                      self.setting_ids)
        self.assertLess(self.setting_ids.index("SETTING_GELATO_BLUE_BIRD_PATTERN"),
                        self.setting_ids.index("SETTING_PINNA_HIDDEN_ITEMS"))
        self.assertIn('SBOOL("Pinna fruit and coins", 0, SETTING_CAT_UI)',
                      self.descs)
        self.assertIn('SBOOL("Enemy hurtboxes", 0, SETTING_CAT_UI)', self.descs)
        other_hud = self.menu.split("const u8 kDisplayOtherSettings[]", 1)[1]
        other_hud = other_hud.split("};", 1)[0]
        self.assertIn("SETTING_PINNA_HIDDEN_ITEMS", other_hud)
        self.assertIn("SETTING_ENEMY_HURTBOXES", other_hud)
        self.assertIn("SUSAMUNE_CONFIG_SETTINGS_SIZE         0x00000084u",
                      self.mem2)
        self.assertIn("SUSAMUNE_CONFIG_BINDS_OFFSET          0x00000084u",
                      self.mem2)

    def test_pinna_display_only_marks_unrevealed_spray_items(self) -> None:
        self.assertIn("TGameSequence::AREA_PINNABEACH", self.source)
        self.assertIn('strcmp(actor->mRegisterName, "WaterHitHideObj")',
                      self.source)
        self.assertIn('strcmp(actor->mRegisterName, "MapSmoke")', self.source)
        self.assertIn("hit->mObjectID != kHideObject", self.source)
        self.assertIn("hide->mAllowReveal", self.source)
        self.assertIn("hide->mHiddenObj", self.source)
        self.assertIn("hide->mObjectType & kNoCollision", self.source)
        self.assertIn("sizeof(THideObjBase) == 0x150", (
            ROOT / "include/SMS/MapObj/MapObjHide.hxx").read_text())
        for label in ("COIN", "COCONUT", "PAPAYA", "PINEAPPLE", "DURIAN",
                      "BANANA"):
            self.assertIn(f'"{label}"', self.source)

    def test_hurtboxes_use_retail_receive_cylinders(self) -> None:
        self.assertIn("actor->mReceiveRadius", self.source)
        self.assertIn("actor->mReceiveHeight", self.source)
        self.assertNotIn("actor->mAttackRadius", self.source)
        self.assertNotIn("actor->mAttackHeight", self.source)
        self.assertIn("kNoCollision | kCannotReceive", self.source)
        self.assertIn("gpStrategy->mObjectGroup", self.source)
        self.assertIn("gpStrategy->mEnemyGroup", self.source)
        self.assertIn("gpStrategy->mBossGroup", self.source)
        self.assertIn("kBossActor | kEnemyActor", self.source)
        self.assertIn("actor->mObjectID == kYoshiTongue", self.source)
        self.assertIn("actor->mObjectID == kEelTooth", self.source)
        self.assertIn("+ 0x70) <= 1", self.source)
        self.assertIn("SUSAMUNE_VT_POIHANA_COLLISION", self.source)
        self.assertIn("0x803dbd78u, 0x803b7530u, 0x803af350u",
                      self.addresses)
        self.assertIn("if (allValid)", self.source)
        self.assertIn("lowerValid[i] && lowerValid[next]", self.source)

    def test_visuals_are_drawn_only_over_live_gameplay(self) -> None:
        self.assertIn("TApplication::CONTEXT_DIRECT_STAGE", self.source)
        self.assertIn("gpMarDirector->_260 == 0", self.source)
        self.assertIn("TMarDirector::STATE_GAME_STARTING", self.source)
        self.assertIn("PracticeVisuals::draw(gMenu)", self.main)
        self.assertIn("!WarpWheel::shown()", self.main)


if __name__ == "__main__":
    unittest.main()
