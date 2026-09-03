from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "ghost_model.cpp").read_text(encoding="utf-8")


def function_body(signature: str) -> str:
    start = SOURCE.index(signature)
    brace = SOURCE.index("{", start)
    depth = 0
    for index in range(brace, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[brace + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


class GhostModelLifetimeTests(unittest.TestCase):
    def test_setup_thread_only_queues_the_reload(self) -> None:
        body = function_body("void onStageSetup(TMarDirector *director)")
        self.assertIn("sPendingDirector = director", body)
        self.assertIn("sPendingGeneration = generation", body)
        self.assertNotIn("sRegistered", body)
        self.assertNotIn("freeAll", body)
        self.assertNotIn("loadModel", body)
        self.assertNotIn("registerView", body)

    def test_render_thread_quiesces_before_reusing_model_heap(self) -> None:
        body = function_body("void loadPendingStage()")
        quiesce = body.index("sQuiescedGeneration = generation")
        barrier = body.index("GXDrawDone()")
        retire = body.index("retirePlayerDrawBuffers())")
        release = body.index("sAttachmentHeap->freeAll()")
        reload_model = body.index("loadModel(APPEARANCE_SHADOW)")
        self.assertLess(quiesce, barrier)
        self.assertLess(barrier, retire)
        self.assertLess(retire, release)
        self.assertLess(release, reload_model)
        begin = function_body("void beginFrame()")
        self.assertIn("loadPendingStage()", begin)

    def test_mario_draw_buffers_drop_retired_ghost_packets(self) -> None:
        body = function_body("bool retirePlayerDrawBuffers()")
        self.assertIn("gpMarioOriginal->mDrawBufferA->frameInit()", body)
        self.assertIn("gpMarioOriginal->mDrawBufferB->frameInit()", body)
        self.assertIn("return true", body)

    def test_unregistered_view_cannot_submit_retired_packets(self) -> None:
        perform = SOURCE[
            SOURCE.index("virtual void perform(u32 cue"):
            SOURCE.index("alignas(32) u8 sViewStorage")
        ]
        self.assertRegex(perform, r"if \(!sRegistered\) return;")

    def test_packets_are_verified_after_view_calc_and_before_entry(self) -> None:
        self.assertIn("bool modelPacketsReady(const ModelSlot &slot)", SOURCE)
        self.assertIn("kShapePacketDrawMtxOffset = 0x18u", SOURCE)
        self.assertIn("kShapePacketNrmMtxOffset = 0x1Cu", SOURCE)
        perform = SOURCE[
            SOURCE.index("virtual void perform(u32 cue"):
            SOURCE.index("alignas(32) u8 sViewStorage")
        ]
        self.assertRegex(
            perform,
            re.compile(
                r"viewCalc\(\);\s*"
                r"sPrepared\[runner\] = modelPacketsReady\(slot\)",
                re.MULTILINE,
            ),
        )
        self.assertIn(
            "if (!sPrepared[runner] || !modelPacketsReady(slot)) continue;",
            perform,
        )

    def test_partial_model_is_rejected_before_display_lists(self) -> None:
        storage = function_body("bool modelStorageReady(const ModelSlot &slot)")
        for field in (
            "mJointArray",
            "mDrawMtxBuf[0]",
            "mDrawMtxBuf[1]",
            "mNrmMtxBuf[0]",
            "mNrmMtxBuf[1]",
            "mMatPackets",
            "mShapePackets",
            "mVtxBuffer",
        ):
            self.assertIn(field, storage)
        load = function_body("bool loadModel(Appearance appearance)")
        guard = load.index("!modelStorageReady(slot)")
        self.assertLess(guard, load.index("configureOpacity(slot"))
        self.assertLess(guard, load.index("installColorCallbacks(slot"))

    def test_timer_source_is_not_part_of_the_fix(self) -> None:
        self.assertNotIn("qft_timer", SOURCE.lower())


if __name__ == "__main__":
    unittest.main()
