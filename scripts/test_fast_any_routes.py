"""Execute the production Fast Any% start, child-retry and result paths."""

import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_iling_attempt_lifecycle import function

ROOT = Path(__file__).resolve().parents[1]


class FastAnyRouteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-fast-any-")
        cls.addClassCleanup(cls.temp.cleanup)
        il = (ROOT / "src/iling.cpp").read_text()
        loader = (ROOT / "src/stage_loader.cpp").read_text()
        sequence = (ROOT / "include/SMS/System/GameSequence.hxx").read_text()
        wheel = (ROOT / "src/warp_wheel.cpp").read_text()
        parents = re.search(r"(?:constexpr|const) u8 kParentAreas\[\].*?\};", wheel, re.S).group(0)
        code = r'''
#include "susamune/stage_loader.hxx"
typedef unsigned long long u64;
namespace Assist {enum {OTHER=1,KING_BOO_FRUIT=2,PETEY_NO_TORNADO=4,PETEY_ROUTE=8};}
struct TGameSequence {
''' + sequence[sequence.index("    enum Area {"):sequence.index("    void set(")] + r'''
u8 mAreaID,mEpisodeID;u16 mFlags;};
namespace LevelWarp {struct Dest {enum {POST_CORONA=0x80};u8 area,episode,gameInt3;};
''' + parents + function(wheel, "LevelWarp::parentArea").replace("LevelWarp::", "") + "}\n"
        code += il[il.index("enum FinishKind {"):il.index("const int kSecretOnlyPbSlotFirst")]
        for name in ("kSecretOnlyPbSlotFirst", "kSecretOnlyPbSlotLast", "kEntryPinnaEyg",
                     "kEntryPinna8", "kEntryGelatoGbs", "kEntryNoki3Inside", "kEntrySirena8",
                     "kEntryFullRedsFirst", "kEntryFullRedsLast"):
            code += re.search(r"const int " + name + r" = \d+;", il).group(0) + "\n"
        code += function(il, "isSecretOnlyPbSlot")
        code += r'''
struct Application {TGameSequence mCurrentScene,mPrevScene;}gpApplication;
struct Timer {u32 serial=1;u32 attemptSerial(){return serial;}}gQFTTimer;
struct TFlagManager {static TFlagManager *smInstance;void setFlag(u32,s32){}};
TFlagManager *TFlagManager::smInstance=0;
bool sPinnaEygRestart,sRunning,sAttemptReady,sAwaitingStageSetup,sTransitionPending;
bool sChildRetryContinuation,sSecretOnly,sRecordsEligible,sNativeIgt;
bool sBowserNozzleShieldActive,sBowserNozzleShieldPending;
s32 sSavedBowserNozzleFlag;LevelWarp::Dest sAttemptStart;
u8 sFinishKind,sAssistReasons,sEpisodeChoices[21];int sSelectedEntry;u32 sAttemptSerial;
u8 liveReasons;int recordStarts,recordInvalid,pbWrites,recordResults,splitResults,splitInvalid;
namespace Records {
enum GhostRaceSource{GHOST_RACE_NONE,GHOST_RACE_PERSONAL,GHOST_RACE_IMPORTED};
void onILAttemptStarted(int){++recordStarts;recordInvalid=0;}
void invalidateAttempt(u8){++recordInvalid;}
void onILResult(int,u8,s32,s32,bool,GhostRaceSource,s32,s32){++recordResults;}
}
namespace SplitStats {void invalidateAttempt(){++splitInvalid;}void onILAttemptStarted(int,bool){}
void onILAttemptEnded(){}void onILResult(int,s32){++splitResults;}}
namespace Ghost {enum{RACE_SOURCE_PERSONAL,RACE_SOURCE_IMPORTED};
struct RaceContext{int ilEntry;u32 attemptSerial;int source;s32 targetQf,startingPbQf;};
bool raceContext(RaceContext*){return false;}}
u8 liveGlobalAssistReasons(){return liveReasons;}
bool isPlazaEntry(int){return false;}
void applyPlazaOverlay(int){}void applyEntryOverlay(int){}
int entryForChildMode(const TGameSequence&,int){return -1;}
void clearAttempt(){sRunning=false;sAwaitingStageSetup=false;sSelectedEntry=-1;}
void captureGhostRace(int){}
int episodeChoiceIndex(int){return -1;}
int sRecentQf[5],sRecentNext,sRecentCount;u8 sRecentEntry[5];const int kRecentCount=5;
struct Console{int getFinishedTime(){return 0;}};
struct TMarDirector{Console *mGCConsole;}*gpMarDirector;
bool stageObjectsLive(){return false;}
bool recordPB(int,s32){++pbWrites;return true;}
namespace ILing{bool sameEpisodeShine(int,int){return false;}}
namespace StageLoader{
''' + loader[loader.index("enum SessionState {"):loader.index("enum ModalState {")] + r'''
const int kMarioWinDemoState=0x1302,kRetryDelayFrames=15;
struct Mario{int mState;}*gpMarioOriginal;
enum {SETTING_STREAK_AUTO_RESET};
struct Settings{bool getBool(int){return true;}}gSettings;
void clearShinePublishLatch(){}
struct Runtime{bool practiceLoaded;int retryFrames;u8 mode,activePlaylistId,activeCount,activeIndex,state;u32 attemptSerial;
u32 eligibleCompletes;u64 completedQfTotal;int targetQf;bool playlistPbEligible;}sRuntime;
struct Queues{u8 active[120];}sQueues;
int successes,failures,lastOutcome;bool finalEligible;
void queueSuccess(int,s32){++successes;++sRuntime.activeIndex;sRuntime.state=STATE_RETRY_DELAY;}
void queueFailure(Outcome outcome,s32){++failures;lastOutcome=outcome;sRuntime.state=STATE_RETRY_DELAY;}
void beginAttempt(u32 serial){sRuntime.attemptSerial=serial;sRuntime.state=STATE_RUNNING;}
void incrementSaturated(u32 &v){if(v!=0xffffffffu)++v;}
void addSaturated(u64 &total,u64 v){total+=v;}
int expectedResultEntry();
'''
        for name in ("expectedStartEntry", "fastAnyStart", "active", "mode",
                     "onILAttemptStarted", "onILResult", "invalidatePlaylistBest"):
            code += function(loader, name)
        code += "int expectedResultEntry(){return expectedStartEntry();}\n}\n"
        for name in ("validEntry", "pbSlot", "sameDest", "parentOrSelf", "sameCourse",
                     "isBonusShine", "selectedStart", "sessionStartChanged", "sceneMatches",
                     "entryFinish", "acceptsAnySelectedOrigin", "isPinnaOneRouteScene",
                     "isPinnaEightReturn", "acceptsSelectedOriginScene", "isInternalScene",
                     "entryForStartScene", "acceptsSkipOrigin", "fullRedsBaseShine",
                     "entryForResult", "armAttempt", "beginAttemptScene", "beforeStageSetup",
                     "recordResult"):
            code += function(il, name)
        reset_start = il.index("    const u32 serial = gQFTTimer.attemptSerial();", il.index("void update()"))
        reset_end = il.index("    if (sCarryRestorePending", reset_start)
        code += "void updateSerial(){\n" + il[reset_start:reset_end] + "}\n"
        code += r'''
#define API extern "C" __declspec(dllexport)
API void reset(){
  StageLoader::sRuntime={};StageLoader::sRuntime.activePlaylistId=255;
  StageLoader::successes=StageLoader::failures=0;
  sRunning=sAttemptReady=sAwaitingStageSetup=sChildRetryContinuation=false;
  sSelectedEntry=-1;liveReasons=0;gQFTTimer.serial=1;
  recordStarts=recordInvalid=pbWrites=recordResults=splitResults=splitInvalid=0;
  gpApplication.mCurrentScene={2,0,0};gpApplication.mPrevScene={2,0,0};
}
API void select(int entry,int playlist,int mode){
  StageLoader::sRuntime.activePlaylistId=(u8)playlist;
  StageLoader::sRuntime.mode=(u8)mode;
  StageLoader::sRuntime.activeCount=2;
  StageLoader::sRuntime.activeIndex=0;
  StageLoader::sRuntime.state=StageLoader::STATE_REQUESTING;
  StageLoader::sRuntime.playlistPbEligible=true;
  StageLoader::sRuntime.targetQf=-1;
  StageLoader::sQueues.active[0]=(u8)entry;
  LevelWarp::Dest start=selectedStart(entry);
  armAttempt(kEntries[entry],entry,&start);
  gpApplication.mCurrentScene={start.area,start.episode,0};
  beforeStageSetup();
  ++gQFTTimer.serial;sAttemptSerial=gQFTTimer.serial;sAttemptReady=true;
  StageLoader::onILAttemptStarted(entry);
}
API void hop(int area,int episode,int retry){
  gpApplication.mPrevScene=gpApplication.mCurrentScene;
  gpApplication.mCurrentScene={(u8)area,(u8)episode,0};
  beforeStageSetup();if(retry)++gQFTTimer.serial;updateSerial();
}
API void finish(int shine,int assist){sAssistReasons|=(u8)assist;
  int entry=entryForResult((u8)shine);if(entry>=0)recordResult(entry,1200);
}
API int value(int key){switch(key){
case 0:return sAttemptStart.area;case 1:return sAttemptStart.episode;
case 2:return sAttemptStart.gameInt3;case 3:return sSelectedEntry;
case 4:return sRunning;case 5:return sRecordsEligible;
case 6:return StageLoader::successes;case 7:return StageLoader::failures;
case 8:return StageLoader::sRuntime.playlistPbEligible;
case 9:return pbWrites;case 10:return recordResults;case 11:return splitResults;
case 12:return sChildRetryContinuation;case 13:return recordInvalid;
case 14:return StageLoader::sRuntime.activeIndex;default:return -1;}}
'''
        source = Path(cls.temp.name) / "fast_any.cpp"
        source.write_text(code)
        library = source.with_suffix(".dll")
        proc = subprocess.run([
            str(ROOT / "toolchain/clang++.exe"), "--target=x86_64-pc-windows-msvc",
            "-shared", "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry", "-O2",
            "-I", str(ROOT / "include"), "-I", str(ROOT / "src"),
            str(source), "-o", str(library)], capture_output=True, text=True)
        if proc.returncode:
            raise RuntimeError(proc.stdout + proc.stderr)
        cls.lib = C.CDLL(str(library))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def setUp(self):
        self.lib.reset()

    def values(self, *keys):
        return tuple(self.lib.value(key) for key in keys)

    def test_fast_any_pinna_starts_on_beach_with_original_parent_episode(self):
        for entry, episode in ((38, 0), (39, 1), (42, 2), (43, 3), (46, 2), (49, 6)):
            with self.subTest(entry=entry):
                self.lib.reset(); self.lib.select(entry, 0, 0)
                self.assertEqual(self.values(0, 1, 2, 3), (5, episode, episode, entry))

    def test_noki_three_starts_outside_bottle_and_finishes_exact_shine(self):
        self.lib.select(67, 0, 0)
        self.assertEqual(self.values(0, 1, 2, 5, 8), (9, 2, 2, 0, 1))
        self.lib.hop(0x2c, 0, 0)
        self.lib.finish(58, 0)
        self.assertEqual(self.values(6, 9), (0, 0))
        self.lib.finish(52, 0)
        self.assertEqual(self.values(6, 8, 9, 10, 11, 14), (1, 1, 0, 0, 0, 1))

    def test_standalone_custom_and_streak_starts_keep_their_pb_identity(self):
        for playlist, mode in ((255, 0), (3, 0), (0, 1)):
            for entry, area, episode in ((38, 13, 0), (42, 13, 1), (49, 13, 4), (67, 0x2c, 0)):
                with self.subTest(playlist=playlist, mode=mode, entry=entry):
                    self.lib.reset(); self.lib.select(entry, playlist, mode)
                    self.assertEqual(self.values(0, 1, 5), (area, episode, 1))

    def test_pinna_beach_approach_and_movie_chain_never_overwrite_park_pb(self):
        for entry, park_episode, shine in ((38, 0, 30), (42, 1, 32), (49, 4, 36)):
            with self.subTest(entry=entry):
                self.lib.reset(); self.lib.select(entry, 0, 0)
                self.lib.hop(13, park_episode, 0)
                if entry == 38:
                    for area, episode in ((13, 6), (0x3a, 1), (13, 7)):
                        self.lib.hop(area, episode, 0)
                self.lib.finish(shine, 0)
                self.assertEqual(self.values(3, 6, 8, 9, 10, 11), (entry, 1, 1, 0, 0, 0))

    def test_repeated_secret_deaths_keep_queue_identity_and_allow_finish(self):
        for entry, area, shine in ((2, 0x2f, 2), (7, 0x2e, 5), (17, 0x30, 13),
                                   (25, 0x20, 20), (39, 0x32, 31), (45, 0x29, 35),
                                   (46, 0x29, 35), (53, 0x33, 41), (57, 0x28, 43),
                                   (71, 0x1f, 55), (82, 0x2a, 64)):
            with self.subTest(entry=entry):
                self.lib.reset(); self.lib.select(entry, 0, 0)
                self.lib.hop(area, 0, 0)
                self.lib.hop(area, 0, 1); self.lib.hop(area, 0, 1)
                self.assertEqual(self.values(3, 4, 5, 8, 12), (entry, 1, 0, 0, 1))
                self.lib.finish(shine, 0)
                self.assertEqual(self.values(6, 9, 10, 11, 14), (1, 0, 0, 0, 1))

    def test_assistance_and_streak_rules_still_reject_child_continuations(self):
        for mode, assist in ((0, 1), (1, 0)):
            with self.subTest(mode=mode):
                self.lib.reset(); self.lib.select(2, 0, mode)
                self.lib.hop(0x2f, 0, 0); self.lib.hop(0x2f, 0, 1)
                self.lib.finish(2, assist)
                self.assertEqual(self.values(6, 9, 10, 11, 14), (0, 0, 0, 0, 0))

    def test_new_fast_any_route_invalidates_only_its_old_playlist_best(self):
        loader = (ROOT / "src/stage_loader.cpp").read_text()
        self.assertIn("playlistHashWord(hash, preset == 0 ? 1 : 0)", function(loader, "builtinContentHash"))
        body = function(loader, "reconcileBuiltinHashes")
        self.assertIn("if (playlists->contentHashes[preset] == hash) continue;", body)
        self.assertIn("playlists->bestQf[region][preset]", body)


if __name__ == "__main__":
    unittest.main()
