"""Restore each slot's IL overlays and nozzle ownership before disarming it."""

import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_practice_tape import function_source

ROOT = Path(__file__).resolve().parents[1]


class ILSlotSidecarTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.folder.cleanup)
        production = ROOT / "src/iling.cpp"
        definitions = "\n".join(function_source(production, signature) + ";" for signature in (
            "struct OverlayFlag {", "struct AttemptState {", "struct SavedAttemptData {"))
        helpers = "\n".join(function_source(production, signature) for signature in (
            "bool readOverlayFlag(", "void writeOverlayFlag(", "void restoreOverlayFlags(",
            "void restorePlazaStoryFlags()", "void clearAttempt()"))
        functions = "\n".join(function_source(production, signature) for signature in (
            "void captureSavestate(", "void restoreSavestate(", "void onSavestateLoaded()"))
        source = Path(cls.folder.name) / "ilslots.cpp"
        source.write_text(r'''
#include "susamune/iling.hxx"
#define memcpy __builtin_memcpy
namespace LevelWarp {struct Dest {u8 area,episode,gameInt3;};}
static const int kOverlayFlagCount=2;
enum{REJECT_PRACTICE};
''' + definitions + r'''
static_assert(sizeof(SavedAttemptData)==sizeof(ILing::SavestateData),"sidecar size");
static AttemptState sAttemptState,sSavedAttemptState;
static bool sHaveSavedAttempt,sPinnaEygRestart,sSavedPinnaEygRestart;
static bool sHaveSetupShineCount,sHaveSetupMovieFlag,sTemporaryRocketActive;
static bool sRocketEquipPending,sBowserNozzleShieldPending,sBowserNozzleShieldActive;
static u8 sSavedSecondNozzle;
static s32 sSavedSecondNozzleFlag,sSavedBowserNozzleFlag;
static int sFanfareDelay,sAchievementChimeBlockFrames,sBannerFrames;
#define sAttemptSerial sAttemptState.serial
struct Timer{u32 attemptSerial(){return 999;}}gQFTTimer;
#define sRunning sAttemptState.running
#define sAttemptReady sAttemptState.ready
#define sAwaitingStageSetup sAttemptState.awaitingStageSetup
#define sCarryRestorePending sAttemptState.carryRestorePending
#define sTransitionPending sAttemptState.transitionPending
#define sRecordsEligible sAttemptState.recordsEligible
#define sChildRetryContinuation sAttemptState.childRetryContinuation
#define sNativeIgt sAttemptState.nativeIgt
#define sHavePlazaStoryFlags sAttemptState.havePlazaStoryFlags
#define sPlazaStoryFlags sAttemptState.plazaStoryFlags
#define sOverlayCount sAttemptState.overlayCount
#define sAssistReasons sAttemptState.assistReasons
#define sOverlayFlags sAttemptState.overlayFlags
#define sSecretOnly sAttemptState.secretOnly
#define sRejectionCause sAttemptState.rejectionCause
#define sSelectedEntry sAttemptState.selectedEntry
static unsigned ended,invalidated;
namespace Records {void onILAttemptEnded(){}}
namespace StageLoader {bool onSavestateLoaded(){return false;}void onILAttemptEnded(){++ended;}void invalidatePlaylistBest(){++invalidated;}}
namespace SplitStats {void onILAttemptEnded(){}}
struct TFlagManager {
    static TFlagManager *smInstance;
    struct {u8 m1Type[0x80];} Type1Flag;
    int values[4];
    int getFlag(u32 id){return values[id==0x40004?0:id];}
    bool getBool(u32 id){return getFlag(id)!=0;}
    void setFlag(u32 id,int value){values[id==0x40004?0:id]=value;}
    void setBool(bool value,u32 id){setFlag(id,value);}
};
static TFlagManager flags;
TFlagManager *TFlagManager::smInstance=&flags;
struct Fludd {u8 mSecondNozzle;};
static Fludd fludd;
static struct Mario {Fludd *mFludd;} mario={&fludd},*gpMarioOriginal=&mario;
void restorePlazaSetupState(){}
bool isPlazaEntry(int){return false;}
bool stageObjectsLive(){return true;}
''' + helpers + r'''
namespace ILing {
''' + functions + r'''
}
static ILing::SavestateData slots[3],candidate;
extern "C" __declspec(dllexport) void prime(u32 tag,u32 rocket,u32 shield) {
    sAttemptState={};sRunning=sAttemptReady=sRecordsEligible=true;
    sAttemptState.serial=100+tag;sSelectedEntry=(int)tag;
    sHavePlazaStoryFlags=true;sPlazaStoryFlags=(u8)(tag<<4);
    sOverlayCount=1;sOverlayFlags[0]={1,(u8)(tag&1),1,0,0};
    sTemporaryRocketActive=rocket!=0;sSavedSecondNozzle=(u8)(tag+3);
    sSavedSecondNozzleFlag=40+tag;sRocketEquipPending=true;
    sBowserNozzleShieldPending=shield==1;sBowserNozzleShieldActive=shield==2;
    sSavedBowserNozzleFlag=60+tag;sPinnaEygRestart=(tag&1)!=0;
    flags.values[0]=999;flags.values[1]=1;flags.Type1Flag.m1Type[0x70]=0x0b;
    fludd.mSecondNozzle=99;ended=invalidated=0;
    sFanfareDelay=sAchievementChimeBlockFrames=sBannerFrames=9;
}
extern "C" __declspec(dllexport) void save(u32 slot,u32 commit) {
    ILing::captureSavestate(candidate);if(commit)slots[slot]=candidate;
}
extern "C" __declspec(dllexport) void load(u32 slot){ILing::restoreSavestate(slots[slot]);}
extern "C" __declspec(dllexport) int value(u32 which) {
    switch(which){case 0:return sRunning;case 1:return sRecordsEligible;
    case 2:return flags.values[1];case 3:return flags.Type1Flag.m1Type[0x70];
    case 4:return fludd.mSecondNozzle;case 5:return flags.values[0];
    case 6:return sBowserNozzleShieldPending;case 7:return sBowserNozzleShieldActive;
    case 8:return sSavedBowserNozzleFlag;case 9:return sPinnaEygRestart;
    case 10:return sSelectedEntry;case 11:return sAttemptState.serial;
    case 12:return sFanfareDelay+sAchievementChimeBlockFrames+sBannerFrames;
    case 13:return ended;case 14:return invalidated;case 15:return sTemporaryRocketActive;
    default:return 999;}
}
''', encoding="ascii")
        library = source.with_suffix(".dll")
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
                        "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry", "-O2",
                        "-I", str(ROOT / "include"), str(source), "-o", str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_slots_restore_own_overlay_and_rocket_baselines_without_pb_eligibility(self):
        for slot in range(3):
            self.lib.prime(slot, 1, slot)
            self.lib.save(slot, 1)
        for slot in (2, 0, 1):
            self.lib.prime(7, 1, 2)
            self.lib.load(slot)
            self.assertEqual([self.lib.value(i) for i in range(16)],
                             [0, 0, slot & 1, (slot << 4) | 0x0b,
                              slot + 3, slot + 40, int(slot == 1), int(slot == 2),
                              slot + 60, slot & 1, -1, slot + 100, 0, 1, 1, 0])

    def test_plain_slot_does_not_apply_another_slots_temporary_nozzle(self):
        self.lib.prime(0, 0, 0)
        self.lib.save(0, 1)
        self.lib.prime(7, 1, 2)
        self.lib.load(0)
        self.assertEqual([self.lib.value(i) for i in (4, 5, 6, 7, 15)], [99, 999, 0, 0, 0])

    def test_uncommitted_capture_is_pure_and_keeps_old_slot(self):
        self.lib.prime(1, 1, 1)
        self.lib.save(0, 1)
        self.lib.prime(7, 1, 2)
        before = [self.lib.value(i) for i in range(16)]
        self.lib.save(0, 0)
        self.assertEqual([self.lib.value(i) for i in range(16)], before)
        self.lib.load(0)
        self.assertEqual([self.lib.value(i) for i in (4, 5, 6, 7, 8, 11)],
                         [4, 41, 1, 0, 61, 101])


if __name__ == "__main__":
    unittest.main()
