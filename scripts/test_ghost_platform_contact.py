"""Run Watch contact ownership and the retail rail-platform rider predicate."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_iling_attempt_lifecycle import function

ROOT=Path(__file__).resolve().parents[1]


class WatchPlatformTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='moonshine-watch-contact-')
        cls.addClassCleanup(cls.temp.cleanup)
        source=(ROOT/'src/ghost.cpp').read_text()
        code=r'''
extern "C" int _fltused=0;
using u8=unsigned char;using u16=unsigned short;using u32=unsigned;using s16=short;using f32=float;
struct TVec3f{float x,y,z;void set(float a,float b,float c){x=a;y=b;z=c;}};
using Vec=TVec3f;struct TVec3s{short x,y,z;};
struct TBGCheckData{void*owner;void*getActor()const{return owner;}}illegal,originalFloor,platformFloor;
struct TMario{
    enum{STATE_IDLE=0x0c400201,ANIMATION_IDLE=0xc3};
    u16 mPerformFlags;struct{bool mIsVisible;}mAttributes,mPrevAttributes;
    TVec3f mTranslation,mLastPosition,mLastPos,mLastGroundedPos,mSpeed,mPrevSpeed;
    f32 mForwardSpeed;TVec3s mAngle;s16 mModelAngleY;
    const TBGCheckData*mFloorTriangle;f32 mFloorBelow;u32 mState;
}mario,replacement;
TMario*gpMarioOriginal=&mario;
struct TMarDirector{enum{STATE_NORMAL=4};u8 mCurState;}director;
TMarDirector*gpMarDirector=&director;
struct CPolarSubCamera{void addMoveCameraAndMario(Vec) {}}camera;
CPolarSubCamera*gpCamera=&camera;
bool two,sGhostVisible,sSecondaryGhostVisible;
TVec3f sGhostPosition,sSecondaryGhostPosition;s16 sGhostYaw,sSecondaryGhostYaw;
u16 sGhostAnimationId;
constexpr u16 kCueMove=1,kCueEntry=0x200;
bool observerHasTwo(){return two;}
struct Map{f32 height;int queries;TBGCheckData*floor;
    const TBGCheckData*getIllegalCheckData(){return &illegal;}
    f32 checkGround(f32,f32 y,f32,const TBGCheckData**out){++queries;
        if(y>=height){*out=floor;return height;}*out=&illegal;return -32768;}
}map;
Map*gpMap=&map;
'''
        a=source.index('TMario *sObserverMario;');b=source.index('bool sObserverPastEnd;',a)
        code+=source[a:b]
        for name in ('releaseObserverMario','captureObserverMarioPose','bindObserverMario',
                     'finalizeObserverMarioBaseline','updateObserverGround','anchorObserverMario'):
            code+=function(source,name)
        # Exact retail standing predicate. This starts the rail in Sirena/Bianco
        # secrets; movement/timers remain owned by the retail platform itself.
        code+=r'''
constexpr u32 MARIO_STATUS_FLAG_UNK200=0x200,MARIO_STATUS_FLAG_UNK200000=0x200000;
const TBGCheckData*SMS_GetMarioGrPlane(){return mario.mFloorTriangle;}
bool SMS_IsMarioTouchGround4cm(){return mario.mTranslation.y<=mario.mFloorBelow+4;}
TMario*SMS_GetMarioHitActor(){return &mario;}
u32 SMS_GetMarioStatus(TMario*m){return m->mState;}
struct TRailMapObj{int flags;bool checkRailFlag(int v){return flags&v;}
 void onRailFlag(int v){flags|=v;}void offRailFlag(int v){flags&=~v;}
 bool checkMarioRiding();}platform;
'''
        # Audited against doldecomp/sms src/MoveBG/MapObjRailBlock.cpp,
        # TRailMapObj::checkMarioRiding. Keep this independent retail predicate
        # here so CI does not require an untracked decomp checkout or game ISO.
        code+=r'''
bool TRailMapObj::checkMarioRiding(){
    const TBGCheckData* data=SMS_GetMarioGrPlane();
    if(!checkRailFlag(1)){
        if(data&&data->getActor()==this&&SMS_IsMarioTouchGround4cm()){
            u32 status=SMS_GetMarioStatus(SMS_GetMarioHitActor());
            if((status&MARIO_STATUS_FLAG_UNK200)&&!(status&MARIO_STATUS_FLAG_UNK200000)){
                onRailFlag(1);offRailFlag(2);
            }
        }
    }else if(!data||data->getActor()!=this||!SMS_IsMarioTouchGround4cm())offRailFlag(1);
    return checkRailFlag(1);
}
'''
        code+=r'''
#define API extern "C" __declspec(dllexport)
API void reset(){
    mario={};mario.mTranslation={10,20,30};mario.mFloorTriangle=&originalFloor;
    mario.mFloorBelow=19;mario.mState=0x02000880;mario.mAttributes.mIsVisible=true;
    mario.mPrevAttributes.mIsVisible=true;replacement=mario;gpMarioOriginal=&mario;
    gpMarDirector=&director;director.mCurState=4;gpCamera=&camera;
    gpMap=&map;map={20,0,&platformFloor};platformFloor.owner=&platform;platform.flags=2;
    sObserverMario=nullptr;sObserverMarioOwned=false;sObserverMarioBaselineFinalized=false;
    sObserverStageReady=true;sGhostVisible=true;sSecondaryGhostVisible=false;two=false;
    sGhostPosition=mario.mTranslation;sGhostYaw=44;sGhostAnimationId=0x72;
}
API void target(float x,float y,float z){sGhostPosition={x,y,z};}
API void animation(unsigned id){sGhostAnimationId=id;}
API void ground(float height){map.height=height;}
API void option(int k,int v){switch(k){case 0:two=v;sSecondaryGhostVisible=v;break;
 case 1:sGhostVisible=v;break;case 2:director.mCurState=v;break;case 3:sObserverStageReady=v;break;
 case 4:gpMap=v?&map:nullptr;break;case 5:gpMarioOriginal=v?&replacement:&mario;break;}}
API void tick(){anchorObserverMario();}
API void release(int restore){releaseObserverMario(restore);}
API int riding(){return platform.checkMarioRiding();}
API unsigned state(){return mario.mState;}
API int floor(){return mario.mFloorTriangle==&platformFloor?1:mario.mFloorTriangle==&originalFloor?2:0;}
API float height(){return mario.mFloorBelow;}
API int queries(){return map.queries;}
API int visible(){return mario.mAttributes.mIsVisible;}
API float coordinate(int n){return n==0?mario.mTranslation.x:n==1?mario.mTranslation.y:mario.mTranslation.z;}
'''
        path=Path(cls.temp.name)/'contact.cpp';path.write_text(code)
        proc=subprocess.run([str(ROOT/'toolchain/clang++.exe'),'--target=x86_64-pc-windows-msvc',
            '-shared','-nostdlib','-fuse-ld=lld','-Wl,/noentry','-O2',str(path),'-o',str(path.with_suffix('.dll'))],
            capture_output=True,text=True)
        if proc.returncode:raise RuntimeError(proc.stdout+proc.stderr)
        cls.lib=C.CDLL(str(path.with_suffix('.dll')))
        cls.lib.target.argtypes=[C.c_float]*3
        cls.lib.ground.argtypes=[C.c_float]
        cls.lib.height.restype=cls.lib.coordinate.restype=C.c_float
        cls.lib.state.restype=C.c_uint
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def setUp(self):self.lib.reset()

    def test_standing_on_recorded_floor_starts_retail_rail(self):
        self.lib.tick()
        self.assertEqual(self.lib.floor(),1)
        self.assertEqual(self.lib.height(),20)
        self.assertEqual(self.lib.riding(),1)
        self.assertEqual(self.lib.visible(),0)

    def test_crossing_platform_does_not_count_as_standing_until_settled(self):
        self.lib.target(15,20,30);self.lib.tick()
        self.assertEqual(self.lib.riding(),0)
        self.lib.tick()
        self.assertEqual(self.lib.riding(),1)

    def test_quick_landing_starts_rail_without_a_stationary_sample(self):
        # Exact adjacent poses from the reported PAL S4 20.020 ghost. Its
        # landing crosses almost 30 units while the recorded animation changes
        # to ANIM_LAEND, whose retail waiting status carries the rider bit.
        self.lib.target(-441.625,3700,-7908.875);self.lib.tick()
        self.lib.ground(3700);self.lib.animation(0x57)
        self.lib.target(-442.25,3700,-7938.75);self.lib.tick()
        self.assertEqual(self.lib.riding(),1)

    def test_moving_idle_and_landing_poses_can_acquire_a_platform(self):
        for animation in (0xc3,0x4b,0x4e,0x57):
            self.lib.reset();self.lib.animation(animation)
            self.lib.target(15,20,30);self.lib.tick()
            self.assertEqual(self.lib.riding(),1,hex(animation))

    def test_retail_ground_query_margin_finds_rising_platform(self):
        self.lib.animation(0xc3);self.lib.ground(32)
        self.lib.target(15,20,30);self.lib.tick()
        self.assertEqual((self.lib.floor(),self.lib.riding()),(1,1))
        self.lib.ground(46);self.lib.tick()
        self.assertEqual(self.lib.riding(),0)

    def test_landing_animation_in_air_does_not_invent_floor_contact(self):
        self.lib.animation(0x57);self.lib.target(15,25,30);self.lib.tick()
        self.assertEqual(self.lib.riding(),0)

    def test_airborne_or_gap_releases_existing_rider(self):
        for gap in (False,True):
            self.lib.reset();self.lib.tick();self.assertEqual(self.lib.riding(),1)
            if gap:self.lib.option(1,0)
            else:self.lib.target(10,25,30)
            self.lib.tick();self.assertEqual(self.lib.riding(),0)

    def test_original_actor_floor_state_and_pose_return_on_exit(self):
        self.lib.tick();self.lib.target(12,23,35);self.lib.tick();self.lib.release(1)
        self.assertEqual((self.lib.floor(),self.lib.height(),self.lib.state(),self.lib.visible()),
                         (2,19,0x02000880,1))
        self.assertEqual([self.lib.coordinate(i)for i in range(3)],[10,20,30])

    def test_replacement_actor_never_receives_old_floor_pointer(self):
        self.lib.tick();self.lib.option(5,1);self.lib.release(1)
        self.assertEqual(self.lib.floor(),1) # old actor was not restored via a new identity

    def test_two_ghost_camera_midpoint_never_gets_invented_contact(self):
        self.lib.option(0,1);self.lib.tick()
        self.assertEqual((self.lib.floor(),self.lib.height(),self.lib.state(),self.lib.queries()),
                         (0,-32768,0x02000880,0))

    def test_losing_map_or_switching_to_two_clears_borrowed_contact(self):
        for option,value in ((0,1),(4,0)):
            self.lib.reset();self.lib.tick();self.assertEqual(self.lib.riding(),1)
            self.lib.option(option,value);self.lib.tick()
            self.assertEqual(self.lib.riding(),0)
            self.assertEqual(self.lib.height(),-32768)

    def test_loading_menu_hold_and_missing_map_do_not_query_collision(self):
        for option,value in ((2,0),(2,12),(3,0),(4,0)):
            self.lib.reset();self.lib.option(option,value);self.lib.tick()
            self.assertEqual(self.lib.queries(),0,(option,value))

    def test_racing_cannot_enter_observer_anchor(self):
        source=(ROOT/'src/ghost.cpp').read_text()
        hook=function(source,'beforeDirect')
        self.assertLess(hook.index('!observerRunning()'),hook.index('anchorObserverMario()'))


if __name__=='__main__':unittest.main()
