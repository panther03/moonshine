import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "savestate.cpp").read_text(encoding="utf-8")
ADDRESSES = (ROOT / "include" / "susamune" / "addresses.hxx").read_text(
    encoding="utf-8"
)


class SavestateStaticRegionTests(unittest.TestCase):
    def test_every_region_captures_all_safe_static_runs(self):
        expected = {
            "jp": (
                ("80408dc0", "80408fc0"), ("80408fe8", "804097ac"),
                ("8040a208", "8040a268"), ("8040a290", "8040a318"),
                ("8040a348", "8040a4b0"), ("8040a4d0", "8040b45c"),
            ),
            "us": (
                ("803e9750", "803e9b10"), ("803efcb0", "803fd490"),
                ("8040c1e8", "8040cc00"), ("8040cf00", "8040cf08"),
                ("8040cfd0", "8040d058"), ("8040d0a8", "8040e1e8"),
                ("8040e220", "8040e228"),
            ),
            "pal": (
                ("803e1110", "803e14d0"), ("803e7670", "803f4c30"),
                ("80403988", "80404360"), ("80404660", "80404668"),
                ("80404730", "804047b8"), ("80404808", "804058c0"),
                ("804058f8", "80405900"),
            ),
        }
        layout = (SOURCE + ADDRESSES).lower()
        for region, ranges in expected.items():
            link_map = (ROOT / "maps" / f"{region}.map").read_text(encoding="utf-8")
            for start, end in ranges:
                self.assertIn(start, link_map)
                self.assertIn(end, link_map)
                self.assertIn(f"0x{start}u", layout)
                self.assertIn(f"0x{end}u", layout)

    def test_audio_and_thp_are_explicit_holes(self):
        for marker in (
            "before MSoundMainSide", "after MSoundMainSide",
            "before MSound.a", "after MSound.a", "after THP",
            "before JAL audio lists", "after JAL audio lists",
        ):
            self.assertIn(marker, SOURCE)

    def test_snapshot_layout_version_was_bumped(self):
        version = re.search(r"kSnapshotVersion\s*=\s*(\d+)u", SOURCE)
        self.assertIsNotNone(version)
        self.assertGreaterEqual(int(version.group(1)), 12)
        self.assertIn("const u32 kHeaderSize      = 0x120u;", SOURCE)

    def test_load_validates_the_complete_region_manifest_before_copying(self):
        self.assertIn("bool validSnapshotRegions(", SOURCE)
        validator = SOURCE[
            SOURCE.index("bool validSnapshotRegions(") :
            SOURCE.index("// ---------------------------------------------------------------------\n// Hardware audio mute")
        ]
        self.assertIn("h->region_count == 0", validator)
        self.assertIn("h->region_count > (u32)kMaxRegions", validator)
        self.assertIn("heapStart < 0x80000000u", validator)
        self.assertIn("heapEnd > 0x81800000u", validator)
        self.assertIn("consumeExpectedRegion", validator)
        self.assertIn("target < 0x80000000u", validator)
        self.assertIn("target >= 0x81800000u", validator)
        self.assertIn("index == h->region_count", validator)

        check = SOURCE.index("if (!validSnapshotRegions(")
        restore = SOURCE.index("for (u32 i = 0; i < h->region_count; i++)")
        self.assertLess(check, restore)
        self.assertIn('feedback("E:badsnap", "Savestate is damaged - save again")',
                      SOURCE)

    def test_save_validates_heap_and_root_allocations_before_copying(self):
        save_at = SOURCE.index("bool SavestateManager::saveState()")
        load_at = SOURCE.index("bool SavestateManager::loadState()")
        save = SOURCE[save_at:load_at]
        first_copy = save.index("captureRegion(")
        self.assertIn("heapStart < 0x80000000u", save[:first_copy])
        self.assertIn("heapEnd > 0x81800000u", save[:first_copy])
        self.assertIn("alloc.size <= heapEnd - target", save[:first_copy])
        self.assertIn("alloc.size > 0x81800000u - target", save[:first_copy])
        self.assertIn(
            'feedback("E:rootptr", "Stage layout changed - save again")',
            save[:first_copy],
        )
        self.assertNotIn("target + pa.size <= heapEnd", save)

    def test_heap_object_is_validated_before_dereferencing_its_end(self):
        save_at = SOURCE.index("bool SavestateManager::saveState()")
        load_at = SOURCE.index("bool SavestateManager::loadState()")
        update_at = SOURCE.index("void SavestateManager::updateHook()")
        for body in (SOURCE[save_at:load_at], SOURCE[load_at:update_at]):
            start_guard = body.index(
                "heapStart > 0x81800000u - sizeof(JKRHeap)"
            )
            end_read = body.index("heap->mEnd")
            self.assertLess(start_guard, end_read)

    def test_card_busy_loads_wait_in_rendered_frames(self):
        process_at = SOURCE.index("void SavestateManager::processPendingLoad()")
        process = SOURCE[process_at:SOURCE.index(
            "void SavestateManager::draw(Menu *menu)", process_at
        )]
        self.assertIn("++mLoadWaitFrames < 600", process)
        self.assertIn("CARD_ERROR_BUSY", process)
        self.assertLess(process.index("CARD_ERROR_BUSY"),
                        process.index("mLoadPending = false;"))
        self.assertNotIn("OSYieldThread", SOURCE)

        update_at = SOURCE.index("void SavestateManager::updateHook()")
        update = SOURCE[update_at:process_at]
        self.assertLess(update.index("WarpWheel::takeSavestateLoadApproval()"),
                        update.index("if (mLoadPending) return;"))
        self.assertLess(update.index("if (mLoadPending) return;"),
                        update.index("saveState();"))


if __name__ == "__main__":
    unittest.main()
