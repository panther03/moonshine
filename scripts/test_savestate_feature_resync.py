import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURES = (ROOT / "src" / "features.cpp").read_text(encoding="utf-8")
FEATURES_HXX = (ROOT / "include" / "susamune" / "features.hxx").read_text(
    encoding="utf-8"
)
SAVESTATE = (ROOT / "src" / "savestate.cpp").read_text(encoding="utf-8")


class SavestateFeatureResyncTests(unittest.TestCase):
    def test_snapshot_records_the_only_rewound_patch_features(self):
        self.assertIn("SETTING_FRUIT_NEVER_TIMEOUT", FEATURES)
        self.assertRegex(
            FEATURES,
            r"id == SETTING_FAST_TEXT && addr >= 0x80500000u",
        )
        self.assertIn("u8 featuresSavestateState();", FEATURES_HXX)
        self.assertIn("h->feature_state = featuresSavestateState();", SAVESTATE)

    def test_restore_uses_saved_state_and_current_effective_setting(self):
        restore = FEATURES[
            FEATURES.index("void restoreSavestateFeatureState(") :
            FEATURES.index("void applyHooks()")
        ]
        self.assertIn("const bool savedOn", restore)
        self.assertIn("const bool wantOn = featureEnabled(id);", restore)
        self.assertIn("if (savedOn == wantOn)", restore)
        self.assertIn("gPatchOrig[originalIndex] = word;", restore)
        self.assertIn("writeGameCode(addr, gPatchOrig[originalIndex]);", restore)

    def test_reconciliation_runs_after_copy_and_before_runtime_callbacks(self):
        copy_at = SAVESTATE.index(
            "for (u32 i = 0; i < h->region_count; i++)"
        )
        resync_at = SAVESTATE.index(
            "featuresOnSavestateLoaded(h->feature_state);"
        )
        iling_at = SAVESTATE.index("ILing::onSavestateLoaded();")
        self.assertLess(copy_at, resync_at)
        self.assertLess(resync_at, iling_at)

    def test_snapshot_layout_version_was_bumped(self):
        version = re.search(r"kSnapshotVersion\s*=\s*(\d+)u", SAVESTATE)
        self.assertIsNotNone(version)
        self.assertGreaterEqual(int(version.group(1)), 13)


if __name__ == "__main__":
    unittest.main()
