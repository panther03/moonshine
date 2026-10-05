"""Run production state-load queue code with controlled binds, prompts and card IO."""

import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_practice_tape import function_source

ROOT = Path(__file__).resolve().parents[1]


class SavestateQueueTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.folder.cleanup)
        source = r'''
typedef unsigned int u32;
typedef long long OSTime;
extern "C" void *memset(void*d,int c,__SIZE_TYPE__ n){unsigned char*p=(unsigned char*)d;while(n--)*p++=(unsigned char)c;return d;}
extern "C" char *strncpy(char*d,const char*s,__SIZE_TYPE__ n){char*out=d;while(n--)*d++=*s?*s++:0;return out;}
static int snprintf(char*d,__SIZE_TYPE__ n,const char*,...){if(n)*d=0;return 0;}
enum { BIND_SAVESTATE_SAVE, BIND_SAVESTATE_LOAD, BIND_SAVESTATE_CYCLE,
       BIND_ATTEMPT_SHOW, BIND_ATTEMPT_ADD, BIND_SAVESTATE_CYCLE_SAVE,
       BIND_SAVESTATE_CYCLE_LOAD, BIND_COUNT };
enum { SETTING_ATTEMPT_COUNTER, SETTING_ATTEMPT_IN_STAGE_CONTROLS };
enum { CARD_ERROR_BUSY = -1 };
static u32 sActiveSlot, sLoadSlot, sPendingSlot, sPendingGeneration;
static bool sAwaitingLoadApproval, sBusy;
struct Selection {u32 id,headerCrc,packedSize;char name[32];};
static Selection sSelectedSD,sPendingSD;
static bool diskOwned,sDiskLoadReady,sDiskRecovered,restoreSucceeds,importSucceeds;
namespace PracticeSession {
static u32 heldMask,cancelled;
void armLoadHold(u32 mask){heldMask=mask;}
void cancelLoadHold(){heldMask=0;++cancelled;}
}
static OSTime sDiskStarted;
static const char*sDiskStatus;
static u32 imports,importId,importCrc,importSize,rebased,savedSlot;
static void rebaseMissionStopwatch(OSTime){++rebased;}
enum {SUSAMUNE_STATE_MAX_ARCHIVE_ID=9999};
static u32 poolCapacity(){return 100000;}
struct Menu{void toast(const char*){}}menu;static Menu*gMenu=&menu;
static u32 generations[3], saveCalls, loadCalls, cycleCalls, feedbackCalls;
static u32 loadedSlot, loadedGeneration, pendingAtLoad;
struct Settings {
    bool values[2];
    bool getBool(int id) { return values[id]; }
} gSettings;
struct Binds {
    u32 pressed, values[BIND_COUNT];
    u32 get(int id) { return values[id]; }
    bool wasPressed(int id) { return (pressed & (1u << id)) != 0; }
    bool wasPressedSubsetRaw(int id) { return wasPressed(id); }
    bool recording() { return false; }
    void suppressUntilRelease() {}
} gBinds;
struct Card {
    bool busy;
    int getLastStatus() { return busy ? CARD_ERROR_BUSY : 0; }
} card;
static Card *gpCardManager;
namespace WarpWheel {
bool prompt, approved, requestNeedsPrompt;
bool takeSavestateLoadApproval() { bool out = approved; approved = false; return out; }
bool promptPending() { return prompt; }
bool requestSavestateLoad() { prompt = requestNeedsPrompt; return !prompt; }
}
#define SET_STATUS(text) ((void)0)
class SavestateManager {
public:
    enum{kSlotCount=3};
    bool mLoadPending;
    static bool diskBusy() { return diskOwned || sDiskLoadReady; }
    u32 mLoadWaitFrames;
    struct SlotInfo { u32 generation; bool valid; };
    SlotInfo slotInfo(u32 slot) { return { generations[slot],true }; }
    bool saveState() { ++saveCalls; savedSlot=sActiveSlot; return true; }
    bool loadSlot(u32 slot, u32 generation) {
        ++loadCalls; loadedSlot = slot; loadedGeneration = generation;
        pendingAtLoad = mLoadPending; return restoreSucceeds;
    }
    bool cycleSlot() { ++cycleCalls; return selectSlot((sActiveSlot + 1) % 3); }
    bool cycleSaveSlot();bool cycleLoadSlot();
    bool selectSlot(u32);bool selectSaveSlot(u32);bool selectLoadSlot(u32);
    bool selectSDForLoad(u32,u32,u32,const char*);
    bool sdAvailable()const{return true;}
    bool beginSDLoad(u32 id,u32 crc,u32 size,bool restore){
        ++imports;importId=id;importCrc=crc;importSize=size;
        if(importSucceeds)diskOwned=restore;return importSucceeds;}
    void feedback(const char *, const char *) { ++feedbackCalls; }
    void updateHook();
    void processPendingLoad();
} manager;
'''
        path = ROOT / "src/savestate.cpp"
        source += function_source(path, "void SavestateManager::updateHook()")
        source += function_source(path, "void SavestateManager::processPendingLoad()")
        for method in ("selectSlot", "selectSaveSlot", "selectLoadSlot", "cycleSaveSlot",
                       "cycleLoadSlot", "selectSDForLoad"):
            source += function_source(path, f"bool SavestateManager::{method}(")
        source += r'''
#define API extern "C" __declspec(dllexport)
API void reset() {
    sActiveSlot = sLoadSlot = sPendingSlot = sPendingGeneration = 0;
    sAwaitingLoadApproval = sBusy = manager.mLoadPending = false;
    sSelectedSD={};sPendingSD={};sDiskStatus="";
    diskOwned = sDiskLoadReady = sDiskRecovered = false;restoreSucceeds=importSucceeds=true;
    PracticeSession::heldMask=PracticeSession::cancelled=0;
    imports=importId=importCrc=importSize=rebased=savedSlot=0;sDiskStarted=123;
    manager.mLoadWaitFrames = 0;
    saveCalls = loadCalls = cycleCalls = feedbackCalls = 0;
    loadedSlot = loadedGeneration = pendingAtLoad = 0;
    gBinds.pressed = 0;
    for (u32 i = 0; i < BIND_COUNT; ++i) gBinds.values[i] = 1u << i;
    for (u32 i = 0; i < 3; ++i) generations[i] = 101 + i;
    gSettings.values[0] = gSettings.values[1] = false;
    WarpWheel::prompt = WarpWheel::approved = WarpWheel::requestNeedsPrompt = false;
    card.busy = false; gpCardManager = &card;
}
API void press(u32 value) { gBinds.pressed = value; }
API void select(u32 value) { sActiveSlot = sLoadSlot = value; }
API bool selectSave(u32 value) { return manager.selectSaveSlot(value); }
API bool selectLoad(u32 value) { return manager.selectLoadSlot(value); }
API bool selectSD(u32 id) { return manager.selectSDForLoad(id,id+100,id+1000,"named"); }
API void replaceSD(u32 id) { sSelectedSD.id=id;sSelectedSD.headerCrc=id+100;sSelectedSD.packedSize=id+1000; }
API void transferReady(u32 generation) { diskOwned=false;sDiskLoadReady=manager.mLoadPending=true;
    sPendingSlot=3;sPendingGeneration=generation;manager.mLoadWaitFrames=0; }
API void restoreResult(u32 value) { restoreSucceeds=value!=0; }
API void importResult(u32 value) { importSucceeds=value!=0; }
API void generation(u32 slot, u32 value) { generations[slot] = value; }
API void promptMode(u32 value) { WarpWheel::requestNeedsPrompt = value != 0; }
API void approve() { WarpWheel::prompt = false; WarpWheel::approved = true; }
API void cancel() { WarpWheel::prompt = WarpWheel::approved = false; }
API void busy(u32 value) { card.busy = value != 0; }
API void disk(u32 value) { diskOwned = value != 0; }
API void noCard() { gpCardManager = 0; }
API void counter(u32 value) { gSettings.values[0] = gSettings.values[1] = value != 0; }
API void binding(u32 id, u32 value) { gBinds.values[id] = value; }
API void update() { manager.updateHook(); gBinds.pressed = 0; }
API void process() { manager.processPendingLoad(); }
API u32 get(u32 key) {
    switch (key) {
    case 0: return sActiveSlot;
    case 1: return manager.mLoadPending;
    case 2: return sAwaitingLoadApproval;
    case 3: return manager.mLoadWaitFrames;
    case 4: return saveCalls;
    case 5: return loadCalls;
    case 6: return cycleCalls;
    case 7: return feedbackCalls;
    case 8: return loadedSlot;
    case 9: return loadedGeneration;
    case 10: return pendingAtLoad;
    case 11: return sLoadSlot;
    case 12: return sSelectedSD.id;
    case 13: return imports;
    case 14: return importId;
    case 15: return importCrc;
    case 16: return importSize;
    case 17: return sDiskLoadReady;
    case 18: return rebased;
    case 19: return sPendingSD.id;
    case 20: return savedSlot;
    case 21: return SavestateManager::diskBusy();
    case 22: return PracticeSession::heldMask;
    case 23: return PracticeSession::cancelled;
    }
    return 0;
}
'''
        shim = Path(cls.folder.name) / "queue.cpp"
        shim.write_text(source, encoding="ascii")
        library = shim.with_suffix(".dll")
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
                        "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry", "-O2",
                        str(shim), "-o", str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        for name in ("reset", "approve", "cancel", "noCard", "update", "process"):
            getattr(cls.lib, name).argtypes = []
            getattr(cls.lib, name).restype = None
        for name in ("press", "select", "promptMode", "busy", "counter", "disk", "replaceSD",
                     "transferReady", "restoreResult", "importResult"):
            getattr(cls.lib, name).argtypes = [C.c_uint]
            getattr(cls.lib, name).restype = None
        for name in ("selectSave", "selectLoad", "selectSD"):
            getattr(cls.lib,name).argtypes=[C.c_uint]
            getattr(cls.lib,name).restype=C.c_bool
        for name in ("generation", "binding"):
            getattr(cls.lib, name).argtypes = [C.c_uint, C.c_uint]
            getattr(cls.lib, name).restype = None
        cls.lib.get.argtypes = [C.c_uint]
        cls.lib.get.restype = C.c_uint

    def setUp(self):
        self.lib.reset()

    def assert_loaded(self, slot, generation):
        self.assertEqual(self.lib.get(5), 1)
        self.assertEqual(self.lib.get(8), slot)
        self.assertEqual(self.lib.get(9), generation)
        self.assertEqual(self.lib.get(10), 0, "Queue must clear before the restore callback")
        self.lib.process()
        self.assertEqual(self.lib.get(5), 1, "A request must restore only once")

    def test_direct_load_pins_slot_and_generation_until_post_render(self):
        self.lib.select(1)
        self.lib.press(2)
        self.lib.update()
        self.assertEqual(self.lib.get(1), 1)
        self.assertEqual(self.lib.get(5), 0)
        self.lib.select(2)
        self.lib.generation(1, 999)
        self.lib.noCard()
        self.lib.process()
        self.assert_loaded(1, 102)

    def test_load_hold_pins_original_mask_across_prompt_and_sd_wait(self):
        self.lib.binding(1,0x123)
        self.lib.selectSD(73)
        self.lib.promptMode(1)
        self.lib.press(2);self.lib.update()
        self.assertEqual(self.lib.get(22),0x123)
        self.lib.binding(1,0x456)
        self.lib.approve();self.lib.update();self.lib.process()
        self.assertEqual(self.lib.get(22),0x123)
        self.lib.transferReady(18);self.lib.process()
        self.assertEqual(self.lib.get(22),0x123)
        self.assertEqual(self.lib.get(23),0)

    def test_load_hold_is_cancelled_when_prompt_import_or_restore_fails(self):
        for failure in ('prompt','import','restore'):
            with self.subTest(failure=failure):
                self.lib.reset()
                if failure=='prompt':self.lib.promptMode(1)
                if failure=='import':self.lib.selectSD(73);self.lib.importResult(0)
                if failure=='restore':self.lib.restoreResult(0)
                self.lib.press(2);self.lib.update()
                if failure=='prompt':self.lib.cancel();self.lib.update()
                self.lib.process()
                self.assertEqual(self.lib.get(22),0)
                self.assertEqual(self.lib.get(23),1)

    def test_prompt_and_card_wait_keep_original_request(self):
        self.lib.select(2)
        self.lib.promptMode(1)
        self.lib.press(2)
        self.lib.update()
        self.assertEqual(self.lib.get(2), 1)
        self.assertEqual(self.lib.get(1), 0)
        self.lib.select(0)
        self.lib.press(7)
        self.lib.update()
        self.lib.process()
        self.assertEqual([self.lib.get(i) for i in (4, 5, 6)], [0, 0, 0])
        self.lib.approve()
        self.lib.press(7)
        self.lib.update()
        self.assertEqual(self.lib.get(2), 0)
        self.assertEqual(self.lib.get(1), 1)
        self.lib.busy(1)
        for _ in range(8):
            self.lib.press(7)
            self.lib.update()
            self.lib.process()
        self.assertEqual(self.lib.get(3), 8)
        self.assertEqual([self.lib.get(i) for i in (4, 5, 6)], [0, 0, 0])
        self.lib.select(1)
        self.lib.busy(0)
        self.lib.process()
        self.assert_loaded(2, 103)

    def test_cancelled_prompt_cannot_load_later_or_block_new_save(self):
        self.lib.promptMode(1)
        self.lib.press(2)
        self.lib.update()
        self.lib.cancel()
        self.lib.update()
        self.lib.process()
        self.assertEqual([self.lib.get(i) for i in (1, 2, 5)], [0, 0, 0])
        self.lib.approve()  # A stale approval has no owning request.
        self.lib.press(1)
        self.lib.update()
        self.lib.process()
        self.assertEqual(self.lib.get(4), 1)
        self.assertEqual(self.lib.get(5), 0)

    def test_card_timeout_drops_request_exactly_at_600_rendered_frames(self):
        self.lib.press(2)
        self.lib.update()
        self.lib.busy(1)
        for _ in range(599):
            self.lib.process()
        self.assertEqual(self.lib.get(1), 1)
        self.assertEqual(self.lib.get(7), 0)
        self.lib.process()
        self.assertEqual([self.lib.get(i) for i in (1, 3, 5, 7)], [0, 0, 0, 1])
        self.assertEqual(self.lib.get(22),0)
        self.assertEqual(self.lib.get(23),1)
        self.lib.busy(0)
        self.lib.process()
        self.assertEqual(self.lib.get(5), 0)

    def test_collision_priority_is_save_then_load_then_cycle(self):
        for pressed, calls in ((7, (1, 0, 0)), (6, (0, 1, 0)), (4, (0, 0, 1))):
            with self.subTest(pressed=pressed):
                self.lib.reset()
                self.lib.press(pressed)
                self.lib.update()
                self.lib.process()
                self.assertEqual(tuple(self.lib.get(i) for i in (4, 5, 6)), calls)

    def test_attempt_counter_owns_only_nonzero_colliding_binds(self):
        self.lib.counter(1)
        self.lib.binding(3, 1)  # Counter Show owns Save's combo.
        self.lib.press(7)
        self.lib.update()
        self.lib.process()
        self.assertEqual([self.lib.get(i) for i in (4, 5, 6)], [0, 1, 0])
        self.lib.reset()
        self.lib.counter(1)
        self.lib.binding(4, 2)  # Counter Add owns Load's combo.
        self.lib.press(6)
        self.lib.update()
        self.lib.process()
        self.assertEqual([self.lib.get(i) for i in (4, 5, 6)], [0, 0, 1])
        self.lib.reset()
        self.lib.counter(1)
        self.lib.binding(0, 0)
        self.lib.binding(3, 0)
        self.lib.press(1)
        self.lib.update()
        self.assertEqual(self.lib.get(4), 1)

    def test_disk_ownership_blocks_new_binds_and_preserves_a_queued_load(self):
        self.lib.disk(1)
        self.lib.press(7); self.lib.update(); self.lib.process()
        self.assertEqual([self.lib.get(i) for i in (4, 5, 6)], [0, 0, 0])
        self.lib.disk(0); self.lib.press(2); self.lib.update()
        self.lib.disk(1); self.lib.process()
        self.assertEqual([self.lib.get(i) for i in (1, 3, 5)], [1, 0, 0])
        self.lib.disk(0); self.lib.process()
        self.assert_loaded(0, 101)

    def test_save_load_and_cycle_binds_use_independent_sources(self):
        self.assertTrue(self.lib.selectSave(2))
        self.assertTrue(self.lib.selectLoad(1))
        self.lib.press(1); self.lib.update()
        self.assertEqual(self.lib.get(20), 2)
        self.lib.press(2); self.lib.update(); self.lib.process()
        self.assert_loaded(1, 102)
        self.lib.press(1 << 5); self.lib.update()
        self.assertEqual([self.lib.get(i) for i in (0, 11)], [0, 1])
        self.lib.selectSD(51)
        self.lib.press(1 << 5); self.lib.update()
        self.assertEqual([self.lib.get(i) for i in (0, 11, 12)], [1, 1, 51])
        self.lib.press(1 << 6); self.lib.update()
        self.assertEqual([self.lib.get(i) for i in (0, 11, 12)], [1, 2, 0])
        self.lib.selectSD(52)
        self.lib.press(4); self.lib.update()
        self.assertEqual([self.lib.get(i) for i in (0, 11, 12)], [2, 2, 0])

    def test_selection_refuses_bounds_and_queued_or_disk_owned_changes(self):
        for slot in (3, 0xffffffff):
            self.assertFalse(self.lib.selectSave(slot))
            self.assertFalse(self.lib.selectLoad(slot))
        for selected in (0, 10000):
            self.assertFalse(self.lib.selectSD(selected))
        self.lib.selectSD(41)
        self.lib.press(2); self.lib.update()
        self.assertFalse(self.lib.selectSave(1))
        self.assertFalse(self.lib.selectLoad(1))
        self.assertFalse(self.lib.selectSD(42))
        self.lib.process()
        self.assertFalse(self.lib.selectSave(1))
        self.assertFalse(self.lib.selectLoad(1))
        self.assertFalse(self.lib.selectSD(42))
        self.assertEqual([self.lib.get(i) for i in (0, 11, 12)], [0, 0, 41])

    def test_sd_source_is_pinned_through_prompt_and_read_starts_only_post_draw(self):
        self.lib.selectSD(51); self.lib.promptMode(1)
        self.lib.press(2); self.lib.update()
        self.assertEqual(self.lib.get(13), 0)
        self.lib.replaceSD(99)  # Simulate a changed catalog/selection after confirmation opened.
        self.lib.approve(); self.lib.update()
        self.assertEqual(self.lib.get(13), 0)
        self.lib.process()
        self.assertEqual([self.lib.get(i) for i in (5, 13, 14, 15, 16, 19)],
                         [0, 1, 51, 151, 1051, 0])
        self.lib.press(127); self.lib.update(); self.lib.process()
        self.assertEqual([self.lib.get(i) for i in (4, 5, 6, 13)], [0, 0, 0, 1])
        self.lib.transferReady(876)
        self.lib.process()
        self.assert_loaded(3, 876)
        self.assertEqual([self.lib.get(i) for i in (17, 18, 21)], [0, 0, 0])

    def test_failed_sd_start_is_not_retried_and_cancelled_prompt_does_not_read(self):
        self.lib.selectSD(51); self.lib.importResult(0)
        self.lib.press(2); self.lib.update(); self.lib.process(); self.lib.process()
        self.assertEqual([self.lib.get(i) for i in (1, 5, 13, 19, 21)], [0, 0, 1, 0, 0])
        self.lib.reset(); self.lib.selectSD(51); self.lib.promptMode(1)
        self.lib.press(2); self.lib.update(); self.lib.cancel(); self.lib.update()
        self.lib.process()
        self.assertEqual([self.lib.get(i) for i in (1, 2, 5, 13)], [0, 0, 0, 0])

    def test_sd_ready_waits_for_card_and_releases_storage_on_timeout_or_failed_restore(self):
        for failure in (False, True):
            with self.subTest(failed_restore=failure):
                self.lib.reset(); self.lib.transferReady(456); self.lib.busy(1)
                self.lib.restoreResult(not failure)
                for _ in range(7): self.lib.process()
                self.assertEqual([self.lib.get(i) for i in (1, 3, 5, 17, 21)], [1, 7, 0, 1, 1])
                self.lib.busy(0); self.lib.process()
                self.assert_loaded(3, 456)
                self.assertEqual([self.lib.get(i) for i in (17, 18, 21)], [0, int(failure), 0])
        self.lib.reset(); self.lib.transferReady(456); self.lib.busy(1)
        for _ in range(599): self.lib.process()
        self.assertEqual([self.lib.get(i) for i in (1, 17, 18, 21)], [1, 1, 0, 1])
        self.lib.process()
        self.assertEqual([self.lib.get(i) for i in (1, 3, 5, 7, 17, 18, 21)], [0, 0, 0, 1, 0, 1, 0])
        self.lib.busy(0); self.lib.process()
        self.assertTrue(self.lib.selectSave(1))
        self.assertEqual(self.lib.get(5), 0)


if __name__ == "__main__":
    unittest.main()
