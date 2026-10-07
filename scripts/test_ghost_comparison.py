"""Production ghost target snapshots and split-overlay delta regressions."""
from pathlib import Path
import ctypes
import subprocess
import tempfile
import unittest
from test_ghost_tas_clock import function

ROOT = Path(__file__).resolve().parents[1]


class GhostComparisonTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        folder = Path(cls.folder.name)
        ghost = (ROOT / 'src/ghost.cpp').read_text()
        splits = (ROOT / 'src/split_stats.cpp').read_text()
        shim = r'''
#include "susamune/ghost_clock.h"
#include "susamune/ghost_format.h"
#include "susamune/susamune_cfg.h"
typedef unsigned int u32; typedef unsigned short u16;
typedef unsigned char u8; typedef int s32; typedef short s16;
void *memcpy(void *d,const void*s,unsigned long long n) {
 u8*to=(u8*)d;const u8*from=(const u8*)s;while(n--)*to++=*from++;return d;
}
struct Timer { u32 serial; u32 attemptSerial(){return serial;} } gQFTTimer;
enum { SETTING_SPLIT_COMPARISON };
struct Settings { u8 comparison; u8 get(int){return comparison;} } gSettings;
namespace ILing { int pbProfile(){return 0;} }
namespace SplitStats { enum { ROUTE_COUNT=132 }; }
namespace Ghost {
struct Track { bool valid,completed; u8 sourceRegion,splitCount; u32 startQf,endQf;
 SusamuneGhostSplitSample splits[6]; u32 resultQf; int parentEpisode;
 u8 area,episode,routeParentArea,routeFlags; u16 formatVersion;
} sPlayback;
enum RaceSource {RACE_SOURCE_NONE,RACE_SOURCE_PERSONAL,RACE_SOURCE_IMPORTED};
struct RaceContext {u32 attemptSerial,targetQf; s32 startingPbQf,routeVariant;
 s16 ilEntry;u8 area,episode,routeParentArea,routeFlags;RaceSource source;
} sRaceContext;
bool sRaceContextValid,sPlaybackPinned;
RaceSource sPlaybackRaceSource;
u32 sPlaybackRaceToken,sPlaybackToken,sAttemptSerial,sRaceContextPlaybackToken;
u8 sLiveArea,sLiveEpisode,sLiveRouteParentArea; s32 sLiveParentEpisode;
SusamuneGhostSplitSample sRaceSplits[6];u8 sRaceSplitCount;
u32 sRaceSplitSerial,sRaceSplitPlaybackToken;
SusamuneGhostClock sRecordClock;
bool compatible;
u8 runningRegion(){return 1;}
bool playbackOwnsCourse(u8,u8,s32,u8){return compatible;}
void captureSplit(u16,u8,s32){}
'''
        body = '\n'.join(function(ghost, signature) for signature in (
            'void captureComparisonSplits(', 'void captureRaceContext(',
            'bool comparisonSplit(', 'bool comparisonDelta('))
        export_body = ghost[ghost.index('    u8 splitCount = track->splitCount;'):ghost.index('    const bool teaching = inputCount != 0 || splitCount != 0;')]
        body += '\nu8 exportSplits(const Track *track) {\n' + export_body + 'return splitCount; }\n'
        overlay = r'''
}
''' + splits[splits.index('struct RegionPayload {'):splits.index('struct Runtime {')] + r'''
enum {FLAG_ATTEMPT_ACTIVE=1,FLAG_ATTEMPT_ELIGIBLE=2};
enum OverlayColor {OVERLAY_RED,OVERLAY_WHITE,OVERLAY_GREEN,OVERLAY_GOLD};
struct RouteDesc {u16 firstSegment;u8 entry,checkpointCount;} kRoutes[132];
struct State {u8 flags,activeRoute,expectedEvent,activeProfile,candidateGoldMask;
 s32 lastSplitQf;u32 attemptQf[6];RegionPayload payload;
} state,*sState=&state;
u8 kRegion=1;
u8 segmentCount(const RouteDesc&r){return r.checkpointCount+1;}
bool shownTarget; s32 shownDelta,shownAnchor;int shownColor;
void armOverlay(OverlayColor c,bool have,s32 delta,s32 anchor){
 shownTarget=have;shownDelta=delta;shownAnchor=anchor;shownColor=c;
}
'''
        wrapper = r'''
extern "C" {
__declspec(dllexport) void reset(int pinned,int complete,int count,int source){
 using namespace Ghost;
 gQFTTimer.serial=11;sAttemptSerial=11;sPlaybackToken=7;sPlaybackRaceToken=7;
 sRaceSplitCount=0;sRaceSplitSerial=0;sRaceSplitPlaybackToken=0;
 sPlayback.valid=true;sPlayback.completed=complete!=0;sPlayback.sourceRegion=1;
 sPlayback.splitCount=count;sPlaybackPinned=pinned!=0;compatible=true;
 sPlaybackRaceSource=(RaceSource)source;sRaceContextValid=false;
 sPlayback.startQf=0;sPlayback.endQf=720;sPlayback.resultQf=360;sPlayback.parentEpisode=-1;
 sPlayback.area=2;sPlayback.episode=0;sPlayback.routeParentArea=255;sPlayback.routeFlags=0;
 for(int i=0;i<6;i++){sPlayback.splits[i].qf=120*(i+1);
  sPlayback.splits[i].route=0;sPlayback.splits[i].endpoint=i;
  sPlayback.splits[i].schema=SUSAMUNE_SPLIT_STATS_SCHEMA_HASH;}
 sRecordClock.ready=false;sRecordClock.omittedQf=0;
 state.flags=FLAG_ATTEMPT_ACTIVE;state.activeRoute=0;state.expectedEvent=0;
 state.activeProfile=0;state.lastSplitQf=0;state.candidateGoldMask=0;
 kRoutes[0].firstSegment=0;kRoutes[0].checkpointCount=2;
 gSettings.comparison=3;shownTarget=false;shownDelta=0;shownAnchor=-1;
}
__declspec(dllexport) void snapshot(){Ghost::captureRaceContext();}
__declspec(dllexport) int raceAwardContext(){return Ghost::sRaceContextValid;}
__declspec(dllexport) int checkpoint(int endpoint,int qf){
 return captureSegment(0,endpoint,qf)?(shownTarget?1:0):-1;
}
__declspec(dllexport) int delta(){return shownDelta;}
__declspec(dllexport) int anchor(){return shownAnchor;}
__declspec(dllexport) int target(int route,int endpoint){
 s32 qf=-1;return Ghost::comparisonSplit(route,endpoint,&qf)?qf:-1;
}
__declspec(dllexport) void change(int option,int value){using namespace Ghost;
 switch(option){case 0:compatible=value!=0;break;case 1:sPlayback.sourceRegion=value;break;
 case 2:sPlayback.splits[0].schema=value;break;case 3:sPlaybackToken=value;break;
 case 4:gQFTTimer.serial=value;break;case 5:sPlayback.splits[0].qf=value;break;
 case 6:sRecordClock.ready=true;sRecordClock.omittedQf=value;break;
 case 7:sPlayback.splits[0].route=value;break;case 8:sPlayback.valid=value!=0;break;
 case 9:sPlayback.splits[0].endpoint=value;break;case 10:gSettings.comparison=value;break;
 case 11:sPlayback.splits[1].route=value;break;case 12:sPlayback.splits[1].qf=value;break;
 case 13:sPlayback.endQf=value;break;case 14:sPlayback.startQf=value;break;}
}
__declspec(dllexport) int exportCount(){return Ghost::exportSplits(&Ghost::sPlayback);}
__declspec(dllexport) void pb(int first,int second){
 state.payload.pbQf[0][0]=first;state.payload.pbQf[0][1]=second;
 state.payload.bestQf[0]=first;state.payload.bestQf[1]=second;
}
}
'''
        cpp, dll = folder/'comparison.cpp', folder/'comparison.dll'
        cpp.write_text(shim+body+overlay+function(splits,'bool captureSegment(')+wrapper)
        result = subprocess.run([str(ROOT/'toolchain/clang++.exe'),
            '--target=x86_64-pc-windows-msvc','-shared','-nostdlib','-fuse-ld=lld',
            '-Wl,/noentry','-fno-exceptions','-fno-rtti','-I',str(ROOT/'include'),
            str(cpp),'-o',str(dll)],capture_output=True,text=True)
        if result.returncode: raise AssertionError(result.stderr)
        cls.lib=ctypes.CDLL(str(dll))

    @classmethod
    def tearDownClass(cls):
        handle=cls.lib._handle;del cls.lib
        ctypes.windll.kernel32.FreeLibrary(ctypes.c_void_p(handle))
        cls.folder.cleanup()

    def test_automatic_last_success_supplies_real_overlay_deltas(self):
        self.lib.reset(0,1,3,0);self.lib.snapshot()
        self.assertEqual(self.lib.raceAwardContext(),0)
        for endpoint,qf,delta in [(0,108,-12),(1,264,24),(2,360,0)]:
            self.assertEqual(self.lib.checkpoint(endpoint,qf),1)
            self.assertEqual(self.lib.delta(),delta)
            self.assertEqual(self.lib.anchor(),qf)

    def test_partial_last_attempt_compares_only_captured_prefix(self):
        self.lib.reset(0,0,1,0);self.lib.snapshot()
        self.assertEqual(self.lib.checkpoint(0,100),1)
        self.assertEqual(self.lib.delta(),-20)
        self.assertEqual(self.lib.checkpoint(1,240),0)
        self.assertEqual(self.lib.raceAwardContext(),0)

    def test_personal_and_imported_race_context_stays_separate(self):
        for source in (1,2):
            self.lib.reset(1,1,3,source);self.lib.snapshot()
            self.assertEqual(self.lib.raceAwardContext(),1)
            self.assertEqual(self.lib.target(0,0),120)

    def test_snapshot_waits_for_same_targets_settled_route(self):
        self.lib.reset(1,1,3,1);self.lib.change(0,0);self.lib.snapshot()
        self.assertEqual(self.lib.target(0,0),-1)
        self.lib.change(0,1)
        self.assertEqual(self.lib.target(0,0),120)
        self.lib.change(5,900)
        self.assertEqual(self.lib.target(0,0),120)
        self.lib.change(3,8)
        self.assertEqual(self.lib.target(0,0),-1)

    def test_old_schema_route_and_serial_do_not_invent_targets(self):
        self.lib.reset(1,1,0,1);self.lib.snapshot()
        self.assertEqual(self.lib.checkpoint(0,120),0)
        for option,value in [(2,123),(7,1),(8,0),(9,1)]:
            self.lib.reset(0,1,3,0);self.lib.change(option,value);self.lib.snapshot()
            self.assertEqual(self.lib.target(0,0),-1)
        self.lib.reset(0,1,3,0);self.lib.snapshot();self.lib.change(4,12)
        self.assertEqual(self.lib.target(0,0),-1)

    def test_pal_bianco_three_secret_import_keeps_shared_qf_targets(self):
        self.lib.reset(1,1,2,2)
        self.lib.change(1,2)  # PAL source, foreign to this running-region shim.
        self.lib.change(7,15);self.lib.change(11,15)
        self.lib.change(5,712);self.lib.change(12,1700);self.lib.change(13,1700)
        self.lib.snapshot()
        self.assertEqual(self.lib.target(15,0),712)
        self.assertEqual(self.lib.target(15,1),1700)
        self.assertEqual(self.lib.exportCount(),2)
        self.lib.change(13,1699)
        self.assertEqual(self.lib.exportCount(),1)
        self.lib.change(14,713)
        self.assertEqual(self.lib.exportCount(),0)

    def test_tas_delta_omits_hold_but_keeps_raw_timer_anchor(self):
        self.lib.reset(0,1,3,0);self.lib.snapshot();self.lib.change(6,1000)
        self.assertEqual(self.lib.checkpoint(0,1116),1)
        self.assertEqual(self.lib.delta(),-4)
        self.assertEqual(self.lib.anchor(),1116)

    def test_pb_and_sob_keep_existing_cumulative_raw_qf(self):
        for comparison in (1,2):
            self.lib.reset(0,1,3,0);self.lib.snapshot();self.lib.change(6,1000)
            self.lib.change(10,comparison);self.lib.pb(100,150)
            self.assertEqual(self.lib.checkpoint(0,110),1)
            self.assertEqual(self.lib.delta(),10)
            self.assertEqual(self.lib.checkpoint(1,246),1)
            self.assertEqual(self.lib.delta(),-4)


if __name__=='__main__': unittest.main()
