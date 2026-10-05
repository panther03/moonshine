"""Exercise production ghost prefix capture, bank rebinding and TAS completion."""

import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_ghost_tas_clock import function

ROOT = Path(__file__).resolve().parents[1]


class GhostSavestatePrefixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.folder.cleanup)
        ghost = (ROOT / "src/ghost.cpp").read_text()
        iling = (ROOT / "src/iling.cpp").read_text()
        source = r'''
#include "susamune/ghost_format.h"
#include "susamune/ghost_clock.h"
#include "susamune/state_codec.hxx"
#include "susamune/susamune_cfg.h"
typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
typedef int s32; typedef short s16; typedef long long s64; typedef float f32;
typedef long long OSTime;
extern "C" int _fltused=0;
extern "C" void *memcpy(void*d,const void*s,__SIZE_TYPE__ n) {
 u8 *a=(u8*)d; const u8*b=(const u8*)s; while(n--)*a++=*b++; return d;
}
extern "C" void *memset(void*d,int c,__SIZE_TYPE__ n) {
 u8*a=(u8*)d; while(n--)*a++=(u8)c; return d;
}
struct Timer {
 s32 qf; u32 serial; bool available,stopped; u32 custom,transition; bool death;
 bool currentQf(s32*out,bool*stop=0){*out=qf;if(stop)*stop=stopped;return available;}
 u32 attemptSerial(){return serial;}
 bool consumeCustom(bool d,s32*out){++custom;death=d;*out=qf;stopped=true;return true;}
 bool consumeTransition(s32*out,u16*target){++transition;*out=qf;*target=2;stopped=true;return true;}
} gQFTTimer;
namespace GhostFludd {void capture(SusamuneGhostFluddSample& p) {
 memset(&p,0,sizeof(p));p.mode=SUSAMUNE_GHOST_FLUDD_PRESENT;p.offset[1]=40;}}
namespace ILing {
enum { FINISH_SHINE, FINISH_TRANSITION, FINISH_PLANT, FINISH_DEATH };
enum { SAVED_GHOST_END_NONE, SAVED_GHOST_END_TRANSITION, SAVED_GHOST_END_PLANT,
       SAVED_GHOST_END_DEATH };
bool sRunning,sAttemptReady;
u8 sFinishKind;
u32 invalidations;
void invalidateForAssist(){++invalidations;}
'''
        source += function(iling, "u8 savestateGhostEndpoint()")
        source += function(iling, "void updateSavestateGhostEndpoint(")
        source += r'''
}
namespace Ghost {
enum { kSavestateSpanCount=3 };
struct SavestateData { u32 words[128]; };
const u32 kSavedPrefixMagic=0x53475046u;
const u32 kMaxSamples=SUSAMUNE_GHOST_MAX_SAMPLE_COUNT;
const u16 kMaxSegments=SUSAMUNE_GHOST_V4_MAX_SEGMENTS;
const s32 kMaxDurationQf=107892, kPositionScale=SUSAMUNE_GHOST_POSITION_SCALE;
const s32 kMaxPosition=1000000;
const u32 kPlaybackTokenBit=0x80000000u;
const u32 kSegmentTableOffset=0x80000u-kMaxSegments*32u;
typedef SusamuneGhostPoseSample Sample;
alignas(32) u8 banks[2][0x80000];
alignas(32) SusamuneGhostInputSample inputBanks[2][SUSAMUNE_GHOST_INPUT_MAX_COUNT];
#undef SUSAMUNE_GHOST_RECORD_PPC_BASE
#undef SUSAMUNE_GHOST_PLAY_PPC_BASE
#undef SUSAMUNE_GHOST_INPUT_RECORD_PPC_BASE
#undef SUSAMUNE_GHOST_INPUT_PLAY_PPC_BASE
#define SUSAMUNE_GHOST_RECORD_PPC_BASE ((__UINTPTR_TYPE__)banks[0])
#define SUSAMUNE_GHOST_PLAY_PPC_BASE ((__UINTPTR_TYPE__)banks[1])
#define SUSAMUNE_GHOST_INPUT_RECORD_PPC_BASE ((__UINTPTR_TYPE__)inputBanks[0])
#define SUSAMUNE_GHOST_INPUT_PLAY_PPC_BASE ((__UINTPTR_TYPE__)inputBanks[1])
#define IS_EMULATOR 0
'''
        for signature in ("enum RecordFailure", "enum ClockPhase", "enum ObserverPhase",
                          "struct Segment", "struct Track", "struct SavedPrefix",
                          "enum SavedPrefixFlags", "enum SaveSource", "struct SaveSelection"):
            source += function(ghost, signature) + ";\n"
        source += r'''
Track sRecord,sPlayback;
SusamuneGhostClock sRecordClock;
u32 sAttemptSerial,sRecordToken,sRecordIdentityToken,sPlaybackToken,sPlaybackOriginRecordToken;
u32 sPBTokenSerial,sPlaybackCursor; s32 sPlaybackCursorQf; u16 sPlaybackSegment;
s32 sLastSampleQf,sClockLastQf,sClockEpochStartQf,sPendingPreviousClockQf,sBoundaryPriorQf;
s32 sLiveParentEpisode;
u16 sClockObservations,sBoundaryBaseSegmentCount;
u8 sLiveArea,sLiveEpisode,sLiveRouteParentArea,sLiveRouteFlags,sRaceSplitCount;
ClockPhase sClockPhase; ObserverPhase sObserverPhase;
bool sRecording,sRestoredPrefix,sFrameFrozen,sFrameAssisted,sStageRoutePending;
u8 sRestoredEndpoint;
bool sPendingHadLiveRoute,sPendingContinueRecording,sBoundaryPending,sLiveRouteValid;
bool sPlaybackPinned,sPinRouteCheckPending,sChallengerNotified,sGhostVisible,cleanup,fastForward;
struct Mario { struct {f32 x,y,z;} mTranslation; s16 mModelAngleY; } mario,*gpMarioOriginal=&mario;
struct Director {} director,*gpMarDirector=&director;
struct Menu {void toast(const char*){}} *gMenu=0;
u8 runningRegion(){return 1;}
bool observerRunning(){return sObserverPhase!=OBSERVER_OFF;}
bool observerStatsSuppressed(){return observerRunning()||cleanup;}
void clearRaceContext(){sRaceSplitCount=0;}
void resetObserverRuntime(){sObserverPhase=OBSERVER_OFF;cleanup=false;}
void releaseObserverMario(bool){}
void endObserver(bool,const char*);
void onSavestateLoaded();
void captureLiveRoute(){sLiveArea=2;sLiveEpisode=0;sLiveParentEpisode=0;
 sLiveRouteParentArea=255;sLiveRouteFlags=0;sLiveRouteValid=true;}
bool captureAnimation(u16*id,u16*phase){*id=0xC3;*phase=0;return true;}
u8 captureYoshiState(){return 0;}
u8 captureHeldObject(Track&){return 0;}
Segment *lastSegment(Track&track){return track.segmentCount?&track.segments[track.segmentCount-1]:0;}
'''
        for signature in (
            "SusamuneGhostInputSample &inputAt(", "void clearTrack(", "void bumpRecordToken()", "void bumpPlaybackToken()",
            "u32 nextPBToken()", "void clearRecord()", "void failRecording(",
            "void stopAll()", "void rewindPlayback()", "s32 recordQf(",
            "void updateRestoredRecorder()", "bool validRouteTuple(",
            "u32 readU24(", "s32 readS24(", "void writeU24(", "void writeS24(",
            "s32 sampleX(", "s32 sampleY(", "s32 sampleZ(",
            "u16 sampleAnimationId(", "u16 sampleAnimationPhase(",
            "void setSampleAnimation(", "void setSampleAttachments(",
            "u16 interpolateAnimationPhase(", "s32 interpolateFixed(",
            "bool fixedPosition(", "bool appendSegment()", "void dropLastEmptySegment()",
            "bool appendSample(", "void clearEpochSamples()", "bool startEpochSamples(",
            "bool finishTrackAt(", "void finishRecording(", "void captureInput(",
            "SaveSelection latestSaveableTrack()", "bool markCurrentRecordingPB(",
            "bool recorderBanksValid()", "bool decodeSavedPrefix(",
            "bool captureSavestate(", "bool savestateRestoreSpans(",
            "void restoreSavestate(", "void onSavestateLoaded()",
        ):
            source += function(ghost, signature) + "\n"
        source += r'''
void endObserver(bool,const char*){stopAll();}
SavestateData states[3];
StateCodec::ReadSpan readSpans[3]; StateCodec::WriteSpan writeSpans[3];
}
#define API extern "C" __declspec(dllexport)
API void reset(){using namespace Ghost;
 sRecord.samples=(Sample*)banks[0];sPlayback.samples=(Sample*)banks[1];
 sRecord.segments=(Segment*)(banks[0]+kSegmentTableOffset);
 sPlayback.segments=(Segment*)(banks[1]+kSegmentTableOffset);
 sRecord.inputs=inputBanks[0];sPlayback.inputs=inputBanks[1];
 stopAll();memset(states,0,sizeof(states));memset(&sRecordClock,0,sizeof(sRecordClock));
 gQFTTimer.qf=0;gQFTTimer.serial=sAttemptSerial=7;
 gQFTTimer.available=true;gQFTTimer.stopped=false;gQFTTimer.custom=gQFTTimer.transition=0;
 gpMarioOriginal=&mario;gpMarDirector=&director;mario.mTranslation={0,0,0};
 sFrameFrozen=sFrameAssisted=false;sClockPhase=CLOCK_ACTIVE;sClockObservations=30;
 sClockLastQf=sClockEpochStartQf=sLastSampleQf=0;
 sBoundaryBaseSegmentCount=0;sRecording=true;
 captureLiveRoute();appendSegment();recordQf(0);startEpochSamples(0);
 ILing::sRunning=false;ILing::sAttemptReady=true;
 ILing::sFinishKind=ILing::FINISH_SHINE;ILing::invalidations=0;
}
API void frame(s32 qf,s32 x,u32 button){using namespace Ghost;
 gQFTTimer.qf=qf;mario.mTranslation.x=(f32)x;recordQf(qf);appendSample(qf);
 sClockLastQf=qf;SusamunePracticeInput input={};input.buttons=(u16)button;captureInput(input);
}
API void legacy(){Ghost::sRecord.formatVersion=5;}
API int capture(u32 slot){using namespace Ghost;return captureSavestate(states[slot],readSpans);}
API int destinations(u32 slot){using namespace Ghost;return savestateRestoreSpans(states[slot],writeSpans);}
API const void *source(u32 i){return Ghost::readSpans[i].data;}
API u32 sourceSize(u32 i){return Ghost::readSpans[i].size;}
API void *destination(u32 i){return Ghost::writeSpans[i].data;}
API u32 destinationSize(u32 i){return Ghost::writeSpans[i].size;}
API void restore(u32 slot,s32 qf){gQFTTimer.qf=qf;gQFTTimer.stopped=false;Ghost::restoreSavestate(Ghost::states[slot]);}
API void finish(s32 qf){using namespace Ghost;gQFTTimer.qf=qf;gQFTTimer.stopped=true;finishRecording(qf,true);}
API u32 get(u32 key){using namespace Ghost;switch(key){
 case 0:return sRecord.count;case 1:return sRecord.inputCount;case 2:return sRecord.startQf;
 case 3:return sRecord.endQf;case 4:return sRecord.resultQf;case 5:return sRecord.runFlags;
 case 6:return sRecord.completed;case 7:return sRecording;case 8:return sRestoredPrefix;
 case 9:return latestSaveableTrack().track==&sRecord;case 10:return sRecord.pb;
 case 11:return sRecord.pbToken;case 12:return sRecord.segmentCount;
 case 13:return ILing::invalidations;case 14:return gQFTTimer.custom;
 case 15:return gQFTTimer.transition;case 16:return gQFTTimer.death;
 case 17:return ((u8*)sPlayback.samples)[0];case 18:return sPlaybackPinned;
 case 19:return sRecordClock.omittedQf;case 20:return sRecordToken;
 case 21:return sRestoredEndpoint;
 }return 0;}
API s32 x(u32 index){return Ghost::sampleX(Ghost::sRecord.samples[index]);}
API u32 inputQf(u32 index){return Ghost::inputAt(Ghost::sRecord,index).qf;}
API u32 inputButton(u32 index){return Ghost::inputAt(Ghost::sRecord,index).input.buttons;}
API int pb(s32 qf){return Ghost::markCurrentRecordingPB(qf);}
API void switchBanks(){using namespace Ghost;
 Sample*s=sRecord.samples;sRecord.samples=sPlayback.samples;sPlayback.samples=s;
 Segment*t=sRecord.segments;sRecord.segments=sPlayback.segments;sPlayback.segments=t;
 SusamuneGhostInputSample*i=sRecord.inputs;sRecord.inputs=sPlayback.inputs;sPlayback.inputs=i;
 clearRecord();sRecording=false;sPlaybackPinned=true;sPlayback.valid=true;
 ((u8*)sPlayback.samples)[0]=123;
}
API void mode(u32 value){using namespace Ghost;
 sObserverPhase=value==1?OBSERVER_ACTIVE_TWO:OBSERVER_OFF;cleanup=value==2;
 if(value==3)sRecording=false;
}
API void damage(u32 slot,u32 kind){using namespace Ghost;
 SavedPrefix saved;memcpy(&saved,&states[slot],sizeof(saved));
 switch(kind){case 0:saved.track.count=kMaxSamples+1;break;
 case 1:saved.track.inputCount=SUSAMUNE_GHOST_INPUT_MAX_COUNT+1;break;
 case 2:saved.track.segmentCount=kMaxSegments+1;break;
 case 3:saved.track.samples=(Sample*)1;break;
 case 4:saved.track.endQf=saved.track.startQf-1;break;
 case 5:saved.clock.omittedQf=0xffffffffu;break;
 case 6:saved.flags=0xff;break;case 7:saved.magic=0;break;}
 memcpy(&states[slot],&saved,sizeof(saved));
}
API void badBank(){Ghost::sRecord.segments=(Ghost::Segment*)1;}
API void omit(u32 qf){using namespace Ghost;sRecordClock.omittedQf=qf;}
API void endpoint(u32 kind,u32 live,u32 ready){using namespace ILing;
 Ghost::sRestoredEndpoint=(u8)kind;sRunning=live!=0;sAttemptReady=ready!=0;
 gQFTTimer.custom=gQFTTimer.transition=0;Ghost::updateRestoredRecorder();}
API void configureIL(u32 kind,u32 active){using namespace ILing;
 sFinishKind=(u8)kind;sRunning=active!=0;sAttemptReady=true;}
API void serial(u32 value){gQFTTimer.serial=value;}
API void pending(u32 stage,u32 boundary){using namespace Ghost;
 sStageRoutePending=stage!=0;sBoundaryPending=boundary!=0;}
API void clear(){Ghost::clearRecord();Ghost::sRecording=true;}
API void continuation(){using namespace Ghost;
 sLiveArea=47;sLiveEpisode=0;sLiveRouteParentArea=2;sLiveRouteFlags=1;
 sRecording=true;sClockPhase=CLOCK_ACTIVE;appendSegment();startEpochSamples(gQFTTimer.qf);
}
'''
        cpp = Path(cls.folder.name) / "prefix.cpp"
        cpp.write_text(source)
        dll = cpp.with_suffix(".dll")
        result = subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc",
                                 "-shared", "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry",
                                 "-fno-exceptions", "-fno-rtti", "-I", str(ROOT / "include"),
                                 str(cpp), "-o", str(dll)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        cls.lib = C.CDLL(str(dll))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        for name in ("source", "destination"):
            getattr(cls.lib, name).argtypes = [C.c_uint]
            getattr(cls.lib, name).restype = C.c_void_p
        for name in ("sourceSize", "destinationSize", "get", "inputQf", "inputButton"):
            getattr(cls.lib, name).argtypes = [C.c_uint]
            getattr(cls.lib, name).restype = C.c_uint

    def setUp(self):
        self.lib.reset()
        self.payloads = {}

    def capture(self, slot):
        self.assertEqual(self.lib.capture(slot), 1)
        self.payloads[slot] = [C.string_at(self.lib.source(i), self.lib.sourceSize(i))
                               for i in range(3)]

    def restore(self, slot, qf):
        self.assertEqual(self.lib.destinations(slot), 1)
        for i, payload in enumerate(self.payloads[slot]):
            self.assertEqual(self.lib.destinationSize(i), len(payload))
            if payload:
                C.memmove(self.lib.destination(i), payload, len(payload))
        self.lib.restore(slot, qf)

    def test_restore_replaces_abandoned_future_then_finishes_full_tas(self):
        self.lib.frame(4, 10, 0x100)
        self.lib.frame(8, 20, 0x200)
        self.capture(0)
        self.lib.frame(12, 999, 0x400)
        self.restore(0, 8)
        self.assertEqual([self.lib.get(i) for i in (0, 1, 2, 3)], [3, 2, 0, 8])
        self.lib.frame(12, 30, 0x800)
        self.lib.finish(12)
        self.assertEqual([self.lib.x(i) for i in range(4)], [0, 80, 160, 240])
        self.assertEqual([self.lib.inputButton(i) for i in range(3)], [0x100, 0x200, 0x800])
        self.assertEqual([self.lib.get(i) for i in (2, 3, 4, 6, 9)], [0, 12, 12, 1, 1])
        self.assertEqual(self.lib.get(5) & 0x21, 0x21)

    def test_old_and_new_input_prefixes_keep_their_own_stride(self):
        for legacy, stride in ((False, 24), (True, 16)):
            with self.subTest(legacy=legacy):
                self.lib.reset()
                if legacy:
                    self.lib.legacy()
                self.lib.frame(4, 10, 0x100)
                self.lib.frame(8, 20, 0x200)
                self.capture(0)
                self.assertEqual(len(self.payloads[0][2]), 2 * stride)
                if not legacy:
                    for offset in (16, 40):
                        self.assertEqual(self.payloads[0][2][offset:offset+8],
                                         bytes((8, 0, 40, 0, 0, 0, 0, 0)))
                self.lib.frame(12, 99, 0x400)
                self.restore(0, 8)
                self.lib.frame(12, 30, 0x800)
                self.assertEqual([self.lib.inputButton(i) for i in range(3)],
                                 [0x100, 0x200, 0x800])
        self.assertEqual(self.lib.get(5) & 0x80000000, 0)
        self.assertEqual(self.lib.pb(12), 0)
        self.assertEqual([self.lib.get(i) for i in (10, 11)], [0, 0])

    def test_three_saved_prefixes_keep_independent_lengths_and_inputs(self):
        for slot in range(3):
            self.lib.frame(4 * (slot + 1), slot + 1, 1 << slot)
            self.capture(slot)
        for slot in (0, 2, 1, 0):
            self.restore(slot, 4 * (slot + 1))
            self.assertEqual(self.lib.get(0), slot + 2)
            self.assertEqual(self.lib.get(1), slot + 1)
            self.assertEqual(self.lib.inputQf(slot), 4 * (slot + 1))
            self.assertEqual(self.lib.x(slot + 1), (slot + 1) * 8)

    def test_bank_swap_rebinds_prefix_and_preserves_pinned_target(self):
        self.lib.frame(4, 10, 0x100)
        self.capture(0)
        original = self.lib.source(0)
        self.lib.switchBanks()
        self.assertEqual(self.lib.destinations(0), 1)
        self.assertNotEqual(self.lib.destination(0), original)
        self.restore(0, 4)
        self.assertEqual([self.lib.x(i) for i in range(2)], [0, 80])
        self.assertEqual([self.lib.get(i) for i in (17, 18)], [123, 1])

    def test_empty_watch_and_nonrecording_states_do_not_start_a_recording(self):
        for mode in (1, 2, 3):
            with self.subTest(mode=mode):
                self.lib.reset()
                self.lib.mode(mode)
                self.capture(0)
                self.assertEqual([len(p) for p in self.payloads[0]], [0, 0, 0])
                self.restore(0, 0)
                self.assertEqual([self.lib.get(i) for i in (0, 7, 8, 9)], [0, 0, 0, 0])

    def test_bad_metadata_or_banks_cannot_supply_restore_destinations(self):
        for kind in range(8):
            with self.subTest(kind=kind):
                self.lib.reset()
                self.lib.frame(4, 10, 1)
                self.capture(0)
                self.lib.damage(0, kind)
                self.assertEqual(self.lib.destinations(0), 0)
                self.assertEqual([self.lib.destinationSize(i) for i in range(3)], [0, 0, 0])
                self.assertEqual(self.lib.x(1), 80)
        self.lib.reset()
        self.lib.frame(4, 10, 1)
        self.capture(0)
        self.lib.badBank()
        self.assertEqual(self.lib.destinations(0), 0)
        self.assertEqual(self.lib.capture(1), 0)

    def test_restored_clock_and_child_segment_preserve_absolute_timeline(self):
        self.lib.frame(4, 10, 1)
        self.lib.omit(4)
        self.lib.frame(12, 20, 2)
        self.capture(0)
        self.lib.frame(400, 999, 4)
        self.restore(0, 12)
        self.assertEqual(self.lib.get(19), 4)
        self.lib.continuation()
        self.lib.frame(16, 30, 8)
        self.lib.finish(16)
        self.assertEqual([self.lib.get(i) for i in (2, 3, 4, 6, 12)], [0, 12, 12, 1, 2])

    def test_custom_endpoint_only_and_child_pb_guard_end_at_fresh_attempt(self):
        self.lib.frame(4, 10, 1)
        self.capture(0)
        self.restore(0, 4)
        for kind, expected in ((0, (0, 0, 0)), (1, (0, 1, 0)),
                               (2, (1, 0, 0)), (3, (1, 0, 1))):
            self.lib.endpoint(kind, 0, 1)
            self.assertEqual(tuple(self.lib.get(i) for i in (14, 15, 16)), expected)
        self.lib.endpoint(3, 1, 1)
        self.assertEqual([self.lib.get(i) for i in (14, 15)], [0, 0])
        prior = self.lib.get(13)
        self.lib.serial(8)
        self.lib.endpoint(3, 0, 1)
        self.assertEqual(self.lib.get(13), prior)
        self.assertEqual([self.lib.get(i) for i in (14, 15)], [0, 0])
        self.lib.serial(7)
        self.lib.clear()
        self.lib.endpoint(3, 0, 1)
        self.assertEqual(self.lib.get(8), 0)
        self.assertEqual(self.lib.get(13), prior)
        self.assertEqual([self.lib.get(i) for i in (14, 15)], [0, 0])

    def test_repeated_tas_checkpoints_keep_original_endpoint_after_il_disarms(self):
        self.lib.configureIL(3, 1)
        self.lib.frame(4, 10, 1)
        self.capture(0)
        self.lib.configureIL(0, 0)
        self.restore(0, 4)
        self.assertEqual(self.lib.get(21), 3)
        self.lib.frame(8, 20, 2)
        self.capture(1)
        self.lib.configureIL(1, 0)
        self.restore(1, 8)
        self.assertEqual(self.lib.get(21), 3)
        self.assertEqual(self.lib.get(0), 3)

    def test_pending_departure_cannot_apply_old_tas_prefix_to_new_attempt(self):
        self.lib.frame(4, 10, 1)
        self.capture(0)
        self.restore(0, 4)
        prior = self.lib.get(13)
        for stage, boundary in ((1, 0), (0, 1), (1, 1)):
            self.lib.pending(stage, boundary)
            self.lib.endpoint(3, 0, 1)
            self.assertEqual(self.lib.get(13), prior)
            self.assertEqual([self.lib.get(i) for i in (14, 15)], [0, 0])
        self.lib.pending(0, 0)
        self.lib.serial(8)
        self.lib.endpoint(3, 0, 1)
        self.assertEqual(self.lib.get(13), prior)
        self.assertEqual([self.lib.get(i) for i in (14, 15)], [0, 0])

    def test_settled_tas_continuation_keeps_assistance_and_original_endpoint(self):
        self.lib.frame(4, 10, 1)
        self.capture(0)
        self.restore(0, 4)
        prior = self.lib.get(13)
        self.lib.pending(1, 0)
        self.lib.endpoint(3, 0, 1)
        self.lib.pending(0, 1)
        self.lib.endpoint(3, 0, 1)
        self.lib.pending(0, 0)
        self.lib.endpoint(3, 0, 1)
        self.assertEqual(self.lib.get(13), prior + 1)
        self.assertEqual([self.lib.get(i) for i in (14, 15, 16)], [1, 0, 1])
        self.assertEqual(self.lib.get(5) & 0x21, 0x21)

    def test_export_keeps_tas_flags_prefix_bounds_and_existing_disqualification(self):
        source = (ROOT / "src/ghost.cpp").read_text()
        exported = function(source, "bool exportLatest(")
        for line in ("const SaveSelection selection = latestSaveableTrack();",
                     "header.runFlags = track->runFlags;", "header.startQf = track->startQf;",
                     "header.endQf = track->endQf;", "header.sampleCount = track->count;",
                     "validCanonicalFile(bytes, fileSize, &checked)"):
            self.assertIn(line, exported)
        name = function(source, "void formatTrackName(")
        self.assertIn('(track.runFlags & SUSAMUNE_GHOST_RUN_TAS) ? "TAS " : ""', name)
        restored = function((ROOT / "src/iling.cpp").read_text(), "void onSavestateLoaded()")
        self.assertIn("clearAttempt();", restored)
        helper = function((ROOT / "src/iling.cpp").read_text(), "void updateSavestateGhostEndpoint(")
        for forbidden in ("recordResult", "armAttempt", "StageLoader", "SplitStats", "sRunning ="):
            self.assertNotIn(forbidden, helper)


if __name__ == "__main__":
    unittest.main()
