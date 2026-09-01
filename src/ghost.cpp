#include "susamune/ghost.hxx"

#include "Dolphin/MTX.h"
#include "Dolphin/OS.h"
#include "Dolphin/math.h"
#include "Dolphin/mem.h"
#include "Dolphin/printf.h"
#include "Dolphin/string.h"
#include "SMS/Camera/PolarSubCamera.hxx"
#include "SMS/Manager/FlagManager.hxx"
#include "SMS/Player/Mario.hxx"
#include "SMS/Player/MarioDraw.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/addresses.hxx"
#include "susamune/actions.hxx"
#include "susamune/binds.hxx"
#include "susamune/checksum.hxx"
#include "susamune/ghost_format.h"
#include "susamune/ghost_model.hxx"
#include "susamune/iling.hxx"
#include "susamune/mem2_map.h"
#include "susamune/menu.hxx"
#include "susamune/features.hxx"
#include "susamune/qft_timer.hxx"
#include "susamune/records.hxx"
#include "susamune/settings.hxx"
#include "susamune/stage_loader.hxx"
#include "susamune/warp_wheel.hxx"

namespace Ghost {

namespace {

const s32 kPositionScale = SUSAMUNE_GHOST_POSITION_SCALE;
const s32 kMaxPosition = 1000000;
const s32 kMaxDurationQf = 107892;  // 15 minutes at 120000/1001 QF/s
const u16 kClockSettleObservations = 30;
const u16 kMaxSegments = SUSAMUNE_GHOST_V4_MAX_SEGMENTS;
const u32 kPlaybackTokenBit = 0x80000000u;
const u16 kCueMove = 0x0001u;
const u16 kCueEntry = 0x0200u;

enum RecordFailure {
    RECORD_FAILURE_NONE,
    RECORD_FAILURE_MARIO,
    RECORD_FAILURE_CLOCK_REGRESSION,
    RECORD_FAILURE_CLOCK_GAP,
    RECORD_FAILURE_POSITION,
    RECORD_FAILURE_ANIMATION,
    RECORD_FAILURE_ASSIST,
    RECORD_FAILURE_SEGMENTS,
};

enum ClockPhase {
    CLOCK_UNAVAILABLE,
    CLOCK_PROVISIONAL,
    CLOCK_ACTIVE,
    CLOCK_FINISHED,
    CLOCK_WAIT_STAGE,
};

enum ObserverPhase {
    OBSERVER_OFF,
    OBSERVER_PREPARING_ONE,
    OBSERVER_PREPARING_TWO,
    OBSERVER_WARPING_ONE,
    OBSERVER_WARPING_TWO,
    OBSERVER_ACTIVE_ONE,
    OBSERVER_ACTIVE_TWO,
};

typedef SusamuneGhostPoseSample Sample;
static_assert(sizeof(Sample) == SUSAMUNE_GHOST_POSE_SAMPLE_SIZE,
              "ghost sample layout changed");

struct Segment {
    u32 firstSample;
    u32 sampleCount;
    u32 startQf;
    u32 endQf;
    s32 routeVariant;
    u8 routeArea;
    u8 routeEpisode;
    u8 routeParentArea;
    u8 routeFlags;
    u32 reserved0;
    u32 reserved1;
};
static_assert(sizeof(Segment) == 32, "ghost segment layout changed");
static_assert(sizeof(Segment) == sizeof(SusamuneGhostSegment),
              "runtime and canonical ghost segments differ");

const u32 kMaxSamples = SUSAMUNE_GHOST_MAX_SAMPLE_COUNT;
const u32 kSegmentTableOffset =
    SUSAMUNE_GHOST_SLOT_SIZE - kMaxSegments * sizeof(Segment);
static_assert(kMaxSamples * sizeof(Sample) <= SUSAMUNE_GHOST_SLOT_SIZE,
              "ghost samples do not fit their MEM2 slot");
static_assert(kMaxSamples * sizeof(Sample) <= kSegmentTableOffset,
              "ghost samples overlap their segment table");
static_assert(kSegmentTableOffset + kMaxSegments * sizeof(Segment) <=
                  SUSAMUNE_GHOST_SLOT_SIZE,
              "ghost segments do not fit their MEM2 slot");

struct Track {
    Sample *samples;
    Segment *segments;
    u32 count;
    u32 startQf;
    u32 endQf;
    u32 resultQf;
    u32 runFlags;
    u32 pbToken;
    s32 parentEpisode;
    u8 area;
    u8 episode;
    u8 routeParentArea;
    u8 routeFlags;
    u16 segmentCount;
    u16 formatVersion;
    u16 attachmentFlags;
    u8 attachmentCount;
    SusamuneGhostAttachmentDescriptor
        attachments[SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_COUNT];
    bool valid;
    bool completed;
    bool saved;
    bool pb;
    u8 failure;
};

Track sRecord;
Track sPlayback;
// Watch 2 borrows the record slot's MEM2 payload, but its metadata must not
// participate in recording, PB, or storage-ack ownership.
Track sObserverSecondary;
u32 sAttemptSerial;
s32 sLastSampleQf;
u32 sPlaybackCursor;
s32 sPlaybackCursorQf;
u16 sPlaybackSegment;
bool sRecording;
bool sGhostVisible;
bool sStageRoutePending;
bool sPendingHadLiveRoute;
bool sPendingContinueRecording;
bool sBoundaryPending;
bool sLiveRouteValid;
bool sPlaybackPinned;
ClockPhase sClockPhase;
u16 sClockObservations;
u8 sLiveArea;
u8 sLiveEpisode;
u8 sLiveRouteParentArea;
u8 sLiveRouteFlags;
s32 sClockLastQf;
s32 sClockEpochStartQf;
s32 sLiveParentEpisode;
s32 sPendingPreviousClockQf;
s32 sBoundaryPriorQf;
u16 sBoundaryBaseSegmentCount;
TVec3f sGhostPosition;
s16 sGhostYaw;
u16 sGhostAnimationId;
u16 sGhostAnimationPhase;
u32 sGhostHeldObjectId;
u16 sGhostHeldNameKey;
u8 sGhostYoshi;
u32 sGhostIdSerial;
u32 sRecordToken;
u32 sRecordIdentityToken;
u32 sPlaybackToken;
u32 sPlaybackOriginRecordToken;
u32 sPBTokenSerial;
ObserverPhase sObserverPhase;
bool sObserverPrimaryReady;
bool sObserverSecondaryReady;
u16 sObserverPrimarySegment;
u16 sObserverSecondarySegment;
u32 sObserverSecondaryCursor;
s32 sObserverSecondaryCursorQf;
s32 sObserverBaseQf;
s32 sObserverEndQf;
s32 sObserverLastQf;
s32 sObserverQfOffset;
s32 sObserverStageAnchorQf;
bool sObserverClockReady;
TVec3f sSecondaryGhostPosition;
s16 sSecondaryGhostYaw;
u16 sSecondaryGhostAnimationId;
u16 sSecondaryGhostAnimationPhase;
u32 sSecondaryGhostHeldObjectId;
u16 sSecondaryGhostHeldNameKey;
u8 sSecondaryGhostYoshi;
bool sSecondaryGhostVisible;
TMario *sObserverMario;
CPolarSubCamera *sObserverCamera;
f32 sObserverCameraYOffset;
u16 sObserverMarioPerformFlags;
bool sObserverMarioVisible;
bool sObserverMarioPrevVisible;
bool sObserverMarioOwned;
bool sObserverMarioBaselineFinalized;
TVec3f sObserverMarioTranslation;
TVec3f sObserverMarioLastPosition;
TVec3f sObserverMarioLastPos;
TVec3f sObserverMarioLastGroundedPos;
TVec3f sObserverMarioSpeed;
TVec3f sObserverMarioPrevSpeed;
f32 sObserverMarioForwardSpeed;
TVec3s sObserverMarioAngle;
s16 sObserverMarioModelAngleY;
bool sObserverStageReady;
bool sObserverPastEnd;
bool sObserverRouteValidated;
bool sObserverContinuousClock;
bool sObserverCleanupWarp;
bool sObserverExitArmed;

s32 parentEpisode() {
    return TFlagManager::smInstance
        ? TFlagManager::smInstance->getFlag(0x40003u)
        : -1;
}

const Segment *lastSegment(const Track &track) {
    return track.segmentCount == 0
        ? nullptr
        : &track.segments[track.segmentCount - 1];
}

Segment *lastSegment(Track &track) {
    return track.segmentCount == 0
        ? nullptr
        : &track.segments[track.segmentCount - 1];
}

bool segmentRouteEquals(const Segment &segment, u8 area, u8 episode,
                        s32 parent, u8 parentArea, u8 flags) {
    return segment.routeArea == area && segment.routeEpisode == episode &&
           segment.routeVariant == parent &&
           segment.routeParentArea == parentArea &&
           segment.routeFlags == flags;
}

__attribute__((always_inline)) constexpr bool routeCourseEquals(
    u8 lhsArea, u8 lhsEpisode, s32 lhsParent, u8 lhsParentArea, u8 rhsArea,
    u8 rhsEpisode, s32 rhsParent, u8 rhsParentArea) {
    const bool lhsInternal =
        lhsParentArea != SUSAMUNE_GHOST_ROUTE_PARENT_NONE;
    const bool rhsInternal =
        rhsParentArea != SUSAMUNE_GHOST_ROUTE_PARENT_NONE;
    const u8 lhsCourse = lhsInternal ? lhsParentArea : lhsArea;
    const u8 rhsCourse = rhsInternal ? rhsParentArea : rhsArea;
    const s32 lhsCourseEpisode = lhsInternal ? lhsParent : lhsEpisode;
    const s32 rhsCourseEpisode = rhsInternal ? rhsParent : rhsEpisode;
    return lhsCourse == rhsCourse && lhsCourseEpisode == rhsCourseEpisode;
}

static_assert(routeCourseEquals(6, 3, 3, SUSAMUNE_GHOST_ROUTE_PARENT_NONE,
                                7, 0, 3, 6),
              "parent and child must share a logical course");
static_assert(routeCourseEquals(7, 0, 3, 6, 6, 3, 3,
                                SUSAMUNE_GHOST_ROUTE_PARENT_NONE),
              "child-start ghosts must match their parent course");
static_assert(!routeCourseEquals(6, 3, 3, SUSAMUNE_GHOST_ROUTE_PARENT_NONE,
                                 6, 4, 4,
                                 SUSAMUNE_GHOST_ROUTE_PARENT_NONE),
              "episodes must remain distinct logical courses");
static_assert(!routeCourseEquals(6, 3, 3, SUSAMUNE_GHOST_ROUTE_PARENT_NONE,
                                 4, 3, 3,
                                 SUSAMUNE_GHOST_ROUTE_PARENT_NONE),
              "stage families must remain distinct logical courses");

bool segmentCourseEquals(const Segment &segment, u8 area, u8 episode,
                         s32 parent, u8 parentArea) {
    return routeCourseEquals(segment.routeArea, segment.routeEpisode,
                             segment.routeVariant,
                             segment.routeParentArea, area, episode, parent,
                             parentArea);
}

bool playbackOwnsCourse(u8 area, u8 episode, s32 parent, u8 parentArea) {
    if (!sPlayback.valid) return false;
    for (u16 i = 0; i < sPlayback.segmentCount; i++) {
        if (segmentCourseEquals(sPlayback.segments[i], area, episode,
                                parent, parentArea)) {
            return true;
        }
    }
    return false;
}

bool neutralHub(u8 area) {
    return area == TGameSequence::AREA_DOLPIC;
}

bool recordRouteMatches(u8 area, u8 episode, s32 parent, u8 parentArea,
                        u8 routeFlags) {
    const Segment *segment = lastSegment(sRecord);
    return sRecord.valid && segment &&
           segmentRouteEquals(*segment, area, episode, parent, parentArea,
                              routeFlags);
}

bool pinSurvivesRoute(u8 area, u8 episode, s32 parent, u8 parentArea) {
    return sPlaybackPinned &&
           (neutralHub(area) ||
            playbackOwnsCourse(area, episode, parent, parentArea));
}

bool recordPromotableForRoute(bool routeMatches, bool boundaryReset) {
    return sRecord.valid &&
           (sRecord.completed ||
            (!gSettings.getBool(SETTING_GHOST_LAST_SUCCESS) &&
             (routeMatches || boundaryReset)));
}

u8 routeParentArea(u8 area) {
    return LevelWarp::parentArea(area);
}

void captureLiveRoute() {
    sLiveArea = gpApplication.mCurrentScene.mAreaID;
    sLiveEpisode = gpApplication.mCurrentScene.mEpisodeID;
    sLiveParentEpisode = parentEpisode();
    sLiveRouteParentArea = routeParentArea(sLiveArea);
    sLiveRouteFlags = sLiveRouteParentArea == SUSAMUNE_GHOST_ROUTE_PARENT_NONE
        ? 0
        : SUSAMUNE_GHOST_ROUTE_INTERNAL_SCENE;
    sLiveRouteValid = true;
}

bool liveRouteStorable() {
    return sLiveRouteValid &&
           sLiveArea <= SUSAMUNE_GHOST_ROUTE_AREA_MAX &&
           sLiveEpisode <= SUSAMUNE_GHOST_ROUTE_EPISODE_MAX &&
           sLiveParentEpisode >= SUSAMUNE_GHOST_ROUTE_VARIANT_NONE &&
           sLiveParentEpisode <= SUSAMUNE_GHOST_ROUTE_VARIANT_MAX &&
           (sLiveRouteParentArea == SUSAMUNE_GHOST_ROUTE_PARENT_NONE ||
            sLiveRouteParentArea <= SUSAMUNE_GHOST_ROUTE_AREA_MAX) &&
           (!(sLiveRouteFlags & SUSAMUNE_GHOST_ROUTE_INTERNAL_SCENE) ==
            (sLiveRouteParentArea == SUSAMUNE_GHOST_ROUTE_PARENT_NONE));
}

void clearTrack(Track &track) {
    track.count = 0;
    track.startQf = 0;
    track.endQf = 0;
    track.resultQf = SUSAMUNE_GHOST_RESULT_QF_NONE;
    track.runFlags = SUSAMUNE_GHOST_RUN_INCOMPLETE;
    track.pbToken = 0;
    track.parentEpisode = -1;
    track.area = 0xff;
    track.episode = 0xff;
    track.routeParentArea = SUSAMUNE_GHOST_ROUTE_PARENT_NONE;
    track.routeFlags = 0;
    track.segmentCount = 0;
    track.formatVersion = SUSAMUNE_GHOST_FILE_VERSION_V4;
    track.attachmentFlags = 0;
    track.attachmentCount = 0;
    memset(track.attachments, 0, sizeof(track.attachments));
    track.valid = false;
    track.completed = false;
    track.saved = false;
    track.pb = false;
    track.failure = RECORD_FAILURE_NONE;
}

void bumpRecordToken() {
    sRecord.saved = false;
    sRecordToken = (sRecordToken + 1) & ~kPlaybackTokenBit;
    if (sRecordToken == 0) sRecordToken++;
}

void bumpPlaybackToken() {
    sPlaybackOriginRecordToken = 0;
    sPlaybackToken = (sPlaybackToken + 1) & ~kPlaybackTokenBit;
    if (sPlaybackToken == 0) sPlaybackToken++;
}

u32 nextPBToken() {
    sPBTokenSerial++;
    if (sPBTokenSerial == 0) sPBTokenSerial++;
    return sPBTokenSerial;
}

void clearRecord() {
    clearTrack(sRecord);
    bumpRecordToken();
    sRecordIdentityToken = sRecordToken;
}

bool observerHasTwo() {
    return sObserverPhase == OBSERVER_PREPARING_TWO ||
           sObserverPhase == OBSERVER_WARPING_TWO ||
           sObserverPhase == OBSERVER_ACTIVE_TWO;
}

bool observerRunning() {
    return sObserverPhase == OBSERVER_WARPING_ONE ||
           sObserverPhase == OBSERVER_WARPING_TWO ||
           sObserverPhase == OBSERVER_ACTIVE_ONE ||
           sObserverPhase == OBSERVER_ACTIVE_TWO;
}

void releaseObserverMario(bool restore) {
    if (restore && sObserverMarioOwned && sObserverMario &&
        sObserverMario == gpMarioOriginal) {
        if (sObserverCamera && sObserverCamera == gpCamera &&
            gpMarDirector &&
            gpMarDirector->mCurState == TMarDirector::STATE_NORMAL &&
            sObserverCameraYOffset != 0.0f) {
            Vec cameraMove = {0.0f, -sObserverCameraYOffset, 0.0f};
            sObserverCamera->addMoveCameraAndMario(cameraMove);
        }
        sObserverMario->mPerformFlags = sObserverMarioPerformFlags;
        // A pre-normal abort must not preserve the visibility bit we cleared.
        const bool visible = sObserverMarioBaselineFinalized
            ? sObserverMarioVisible : true;
        const bool prevVisible = sObserverMarioBaselineFinalized
            ? sObserverMarioPrevVisible : true;
        sObserverMario->mAttributes.mIsVisible = visible;
        sObserverMario->mPrevAttributes.mIsVisible =
            prevVisible;
        sObserverMario->mTranslation = sObserverMarioTranslation;
        sObserverMario->mLastPosition = sObserverMarioLastPosition;
        sObserverMario->mLastPos = sObserverMarioLastPos;
        sObserverMario->mLastGroundedPos = sObserverMarioLastGroundedPos;
        sObserverMario->mSpeed = sObserverMarioSpeed;
        sObserverMario->mPrevSpeed = sObserverMarioPrevSpeed;
        sObserverMario->mForwardSpeed = sObserverMarioForwardSpeed;
        sObserverMario->mAngle = sObserverMarioAngle;
        sObserverMario->mModelAngleY = sObserverMarioModelAngleY;
    }
    sObserverMario = nullptr;
    sObserverCamera = nullptr;
    sObserverCameraYOffset = 0.0f;
    sObserverMarioOwned = false;
}

void bindObserverMario() {
    if (!gpMarioOriginal) return;
    if (sObserverMarioOwned && sObserverMario == gpMarioOriginal) return;
    releaseObserverMario(false);
    sObserverMario = gpMarioOriginal;
    sObserverCamera = gpCamera;
    sObserverCameraYOffset = 0.0f;
    sObserverMarioPerformFlags = sObserverMario->mPerformFlags;
    sObserverMarioVisible = sObserverMario->mAttributes.mIsVisible;
    sObserverMarioPrevVisible = sObserverMario->mPrevAttributes.mIsVisible;
    sObserverMarioTranslation = sObserverMario->mTranslation;
    sObserverMarioLastPosition = sObserverMario->mLastPosition;
    sObserverMarioLastPos = sObserverMario->mLastPos;
    sObserverMarioLastGroundedPos = sObserverMario->mLastGroundedPos;
    sObserverMarioSpeed = sObserverMario->mSpeed;
    sObserverMarioPrevSpeed = sObserverMario->mPrevSpeed;
    sObserverMarioForwardSpeed = sObserverMario->mForwardSpeed;
    sObserverMarioAngle = sObserverMario->mAngle;
    sObserverMarioModelAngleY = sObserverMario->mModelAngleY;
    sObserverMarioOwned = true;
    sObserverMarioBaselineFinalized = false;
    sObserverMario->mPerformFlags |= kCueEntry;
    sObserverMario->mAttributes.mIsVisible = false;
    sObserverMario->mPrevAttributes.mIsVisible = false;
}

void finalizeObserverMarioBaseline() {
    if (!sObserverMarioOwned || !sObserverMario ||
        sObserverMarioBaselineFinalized) return;
    // bindObserverMario already hid these bits during the intro. Once normal
    // gameplay begins, the state we must restore is Sunshine's visible Mario.
    sObserverMarioVisible = true;
    sObserverMarioPrevVisible = true;
    sObserverMarioTranslation = sObserverMario->mTranslation;
    sObserverMarioLastPosition = sObserverMario->mLastPosition;
    sObserverMarioLastPos = sObserverMario->mLastPos;
    sObserverMarioLastGroundedPos = sObserverMario->mLastGroundedPos;
    sObserverMarioSpeed = sObserverMario->mSpeed;
    sObserverMarioPrevSpeed = sObserverMario->mPrevSpeed;
    sObserverMarioForwardSpeed = sObserverMario->mForwardSpeed;
    sObserverMarioAngle = sObserverMario->mAngle;
    sObserverMarioModelAngleY = sObserverMario->mModelAngleY;
    sObserverMarioBaselineFinalized = true;
    sObserverMario->mAttributes.mIsVisible = false;
    sObserverMario->mPrevAttributes.mIsVisible = false;
}

void resetObserverRuntime() {
    releaseObserverMario(true);
    clearTrack(sObserverSecondary);
    sObserverPhase = OBSERVER_OFF;
    sObserverPrimaryReady = false;
    sObserverSecondaryReady = false;
    sObserverPrimarySegment = 0xffff;
    sObserverSecondarySegment = 0xffff;
    sObserverSecondaryCursor = 0;
    sObserverSecondaryCursorQf = 0;
    sObserverBaseQf = 0;
    sObserverEndQf = 0;
    sObserverLastQf = 0;
    sObserverQfOffset = 0;
    sObserverStageAnchorQf = -1;
    sObserverClockReady = false;
    sSecondaryGhostYaw = 0;
    sSecondaryGhostAnimationId = 0;
    sSecondaryGhostAnimationPhase = 0;
    sSecondaryGhostHeldObjectId = 0;
    sSecondaryGhostHeldNameKey = 0;
    sSecondaryGhostYoshi = SUSAMUNE_GHOST_V4_YOSHI_NONE;
    sSecondaryGhostVisible = false;
    sObserverStageReady = false;
    sObserverPastEnd = false;
    sObserverRouteValidated = false;
    sObserverContinuousClock = false;
    sObserverCleanupWarp = false;
    sObserverExitArmed = false;
}

void failRecording(RecordFailure failure, const char *message) {
    sRecording = false;
    sRecord.valid = false;
    sRecord.failure = static_cast<u8>(failure);
    bumpRecordToken();
    if (gMenu) gMenu->toast(message);
}

void stopAll() {
    clearRecord();
    clearTrack(sPlayback);
    bumpPlaybackToken();
    sPlaybackPinned = false;
    sRecording = false;
    sGhostVisible = false;
    sStageRoutePending = false;
    sPendingHadLiveRoute = false;
    sPendingContinueRecording = false;
    sBoundaryPending = false;
    sLiveRouteValid = false;
    sClockPhase = CLOCK_UNAVAILABLE;
    resetObserverRuntime();
}

bool fixedPosition(f32 value, s32 *fixed) {
    if (!fixed) return false;
    if (value != value || value < -static_cast<f32>(kMaxPosition) ||
        value > static_cast<f32>(kMaxPosition)) {
        return false;
    }
    *fixed = static_cast<s32>(value * static_cast<f32>(kPositionScale));
    return true;
}

u32 readU24(const u8 bytes[3]) {
    return (static_cast<u32>(bytes[0]) << 16) |
           (static_cast<u32>(bytes[1]) << 8) |
           static_cast<u32>(bytes[2]);
}

s32 readS24(const u8 bytes[3]) {
    const u32 raw = readU24(bytes);
    return static_cast<s32>((raw & 0x800000u) ? raw | 0xff000000u : raw);
}

void writeU24(u8 bytes[3], u32 value) {
    bytes[0] = static_cast<u8>(value >> 16);
    bytes[1] = static_cast<u8>(value >> 8);
    bytes[2] = static_cast<u8>(value);
}

void writeS24(u8 bytes[3], s32 value) {
    writeU24(bytes, static_cast<u32>(value) & 0xffffffu);
}

s32 sampleX(const Sample &sample) { return readS24(sample.x); }
s32 sampleY(const Sample &sample) { return readS24(sample.y); }
s32 sampleZ(const Sample &sample) { return readS24(sample.z); }

u16 sampleAnimationId(const Sample &sample) {
    return static_cast<u16>(readU24(sample.animation) >> 15);
}

u16 sampleAnimationPhase(const Sample &sample, u16 version) {
    const u32 packed = readU24(sample.animation);
    if (version == SUSAMUNE_GHOST_FILE_VERSION_V4) {
        const u32 phase = (packed >> SUSAMUNE_GHOST_V4_ANIMATION_PHASE_SHIFT) &
                          SUSAMUNE_GHOST_V4_ANIMATION_PHASE_MAX;
        return static_cast<u16>(phase * SUSAMUNE_GHOST_ANIMATION_PHASE_MAX /
                                SUSAMUNE_GHOST_V4_ANIMATION_PHASE_MAX);
    }
    return static_cast<u16>((packed >> 3) &
                            SUSAMUNE_GHOST_ANIMATION_PHASE_MAX);
}

u8 sampleYoshi(const Sample &sample, u16 version) {
    return version == SUSAMUNE_GHOST_FILE_VERSION_V4
        ? static_cast<u8>((readU24(sample.animation) >>
                           SUSAMUNE_GHOST_V4_YOSHI_SHIFT) &
                          SUSAMUNE_GHOST_V4_YOSHI_MASK)
        : SUSAMUNE_GHOST_V4_YOSHI_NONE;
}

u8 sampleHeld(const Sample &sample, u16 version) {
    return version == SUSAMUNE_GHOST_FILE_VERSION_V4
        ? static_cast<u8>(readU24(sample.animation) &
                          SUSAMUNE_GHOST_V4_HELD_INDEX_MASK)
        : 0;
}

void sampleAttachments(const Track &track, const Sample &sample, u8 *yoshi,
                       u32 *objectId, u16 *nameKey) {
    if (yoshi) *yoshi = sampleYoshi(sample, track.formatVersion);
    if (objectId) *objectId = 0;
    if (nameKey) *nameKey = 0;
    const u8 held = sampleHeld(sample, track.formatVersion);
    if (held == 0 || held == SUSAMUNE_GHOST_V4_HELD_UNKNOWN ||
        held > track.attachmentCount) {
        return;
    }
    const SusamuneGhostAttachmentDescriptor &descriptor =
        track.attachments[held - 1];
    if (objectId) {
        *objectId = (static_cast<u32>(descriptor.objectId[0]) << 24) |
                    (static_cast<u32>(descriptor.objectId[1]) << 16) |
                    (static_cast<u32>(descriptor.objectId[2]) << 8) |
                    static_cast<u32>(descriptor.objectId[3]);
    }
    if (nameKey) {
        *nameKey = static_cast<u16>(
            (static_cast<u16>(descriptor.nameKey[0]) << 8) |
            descriptor.nameKey[1]);
    }
}

void setSampleAnimation(Sample &sample, u16 id, u16 phase) {
    const u32 attachments = readU24(sample.animation) & 0x7fu;
    const u32 phaseV4 =
        (static_cast<u32>(phase) * SUSAMUNE_GHOST_V4_ANIMATION_PHASE_MAX +
         SUSAMUNE_GHOST_ANIMATION_PHASE_MAX / 2) /
        SUSAMUNE_GHOST_ANIMATION_PHASE_MAX;
    writeU24(sample.animation,
             (static_cast<u32>(id) << 15) |
             (phaseV4 << SUSAMUNE_GHOST_V4_ANIMATION_PHASE_SHIFT) |
             attachments);
}

void setSampleAttachments(Sample &sample, u8 yoshi, u8 held) {
    const u32 animation = readU24(sample.animation) & ~0x7fu;
    writeU24(sample.animation,
             animation |
             (static_cast<u32>(yoshi) << SUSAMUNE_GHOST_V4_YOSHI_SHIFT) |
             held);
}

bool attachmentDescriptorEquals(
    const SusamuneGhostAttachmentDescriptor &descriptor, u32 objectId,
    u16 nameKey) {
    return descriptor.objectId[0] == static_cast<u8>(objectId >> 24) &&
           descriptor.objectId[1] == static_cast<u8>(objectId >> 16) &&
           descriptor.objectId[2] == static_cast<u8>(objectId >> 8) &&
           descriptor.objectId[3] == static_cast<u8>(objectId) &&
           descriptor.nameKey[0] == static_cast<u8>(nameKey >> 8) &&
           descriptor.nameKey[1] == static_cast<u8>(nameKey);
}

void setAttachmentDescriptor(SusamuneGhostAttachmentDescriptor &descriptor,
                             u32 objectId, u16 nameKey) {
    descriptor.objectId[0] = static_cast<u8>(objectId >> 24);
    descriptor.objectId[1] = static_cast<u8>(objectId >> 16);
    descriptor.objectId[2] = static_cast<u8>(objectId >> 8);
    descriptor.objectId[3] = static_cast<u8>(objectId);
    descriptor.nameKey[0] = static_cast<u8>(nameKey >> 8);
    descriptor.nameKey[1] = static_cast<u8>(nameKey);
}

u8 captureYoshiState() {
    if (!gpMarioOriginal || !gpMarioOriginal->mYoshi ||
        gpMarioOriginal->mYoshi->mState != TYoshi::MOUNTED) {
        return SUSAMUNE_GHOST_V4_YOSHI_NONE;
    }
    const s8 color = gpMarioOriginal->mYoshi->mType;
    return color >= TYoshi::GREEN && color <= TYoshi::PINK
        ? static_cast<u8>(color + 1)
        : SUSAMUNE_GHOST_V4_YOSHI_UNKNOWN;
}

u8 captureHeldObject(Track &track) {
    if (!gpMarioOriginal || !gpMarioOriginal->mHeldObject) return 0;
    const TTakeActor *held = gpMarioOriginal->mHeldObject;
    if (held->mObjectID == 0 && held->mKeyCode == 0) {
        track.attachmentFlags |=
            SUSAMUNE_GHOST_V4_ATTACHMENT_HELD_OVERFLOW;
        return SUSAMUNE_GHOST_V4_HELD_UNKNOWN;
    }
    for (u8 i = 0; i < track.attachmentCount; i++) {
        if (attachmentDescriptorEquals(track.attachments[i], held->mObjectID,
                                       held->mKeyCode)) {
            return static_cast<u8>(i + 1);
        }
    }
    if (track.attachmentCount <
        SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_COUNT) {
        setAttachmentDescriptor(track.attachments[track.attachmentCount],
                                held->mObjectID, held->mKeyCode);
        return static_cast<u8>(++track.attachmentCount);
    }
    track.attachmentFlags |=
        SUSAMUNE_GHOST_V4_ATTACHMENT_HELD_OVERFLOW;
    return SUSAMUNE_GHOST_V4_HELD_UNKNOWN;
}

bool captureAnimation(u16 *idOut, u16 *phaseOut) {
    if (!idOut || !phaseOut || !gpMarioOriginal ||
        gpMarioOriginal->mAnimationID > SUSAMUNE_GHOST_ANIMATION_ID_MAX) {
        return false;
    }
    J3DFrameCtrl *frame = gpMarioOriginal->getMotionFrameCtrl();
    if (!frame || frame->mNumFrames <= 0 ||
        frame->mCurFrame != frame->mCurFrame) {
        return false;
    }

    f32 current = frame->mCurFrame;
    const f32 end = static_cast<f32>(frame->mNumFrames);
    if (current < 0.0f) current = 0.0f;
    if (current > end) current = end;
    u32 phase = static_cast<u32>(
        current * static_cast<f32>(
                      SUSAMUNE_GHOST_ANIMATION_PHASE_MAX + 1u) /
        end + 0.5f);
    if (phase > SUSAMUNE_GHOST_ANIMATION_PHASE_MAX) {
        phase = SUSAMUNE_GHOST_ANIMATION_PHASE_MAX;
    }
    *idOut = gpMarioOriginal->mAnimationID;
    *phaseOut = static_cast<u16>(phase);
    return true;
}

u16 interpolateAnimationPhase(u16 before, u16 after, s32 numerator,
                              s32 denominator) {
    const s32 period = SUSAMUNE_GHOST_ANIMATION_PHASE_MAX + 1;
    s32 delta = (static_cast<s32>(after) - before + period / 2) &
                (period - 1);
    delta -= period / 2;
    return static_cast<u16>((before +
        static_cast<s32>(static_cast<s64>(delta) * numerator / denominator)) &
        (period - 1));
}

bool appendSegment() {
    if (sRecord.segmentCount >= kMaxSegments) {
        sRecording = false;
        sRecord.failure = RECORD_FAILURE_SEGMENTS;
        if (gMenu) gMenu->toast("Ghost: segment capacity reached");
        return false;
    }
    Segment &segment = sRecord.segments[sRecord.segmentCount++];
    memset(&segment, 0, sizeof(segment));
    segment.firstSample = sRecord.count;
    segment.routeVariant = sLiveParentEpisode;
    segment.routeArea = sLiveArea;
    segment.routeEpisode = sLiveEpisode;
    segment.routeParentArea = sLiveRouteParentArea;
    segment.routeFlags = sLiveRouteFlags;
    if (sRecord.segmentCount == 1) {
        sRecord.area = sLiveArea;
        sRecord.episode = sLiveEpisode;
        sRecord.parentEpisode = sLiveParentEpisode;
        sRecord.routeParentArea = sLiveRouteParentArea;
        sRecord.routeFlags = sLiveRouteFlags;
    } else {
        sRecord.runFlags |= SUSAMUNE_GHOST_RUN_CUSTOM_ROUTE;
    }
    bumpRecordToken();
    return true;
}

void rollbackBoundarySegment() {
    if (sRecord.segmentCount <= sBoundaryBaseSegmentCount) return;
    while (sRecord.segmentCount > sBoundaryBaseSegmentCount) {
        memset(&sRecord.segments[--sRecord.segmentCount], 0,
               sizeof(Segment));
    }
    if (sRecord.segmentCount == 0) {
        sRecord.count = 0;
        sRecord.startQf = 0;
        sRecord.endQf = 0;
        sRecord.valid = false;
        sRecord.runFlags &= ~SUSAMUNE_GHOST_RUN_CUSTOM_ROUTE;
    } else {
        const Segment &last = sRecord.segments[sRecord.segmentCount - 1];
        sRecord.count = last.firstSample + last.sampleCount;
        sRecord.endQf = last.endQf;
        if (sRecord.segmentCount == 1) {
            sRecord.runFlags &= ~SUSAMUNE_GHOST_RUN_CUSTOM_ROUTE;
        }
    }
    sLastSampleQf = sRecord.segmentCount == 0
        ? 0
        : static_cast<s32>(sRecord.endQf);
    bumpRecordToken();
}

void dropLastEmptySegment() {
    Segment *segment = lastSegment(sRecord);
    if (!segment || segment->sampleCount != 0) return;
    memset(segment, 0, sizeof(*segment));
    sRecord.segmentCount--;
    if (sRecord.segmentCount <= 1) {
        sRecord.runFlags &= ~SUSAMUNE_GHOST_RUN_CUSTOM_ROUTE;
    }
    bumpRecordToken();
}

bool appendSample(s32 qf) {
    if (!gpMarioOriginal) {
        failRecording(RECORD_FAILURE_MARIO,
                      "Ghost: Mario disappeared");
        return false;
    }
    if (qf < 0) {
        failRecording(RECORD_FAILURE_CLOCK_REGRESSION,
                      "Ghost: negative QFT");
        return false;
    }

    Segment *segment = lastSegment(sRecord);
    if (!segment) {
        failRecording(RECORD_FAILURE_SEGMENTS,
                      "Ghost: missing route segment");
        return false;
    }

    if (sRecord.count != 0 &&
        qf - static_cast<s32>(sRecord.startQf) > kMaxDurationQf) {
        dropLastEmptySegment();
        sRecording = false;
        sClockPhase = CLOCK_FINISHED;
        if (gMenu) gMenu->toast("Ghost: 15 minute cap");
        return false;
    }
    if (sRecord.count >= kMaxSamples) {
        dropLastEmptySegment();
        sRecording = false;
        sClockPhase = CLOCK_FINISHED;
        if (gMenu) gMenu->toast("Ghost: sample capacity reached");
        return false;
    }

    const s32 delta = segment->sampleCount == 0 ? 0 : qf - sLastSampleQf;
    if (delta < 0) {
        failRecording(RECORD_FAILURE_CLOCK_REGRESSION,
                      "Ghost: sample clock regressed");
        return false;
    }
    if (delta > 0xffff) {
        failRecording(RECORD_FAILURE_CLOCK_GAP,
                      "Ghost: sample clock gap");
        return false;
    }
    if (segment->sampleCount != 0 && delta < 4) return true;

    s32 x;
    s32 y;
    s32 z;
    if (!fixedPosition(gpMarioOriginal->mTranslation.x, &x) ||
        !fixedPosition(gpMarioOriginal->mTranslation.y, &y) ||
        !fixedPosition(gpMarioOriginal->mTranslation.z, &z)) {
        failRecording(RECORD_FAILURE_POSITION,
                      "Ghost: invalid Mario position");
        return false;
    }
    u16 animationId;
    u16 animationPhase;
    if (!captureAnimation(&animationId, &animationPhase)) {
        failRecording(RECORD_FAILURE_ANIMATION,
                      "Ghost: invalid Mario animation");
        return false;
    }

    Sample &sample = sRecord.samples[sRecord.count];
    memset(&sample, 0, sizeof(sample));
    writeS24(sample.x, x);
    writeS24(sample.y, y);
    writeS24(sample.z, z);
    sample.yaw = gpMarioOriginal->mModelAngleY;
    sample.deltaQf = static_cast<u16>(delta);
    setSampleAnimation(sample, animationId, animationPhase);
    setSampleAttachments(sample, captureYoshiState(),
                         captureHeldObject(sRecord));
    if (segment->sampleCount == 0) {
        segment->startQf = static_cast<u32>(qf);
        if (sRecord.count == 0) sRecord.startQf = static_cast<u32>(qf);
    }
    sRecord.count++;
    segment->sampleCount++;
    sLastSampleQf = qf;
    sRecord.endQf = static_cast<u32>(qf);
    segment->endQf = static_cast<u32>(qf);
    if (sClockPhase == CLOCK_ACTIVE && sRecord.count >= 2) {
        sRecord.valid = true;
    }
    bumpRecordToken();
    return true;
}

void clearEpochSamples() {
    Segment *segment = lastSegment(sRecord);
    if (!segment) return;
    sRecord.count = segment->firstSample;
    segment->sampleCount = 0;
    segment->startQf = 0;
    segment->endQf = 0;
    if (sRecord.segmentCount == 1) {
        sRecord.startQf = 0;
        sRecord.endQf = 0;
        sRecord.valid = false;
    } else {
        sRecord.endQf = sRecord.segments[sRecord.segmentCount - 2].endQf;
    }
    sRecord.resultQf = SUSAMUNE_GHOST_RESULT_QF_NONE;
    sRecord.completed = false;
    sRecord.failure = RECORD_FAILURE_NONE;
    sLastSampleQf = 0;
    bumpRecordToken();
}

bool startEpochSamples(s32 qf) {
    clearEpochSamples();
    // QFT can first become readable partway through the materialise-in. Keep
    // that elapsed time by holding Mario's first observable pose from QF zero.
    const s32 anchor = sRecord.segmentCount == 1 ? 0 : qf;
    if (!appendSample(anchor)) return false;
    if (qf - anchor >=
        static_cast<s32>(SUSAMUNE_GHOST_TRANSFORM_INTERVAL_QF)) {
        return appendSample(qf);
    }
    return true;
}

void rewindPlayback() {
    sPlaybackCursor = 0;
    sPlaybackCursorQf = 0;
    sPlaybackSegment = 0xffff;
}

void rebaseClock(s32 qf) {
    if (sRecording) {
        if (sRecord.segmentCount > 1) {
            failRecording(RECORD_FAILURE_CLOCK_REGRESSION,
                          "Ghost: cross-stage QFT regression");
        } else {
            startEpochSamples(qf);
        }
    }
    rewindPlayback();
    sClockPhase = CLOCK_PROVISIONAL;
    sClockObservations = 1;
    sClockLastQf = qf;
    sClockEpochStartQf = qf;
}

s32 interpolateFixed(s32 a, s32 b, s32 numerator, s32 denominator) {
    return a + static_cast<s32>(
        static_cast<s64>(b - a) * numerator / denominator);
}

bool finishTrackAt(s32 qf) {
    Segment *segment = lastSegment(sRecord);
    if (!segment || segment->sampleCount == 0 ||
        qf < static_cast<s32>(segment->startQf) ||
        qf - static_cast<s32>(sRecord.startQf) > kMaxDurationQf) {
        return false;
    }

    u32 index = segment->firstSample;
    const u32 segmentEnd = segment->firstSample + segment->sampleCount;
    s32 sampleQf = static_cast<s32>(segment->startQf);
    while (index + 1 < segmentEnd) {
        const s32 nextQf = sampleQf + sRecord.samples[index + 1].deltaQf;
        if (nextQf == qf) {
            sRecord.count = index + 2;
            segment->sampleCount = sRecord.count - segment->firstSample;
            segment->endQf = static_cast<u32>(qf);
            sRecord.endQf = static_cast<u32>(qf);
            sLastSampleQf = qf;
            return true;
        }
        if (nextQf > qf) {
            const s32 numerator = qf - sampleQf;
            const s32 denominator = nextQf - sampleQf;
            if (numerator == 0) {
                sRecord.count = index + 1;
            } else {
                const Sample before = sRecord.samples[index];
                const Sample after = sRecord.samples[index + 1];
                Sample &terminal = sRecord.samples[index + 1];
                writeS24(terminal.x,
                         interpolateFixed(sampleX(before), sampleX(after),
                                          numerator, denominator));
                writeS24(terminal.y,
                         interpolateFixed(sampleY(before), sampleY(after),
                                          numerator, denominator));
                writeS24(terminal.z,
                         interpolateFixed(sampleZ(before), sampleZ(after),
                                          numerator, denominator));
                const s32 yawDelta = static_cast<s16>(
                    static_cast<u16>(after.yaw - before.yaw));
                terminal.yaw = static_cast<s16>(
                    before.yaw + yawDelta * numerator / denominator);
                const u16 beforeAnimation = sampleAnimationId(before);
                const u16 afterAnimation = sampleAnimationId(after);
                setSampleAnimation(
                    terminal, beforeAnimation,
                    beforeAnimation == afterAnimation
                        ? interpolateAnimationPhase(
                              sampleAnimationPhase(before, sRecord.formatVersion),
                              sampleAnimationPhase(after, sRecord.formatVersion), numerator,
                              denominator)
                        : sampleAnimationPhase(before, sRecord.formatVersion));
                terminal.deltaQf = static_cast<u16>(numerator);
                sRecord.count = index + 2;
            }
            segment->sampleCount = sRecord.count - segment->firstSample;
            segment->endQf = static_cast<u32>(qf);
            sRecord.endQf = static_cast<u32>(qf);
            sLastSampleQf = qf;
            return true;
        }
        sampleQf = nextQf;
        index++;
    }

    if (qf == sampleQf) {
        sRecord.count = index + 1;
        segment->sampleCount = sRecord.count - segment->firstSample;
        segment->endQf = static_cast<u32>(qf);
        sRecord.endQf = static_cast<u32>(qf);
        sLastSampleQf = qf;
        return true;
    }

    const s32 delta = qf - sampleQf;
    if (delta <= 0 || delta > 0xffff || sRecord.count >= kMaxSamples) {
        return false;
    }
    Sample terminal = sRecord.samples[sRecord.count - 1];
    terminal.deltaQf = static_cast<u16>(delta);
    sRecord.samples[sRecord.count++] = terminal;
    segment->sampleCount++;
    segment->endQf = static_cast<u32>(qf);
    sRecord.endQf = static_cast<u32>(qf);
    sLastSampleQf = qf;
    return true;
}

void finishRecording(s32 qf, bool completed) {
    const bool clockWasActive = sClockPhase == CLOCK_ACTIVE;
    if (sRecording && sRecord.count != 0) {
        bool bounded = clockWasActive;
        if (bounded && completed) bounded = finishTrackAt(qf);
        sRecord.valid = bounded && sRecord.count >= 2;
        sRecord.completed = completed && sRecord.valid;
        if (sRecord.completed) {
            sRecord.resultQf = static_cast<u32>(qf);
            sRecord.runFlags &= ~SUSAMUNE_GHOST_RUN_INCOMPLETE;
        } else {
            sRecord.resultQf = SUSAMUNE_GHOST_RESULT_QF_NONE;
            sRecord.runFlags |= SUSAMUNE_GHOST_RUN_INCOMPLETE;
        }
        bumpRecordToken();
    }
    sRecording = false;
    sClockLastQf = qf;
    sClockPhase = CLOCK_FINISHED;
}

void prepareClock(s32 qf) {
    sGhostVisible = false;
    sStageRoutePending = false;
    sClockPhase = CLOCK_PROVISIONAL;
    sClockObservations = 1;
    sClockLastQf = qf;
    sClockEpochStartQf = qf;
}

void beginAttempt(s32 qf, bool boundaryReset = false) {
    if (!gpMarDirector) return;

    captureLiveRoute();
    const u8 area = sLiveArea;
    const u8 episode = sLiveEpisode;
    const s32 parent = sLiveParentEpisode;
    const bool recordReady = sRecord.valid;
    const bool routeMatches = recordRouteMatches(
        area, episode, parent, sLiveRouteParentArea, sLiveRouteFlags);
    // Plaza is a neutral handoff between races. Outside it, reaching another
    // logical course retires the pinned target so its challenger can take the
    // playback slot and the old target buffer can record the next attempt.
    if (sPlaybackPinned &&
        !pinSurvivesRoute(area, episode, parent, sLiveRouteParentArea)) {
        sPlaybackPinned = false;
    }
    if (!sPlaybackPinned &&
        gSettings.getBool(SETTING_GHOST_LAST_SUCCESS) &&
        sPlayback.valid && !sPlayback.completed) {
        // Two buffers cannot recover an older success once a partial replaced
        // it. Do not misrepresent that stale partial as the selected policy.
        clearTrack(sPlayback);
        bumpPlaybackToken();
    }
    // Completed runs remain exportable across scenes. An interrupted attempt
    // is useful only when restarting that exact route; never let a plaza/hub
    // segment evict the last course ghost.
    const bool recordPromotable = recordReady &&
        recordPromotableForRoute(routeMatches, boundaryReset);
    const bool keepChallenger =
        recordPromotable && sPlaybackPinned && !sRecord.saved;

    if (recordPromotable && !sPlaybackPinned) {
        // Keep the newest completed track even when the next attempt changes
        // route. Route matching controls racing, not ownership/exportability.
        Sample *oldPlayback = sPlayback.samples;
        Segment *oldPlaybackSegments = sPlayback.segments;
        sPlayback = sRecord;
        sRecord.samples = oldPlayback;
        sRecord.segments = oldPlaybackSegments;
        bumpPlaybackToken();
        sPlaybackOriginRecordToken = sRecordToken;
    }
    rewindPlayback();

    if (!keepChallenger) {
        clearRecord();
    }
    sLastSampleQf = 0;
    // A pinned library target stays in the playback slot while this independent
    // record slot captures a challenger. Once complete, preserve that challenger
    // across restarts until storage acknowledges it or the pin is released.
    sRecording = !keepChallenger;
    prepareClock(qf);
    if (sRecording && appendSegment()) startEpochSamples(qf);
    if (keepChallenger && gMenu) {
        gMenu->toast("Ghost challenger ready: save or unpin");
    }

}

f32 samplePosition(s32 value) {
    return static_cast<f32>(value) / static_cast<f32>(kPositionScale);
}

int playbackSegmentAt(s32 qf) {
    if (!gpMarDirector || !sLiveRouteValid || !sPlayback.valid) return -1;
    // Later segments own an equal end/start boundary. This keeps a movie
    // return on its new entry pose instead of the departed director's pose.
    for (int i = static_cast<int>(sPlayback.segmentCount) - 1; i >= 0; i--) {
        const Segment &segment = sPlayback.segments[i];
        if (segmentRouteEquals(segment, sLiveArea, sLiveEpisode,
                               sLiveParentEpisode, sLiveRouteParentArea,
                               sLiveRouteFlags) &&
            qf >= static_cast<s32>(segment.startQf) &&
            qf <= static_cast<s32>(segment.endQf)) {
            return i;
        }
    }
    return -1;
}

void selectPlaybackSegment(u16 index) {
    if (index >= sPlayback.segmentCount || sPlaybackSegment == index) return;
    const Segment &segment = sPlayback.segments[index];
    sPlaybackSegment = index;
    sPlaybackCursor = segment.firstSample;
    sPlaybackCursorQf = static_cast<s32>(segment.startQf);
}

void updatePlayback(s32 qf) {
    sGhostVisible = false;
    sGhostHeldObjectId = 0;
    sGhostHeldNameKey = 0;
    sGhostYoshi = SUSAMUNE_GHOST_V4_YOSHI_NONE;
    const int segmentIndex = playbackSegmentAt(qf);
    if (segmentIndex < 0) return;
    const Segment &segment = sPlayback.segments[segmentIndex];
    if (segment.sampleCount == 0) return;
    selectPlaybackSegment(static_cast<u16>(segmentIndex));
    if (qf < sPlaybackCursorQf) {
        sPlaybackSegment = 0xffff;
        selectPlaybackSegment(static_cast<u16>(segmentIndex));
    }

    const u32 segmentEnd = segment.firstSample + segment.sampleCount;

    while (sPlaybackCursor + 1 < segmentEnd) {
        const s32 nextQf = sPlaybackCursorQf +
            sPlayback.samples[sPlaybackCursor + 1].deltaQf;
        if (nextQf > qf) break;
        sPlaybackCursor++;
        sPlaybackCursorQf = nextQf;
    }

    const Sample &a = sPlayback.samples[sPlaybackCursor];
    if (sPlaybackCursor + 1 >= segmentEnd) {
        sGhostPosition.set(samplePosition(sampleX(a)),
                           samplePosition(sampleY(a)),
                           samplePosition(sampleZ(a)));
        sGhostYaw = a.yaw;
        sGhostAnimationId = sampleAnimationId(a);
        sGhostAnimationPhase = sampleAnimationPhase(a, sPlayback.formatVersion);
        sampleAttachments(sPlayback, a, &sGhostYoshi,
                          &sGhostHeldObjectId, &sGhostHeldNameKey);
        sGhostVisible = true;
        return;
    }

    const Sample &b = sPlayback.samples[sPlaybackCursor + 1];
    const s32 nextQf = sPlaybackCursorQf + b.deltaQf;
    f32 t = 0.0f;
    if (nextQf > sPlaybackCursorQf) {
        t = static_cast<f32>(qf - sPlaybackCursorQf) /
            static_cast<f32>(nextQf - sPlaybackCursorQf);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }

    sGhostPosition.x = samplePosition(sampleX(a)) +
        (samplePosition(sampleX(b)) - samplePosition(sampleX(a))) * t;
    sGhostPosition.y = samplePosition(sampleY(a)) +
        (samplePosition(sampleY(b)) - samplePosition(sampleY(a))) * t;
    sGhostPosition.z = samplePosition(sampleZ(a)) +
        (samplePosition(sampleZ(b)) - samplePosition(sampleZ(a))) * t;
    const s32 yawDelta = static_cast<s16>(
        static_cast<u16>(b.yaw - a.yaw));
    sGhostYaw = static_cast<s16>(a.yaw + yawDelta *
        (qf - sPlaybackCursorQf) / (nextQf - sPlaybackCursorQf));
    sGhostAnimationId = sampleAnimationId(a);
    sGhostAnimationPhase = sampleAnimationId(a) == sampleAnimationId(b)
        ? interpolateAnimationPhase(
              sampleAnimationPhase(a, sPlayback.formatVersion),
              sampleAnimationPhase(b, sPlayback.formatVersion),
              qf - sPlaybackCursorQf, nextQf - sPlaybackCursorQf)
        : sampleAnimationPhase(a, sPlayback.formatVersion);
    sampleAttachments(sPlayback, a, &sGhostYoshi,
                      &sGhostHeldObjectId, &sGhostHeldNameKey);
    sGhostVisible = true;
}

void updateProvisionalPlayback(s32 qf) {
    // Recording still waits for a settled clock. Playback can seek the live
    // observation now; a later regression rewinds its cursor in rebaseClock().
    updatePlayback(qf);
}

bool sampleObserverTrack(const Track &track, u16 segmentIndex, s32 qf,
                         u32 *cursor, s32 *cursorQf, TVec3f *position,
                         s16 *yaw, u16 *animationId, u16 *animationPhase,
                         u8 *yoshi, u32 *heldObjectId, u16 *heldNameKey) {
    if (!cursor || !cursorQf || !position || !yaw || !animationId ||
        !animationPhase || !yoshi || !heldObjectId || !heldNameKey ||
        segmentIndex >= track.segmentCount) {
        return false;
    }
    const Segment &segment = track.segments[segmentIndex];
    if (segment.sampleCount == 0 || qf < static_cast<s32>(segment.startQf) ||
        qf > static_cast<s32>(segment.endQf)) {
        return false;
    }

    const u32 segmentEnd = segment.firstSample + segment.sampleCount;
    if (*cursor < segment.firstSample || *cursor >= segmentEnd ||
        qf < *cursorQf) {
        *cursor = segment.firstSample;
        *cursorQf = static_cast<s32>(segment.startQf);
    }
    while (*cursor + 1 < segmentEnd) {
        const s32 nextQf = *cursorQf + track.samples[*cursor + 1].deltaQf;
        if (nextQf > qf) break;
        (*cursor)++;
        *cursorQf = nextQf;
    }

    const Sample &a = track.samples[*cursor];
    if (*cursor + 1 >= segmentEnd) {
        position->set(samplePosition(sampleX(a)), samplePosition(sampleY(a)),
                      samplePosition(sampleZ(a)));
        *yaw = a.yaw;
        *animationId = sampleAnimationId(a);
        *animationPhase = sampleAnimationPhase(a, track.formatVersion);
        sampleAttachments(track, a, yoshi, heldObjectId, heldNameKey);
        return true;
    }

    const Sample &b = track.samples[*cursor + 1];
    const s32 nextQf = *cursorQf + b.deltaQf;
    const s32 span = nextQf - *cursorQf;
    if (span <= 0) return false;
    const s32 elapsed = qf - *cursorQf;
    const f32 t = static_cast<f32>(elapsed) / static_cast<f32>(span);
    position->x = samplePosition(sampleX(a)) +
        (samplePosition(sampleX(b)) - samplePosition(sampleX(a))) * t;
    position->y = samplePosition(sampleY(a)) +
        (samplePosition(sampleY(b)) - samplePosition(sampleY(a))) * t;
    position->z = samplePosition(sampleZ(a)) +
        (samplePosition(sampleZ(b)) - samplePosition(sampleZ(a))) * t;
    const s32 yawDelta = static_cast<s16>(
        static_cast<u16>(b.yaw - a.yaw));
    *yaw = static_cast<s16>(a.yaw + yawDelta * elapsed / span);
    *animationId = sampleAnimationId(a);
    *animationPhase = sampleAnimationId(a) == sampleAnimationId(b)
        ? interpolateAnimationPhase(sampleAnimationPhase(a, track.formatVersion),
                                    sampleAnimationPhase(b, track.formatVersion),
                                    elapsed, span)
        : sampleAnimationPhase(a, track.formatVersion);
    sampleAttachments(track, a, yoshi, heldObjectId, heldNameKey);
    return true;
}

s32 observerQf(bool *pastEnd = nullptr) {
    if (pastEnd) *pastEnd = false;
    if (sObserverPhase != OBSERVER_ACTIVE_ONE &&
        sObserverPhase != OBSERVER_ACTIVE_TWO) {
        return sObserverBaseQf;
    }

    s32 liveQf;
    bool stopped;
    if (!sObserverClockReady ||
        !gQFTTimer.currentQf(&liveQf, &stopped)) {
        return sObserverLastQf;
    }

    s64 absoluteQf = static_cast<s64>(liveQf) + sObserverQfOffset;
    if (pastEnd) {
        // A stopped shared clock cannot reach a later Watch 2 endpoint.
        *pastEnd = stopped || absoluteQf > sObserverEndQf;
    }
    if (absoluteQf < sObserverBaseQf) absoluteQf = sObserverBaseQf;
    if (absoluteQf > sObserverEndQf) absoluteQf = sObserverEndQf;
    sObserverLastQf = static_cast<s32>(absoluteQf);
    return sObserverLastQf;
}

void updateObserverVisual(s32 qf) {
    sGhostVisible = sampleObserverTrack(
        sPlayback, sObserverPrimarySegment, qf, &sPlaybackCursor,
        &sPlaybackCursorQf, &sGhostPosition, &sGhostYaw,
        &sGhostAnimationId, &sGhostAnimationPhase, &sGhostYoshi,
        &sGhostHeldObjectId, &sGhostHeldNameKey);
    sSecondaryGhostVisible = observerHasTwo() &&
        sampleObserverTrack(
            sObserverSecondary, sObserverSecondarySegment, qf,
            &sObserverSecondaryCursor, &sObserverSecondaryCursorQf,
            &sSecondaryGhostPosition, &sSecondaryGhostYaw,
            &sSecondaryGhostAnimationId, &sSecondaryGhostAnimationPhase,
            &sSecondaryGhostYoshi, &sSecondaryGhostHeldObjectId,
            &sSecondaryGhostHeldNameKey);
}

bool observerRoutesCompatible() {
    if (!sPlayback.valid || sPlayback.segmentCount == 0) return false;
    if (!observerHasTwo()) return true;
    if (!sObserverSecondary.valid ||
        sObserverSecondary.segmentCount != sPlayback.segmentCount)
        return false;
    for (u16 i = 0; i < sPlayback.segmentCount; ++i) {
        const Segment &a = sPlayback.segments[i];
        const Segment &b = sObserverSecondary.segments[i];
        if (!segmentRouteEquals(b, a.routeArea, a.routeEpisode,
                                a.routeVariant, a.routeParentArea,
                                a.routeFlags)) {
            return false;
        }
    }
    return true;
}

bool configureObserverSegment() {
    if (sObserverPrimarySegment >= sPlayback.segmentCount) return false;
    const Segment &primary = sPlayback.segments[sObserverPrimarySegment];
    u32 baseQf = primary.startQf;
    u32 endQf = primary.endQf;
    if (primary.sampleCount == 0) return false;

    sObserverSecondarySegment = 0xffff;
    if (observerHasTwo()) {
        if (sObserverPrimarySegment >=
            sObserverSecondary.segmentCount) return false;
        sObserverSecondarySegment = sObserverPrimarySegment;
        const Segment &secondary =
            sObserverSecondary.segments[sObserverSecondarySegment];
        if (secondary.sampleCount == 0) return false;
        if (secondary.startQf < baseQf) baseQf = secondary.startQf;
        if (secondary.endQf > endQf) endQf = secondary.endQf;
        sObserverSecondaryCursor = secondary.firstSample;
        sObserverSecondaryCursorQf = static_cast<s32>(secondary.startQf);
    }
    if (endQf < baseQf || endQf - baseQf >
            static_cast<u32>(kMaxDurationQf)) {
        return false;
    }

    sObserverBaseQf = static_cast<s32>(baseQf);
    sObserverEndQf = static_cast<s32>(endQf);
    sObserverLastQf = sObserverBaseQf;
    sObserverQfOffset = 0;
    sObserverStageAnchorQf = -1;
    sObserverClockReady = false;
    sPlaybackSegment = sObserverPrimarySegment;
    sPlaybackCursor = primary.firstSample;
    sPlaybackCursorQf = static_cast<s32>(primary.startQf);
    updateObserverVisual(sObserverBaseQf);
    return true;
}

LevelWarp::Dest observerSegmentDest() {
    const Segment &segment = sPlayback.segments[sObserverPrimarySegment];
    const u8 variant = segment.routeVariant >= 0
        ? static_cast<u8>(segment.routeVariant)
        : segment.routeEpisode;
    const LevelWarp::Dest dest = {
        segment.routeArea, segment.routeEpisode, variant
    };
    return dest;
}

LevelWarp::Dest observerSegmentSource() {
    const Segment &segment = sPlayback.segments[sObserverPrimarySegment];
    if (segment.routeParentArea == SUSAMUNE_GHOST_ROUTE_PARENT_NONE) {
        return observerSegmentDest();
    }
    const u8 episode = segment.routeVariant >= 0
        ? static_cast<u8>(segment.routeVariant)
        : segment.routeEpisode;
    const LevelWarp::Dest source = {
        segment.routeParentArea, episode, episode
    };
    return source;
}

void armObserverSegment() {
    const bool two = observerHasTwo();
    const LevelWarp::Dest dest = observerSegmentDest();
    const LevelWarp::Dest source = observerSegmentSource();
    // The guarded warp can wait on CARD or be canceled by settings. Restore
    // the current actor now; the loaded-stage callback drops stale ownership.
    releaseObserverMario(true);
    sObserverStageReady = false;
    sObserverPastEnd = false;
    sObserverRouteValidated = false;
    sObserverContinuousClock = false;
    sObserverExitArmed = false;
    sGhostVisible = false;
    sSecondaryGhostVisible = false;
    sObserverPhase = two ? OBSERVER_WARPING_TWO : OBSERVER_WARPING_ONE;
    LevelWarp::warpFrom(source, dest);
}

bool observerRouteLoaded() {
    captureLiveRoute();
    if (!sLiveRouteValid ||
        sObserverPrimarySegment >= sPlayback.segmentCount) {
        return false;
    }
    const Segment &segment = sPlayback.segments[sObserverPrimarySegment];
    if (!segmentRouteEquals(segment, sLiveArea, sLiveEpisode,
                            sLiveParentEpisode, sLiveRouteParentArea,
                            sLiveRouteFlags)) {
        return false;
    }
    if (!observerHasTwo()) return true;
    if (sObserverSecondarySegment >=
        sObserverSecondary.segmentCount) return false;
    return segmentRouteEquals(
        sObserverSecondary.segments[sObserverSecondarySegment],
        sLiveArea, sLiveEpisode, sLiveParentEpisode,
        sLiveRouteParentArea, sLiveRouteFlags);
}

void startObserverClock(s32 liveQf) {
    const bool two = observerHasTwo();
    // A direct observer warp resets QFT and needs a stage-local translation.
    // Retail loading zones carry the existing absolute timeline instead.
    const s32 stageAnchor = sObserverStageAnchorQf >= 0
        ? sObserverStageAnchorQf : liveQf;
    if (!sObserverContinuousClock) {
        sObserverQfOffset = sObserverPrimarySegment == 0
            ? 0 : sObserverBaseQf - stageAnchor;
    }
    sObserverContinuousClock = false;
    sObserverClockReady = true;
    sObserverPastEnd = false;
    // Intro-skip presses are viewer input, not exit requests. An active-state
    // release must be observed before B or Start can end this segment.
    sObserverExitArmed = false;
    sObserverPhase = two ? OBSERVER_ACTIVE_TWO : OBSERVER_ACTIVE_ONE;
    updateObserverVisual(observerQf(&sObserverPastEnd));
}

void anchorObserverMario() {
    if (!sObserverStageReady || !gpMarDirector ||
        gpMarDirector->mCurState != TMarDirector::STATE_NORMAL) return;
    bindObserverMario();
    if (!sObserverMarioOwned || !sObserverMario) return;
    finalizeObserverMarioBaseline();

    // Suppress the actor even during a route gap where neither runner has a
    // sample yet. Otherwise the hidden Mario remains player-controllable.
    sObserverMario->mPerformFlags |= kCueMove | kCueEntry;
    sObserverMario->mAttributes.mIsVisible = false;
    sObserverMario->mPrevAttributes.mIsVisible = false;
    sObserverMario->mSpeed.set(0.0f, 0.0f, 0.0f);
    sObserverMario->mPrevSpeed.set(0.0f, 0.0f, 0.0f);
    sObserverMario->mForwardSpeed = 0.0f;
    if (!sGhostVisible && !sSecondaryGhostVisible) return;

    TVec3f target;
    s16 yaw;
    if (sGhostVisible && sSecondaryGhostVisible) {
        target.set((sGhostPosition.x + sSecondaryGhostPosition.x) * 0.5f,
                   (sGhostPosition.y + sSecondaryGhostPosition.y) * 0.5f,
                   (sGhostPosition.z + sSecondaryGhostPosition.z) * 0.5f);
        yaw = sGhostYaw;
    } else if (sGhostVisible) {
        target = sGhostPosition;
        yaw = sGhostYaw;
    } else {
        target = sSecondaryGhostPosition;
        yaw = sSecondaryGhostYaw;
    }

    // Moving Mario externally must carry the camera's smoothed Y anchors too.
    if (gpCamera && gpCamera == sObserverCamera) {
        Vec cameraMove = {
            0.0f, target.y - sObserverMario->mTranslation.y, 0.0f
        };
        if (cameraMove.y != 0.0f) {
            gpCamera->addMoveCameraAndMario(cameraMove);
            sObserverCameraYOffset += cameraMove.y;
        }
    }

    // TViewObj::testPerform subtracts this mask from each incoming cue.
    sObserverMario->mTranslation = target;
    sObserverMario->mLastPosition = target;
    sObserverMario->mLastPos = target;
    sObserverMario->mLastGroundedPos = target;
    sObserverMario->mAngle.y = yaw;
    sObserverMario->mModelAngleY = yaw;
}

void endObserver(bool reload, const char *message) {
    if (sObserverPhase == OBSERVER_OFF) return;
    const bool wasRunning = observerRunning();
    const bool canReload = reload && wasRunning &&
        sObserverPrimarySegment < sPlayback.segmentCount &&
        !gSettings.getBool(SETTING_DISABLE_WARPS);
    LevelWarp::Dest dest = {0, 0, 0};
    LevelWarp::Dest source = {0, 0, 0};
    if (canReload) {
        dest = observerSegmentDest();
        source = observerSegmentSource();
    }
    if (canReload) releaseObserverMario(true);
    if (wasRunning) {
        ILing::invalidateForAssist();
        Records::invalidateAttempt();
    }
    stopAll();
    sObserverCleanupWarp = canReload;
    sAttemptSerial = gQFTTimer.attemptSerial();
    if (canReload) LevelWarp::warpFrom(source, dest);
    if (message && gMenu) gMenu->toast(message);
}

bool advanceObserverSegment() {
    if (sObserverPrimarySegment + 1 >= sPlayback.segmentCount) return false;
    ++sObserverPrimarySegment;
    if (!configureObserverSegment()) return false;
    armObserverSegment();
    return true;
}

u32 runningGameId() {
#if defined(SUSAMUNE_VERSION_JP)
    return SUSAMUNE_GHOST_GAME_ID_JP;
#elif defined(SUSAMUNE_VERSION_US)
    return SUSAMUNE_GHOST_GAME_ID_US;
#else
    return SUSAMUNE_GHOST_GAME_ID_PAL;
#endif
}

u8 runningRegion() {
#if defined(SUSAMUNE_VERSION_JP)
    return SUSAMUNE_GHOST_REGION_JP;
#elif defined(SUSAMUNE_VERSION_US)
    return SUSAMUNE_GHOST_REGION_US;
#else
    return SUSAMUNE_GHOST_REGION_PAL;
#endif
}

bool validText(const char *text, u32 capacity, u8 length, bool required) {
    if (!text || length > capacity || (required && length == 0)) return false;
    for (u32 i = 0; i < capacity; i++) {
        const u8 c = static_cast<u8>(text[i]);
        if (i < length) {
            if (c < SUSAMUNE_GHOST_TEXT_MIN || c > SUSAMUNE_GHOST_TEXT_MAX ||
                c == '/' || c == '\\') {
                return false;
            }
        } else if (c != 0) {
            return false;
        }
    }
    return true;
}

u8 copyText(char *out, u32 capacity, const char *text) {
    if (!out || capacity == 0) return 0;
    memset(out, 0, capacity);
    if (!text) return 0;
    u8 length = 0;
    while (length < capacity && text[length]) {
        const u8 c = static_cast<u8>(text[length]);
        if (c >= SUSAMUNE_GHOST_TEXT_MIN && c <= SUSAMUNE_GHOST_TEXT_MAX &&
            c != '/' && c != '\\') {
            out[length] = static_cast<char>(c);
            length++;
        } else {
            break;
        }
    }
    return length;
}

const char *courseName(u8 area) {
    switch (area) {
    case TGameSequence::AREA_BIANCO: return "Bianco Hills";
    case TGameSequence::AREA_RICCO: return "Ricco Harbor";
    case TGameSequence::AREA_MAMMA: return "Gelato Beach";
    case TGameSequence::AREA_PINNABEACH: return "Pinna Park";
    case TGameSequence::AREA_SIRENA: return "Sirena Beach";
    case TGameSequence::AREA_MONTE: return "Pianta Village";
    case TGameSequence::AREA_MARE: return "Noki Bay";
    default: return nullptr;
    }
}

const char *internalRouteSuffix(u8 area, u8 episode, s32 variant) {
    switch (area) {
    case TGameSequence::AREA_BIANCOBOSS:
        return episode == 0 ? "Windmill" : nullptr;
    case TGameSequence::AREA_RICOEX0:
        return episode == 0 ? "Blooper Race" : nullptr;
    case TGameSequence::AREA_RICCOBOSS:
        return episode == 0 ? "Blooper" : nullptr;
    case TGameSequence::AREA_MAMMAEX1:
        return episode == 0 ? "Sand Bird" : nullptr;
    case TGameSequence::AREA_MAMMAEX0:
    case TGameSequence::AREA_SIRENAEX0:
    case TGameSequence::AREA_SIRENAEX1:
    case TGameSequence::AREA_COROEX0:
    case TGameSequence::AREA_COROEX1:
    case TGameSequence::AREA_COROEX2:
    case TGameSequence::AREA_COROEX4:
    case TGameSequence::AREA_COROEX5:
    case TGameSequence::AREA_MONTEEX0:
    case TGameSequence::AREA_RICOEX1:
        return episode == 0 ? "Secret" : nullptr;
    case TGameSequence::AREA_PINNABOSS:
        if (episode == 1 && variant == 0) return "Mecha-Bowser";
        return episode == 0 && variant == 7 ? "Balloons" : nullptr;
    case TGameSequence::AREA_DELFINO:
        if ((episode == 0 && variant == 1) ||
            (episode == 1 && variant == 2) ||
            (episode == 2 && (variant == 3 || variant == 4)) ||
            (episode == 3 && variant == 6) ||
            (episode == 4 && variant == 7)) {
            return "Hotel";
        }
        return nullptr;
    case TGameSequence::AREA_CASINO:
        return ((episode == 0 && variant == 3) ||
                (episode == 1 && variant == 4)) ? "Casino" : nullptr;
    case TGameSequence::AREA_DELFINOBOSS:
        return episode == 0 ? "King Boo" : nullptr;
    case TGameSequence::AREA_MAREEX0:
        return episode == 0 ? "Bottle" : nullptr;
    case TGameSequence::AREA_MAREBOSS:
        return episode == 0 ? "Eel Only" : nullptr;
    case TGameSequence::AREA_MAREUNDERSEA:
        return episode == 0 ? "Red Fish" : nullptr;
    default: return nullptr;
    }
}

bool isPinnaParkRoute(u8 episode, s32 variant) {
    return (episode == 0 && variant == 0) ||
           (episode == 1 && variant == 2) ||
           (episode == 2 && variant == 4) ||
           (episode == 3 && variant == 5) ||
           (episode == 4 && variant == 6) ||
           (episode == 5 && variant == 7);
}

const char *standaloneRouteName(u8 area) {
    switch (area) {
    case TGameSequence::AREA_AIRPORT: return "Airstrip 1";
    case TGameSequence::AREA_DOLPIC: return "Delfino Plaza";
    case TGameSequence::AREA_DOLPICEX0: return "Airstrip Reds";
    case TGameSequence::AREA_DOLPICEX1: return "Delfino Slide";
    case TGameSequence::AREA_DOLPICEX2: return "Pachinko";
    case TGameSequence::AREA_DOLPICEX3: return "Grass Secret";
    case TGameSequence::AREA_DOLPICEX4: return "Lily Pad";
    case TGameSequence::AREA_BIANCOEX1: return "Delfino Cop Secret";
    case TGameSequence::AREA_COROEX6: return "Corona Mountain";
    case TGameSequence::AREA_CORONABOSS: return "Bowser";
    default: return nullptr;
    }
}

void formatTrackName(const Track &track, char *out, u32 size) {
    if (!out || size == 0) return;
    out[0] = '\0';
    const u32 duration = track.endQf - track.startQf;
    const u32 millis = (duration * 1001u) / 120u;
    char route[32];
    route[0] = '\0';

    if (track.routeParentArea != SUSAMUNE_GHOST_ROUTE_PARENT_NONE &&
        (track.routeFlags & SUSAMUNE_GHOST_ROUTE_INTERNAL_SCENE) &&
        track.routeParentArea == routeParentArea(track.area)) {
        const char *parent = courseName(track.routeParentArea);
        if (parent && track.parentEpisode >= 0 &&
            track.parentEpisode < 8) {
            const char *suffix = internalRouteSuffix(
                track.area, track.episode, track.parentEpisode);
            if (suffix) {
                snprintf(route, sizeof(route), "%s %ld %s", parent,
                         track.parentEpisode + 1, suffix);
            } else if (track.area == TGameSequence::AREA_PINNAPARCO &&
                       isPinnaParkRoute(track.episode,
                                        track.parentEpisode)) {
                snprintf(route, sizeof(route), "%s %ld", parent,
                         track.parentEpisode + 1);
            } else {
                snprintf(route, sizeof(route), "%s %ld Route", parent,
                         track.parentEpisode + 1);
            }
        }
    } else if (track.routeParentArea == SUSAMUNE_GHOST_ROUTE_PARENT_NONE &&
               track.routeFlags == 0) {
        const char *course = courseName(track.area);
        if (course && track.episode < 8) {
            snprintf(route, sizeof(route), "%s %u", course,
                     static_cast<unsigned>(track.episode) + 1u);
        } else {
            const char *standalone = standaloneRouteName(track.area);
            if (standalone) snprintf(route, sizeof(route), "%s", standalone);
        }
    }

    if (route[0]) {
        snprintf(out, size, "%s - %lu:%02lu.%03lu", route,
                 millis / 60000u, (millis / 1000u) % 60u,
                 millis % 1000u);
    } else {
        snprintf(out, size, "Area %u Episode %u - %lu:%02lu.%03lu",
                 static_cast<unsigned>(track.area),
                 static_cast<unsigned>(track.episode) + 1u,
                 millis / 60000u, (millis / 1000u) % 60u,
                 millis % 1000u);
    }
}

enum SaveSource {
    SAVE_SOURCE_NONE,
    SAVE_SOURCE_RECORD,
    SAVE_SOURCE_PLAYBACK,
};

struct SaveSelection {
    const Track *track;
    u32 token;
    u32 identityToken;
    SaveSource source;
};

SaveSelection latestSaveableTrack() {
    if (sObserverPhase != OBSERVER_OFF) {
        return {nullptr, 0, 0, SAVE_SOURCE_NONE};
    }
    // Once Records accepts a PB, Save owns that immutable completed run until
    // storage acknowledges it or the user explicitly discards its PB token.
    // An unrelated live recording must never mask it.
    if (sRecord.valid && sRecord.pb && !sRecord.saved &&
        sRecord.pbToken != 0) {
        return {&sRecord, sRecordToken, sRecord.pbToken,
                SAVE_SOURCE_RECORD};
    }
    if (sPlayback.valid && sPlayback.pb && !sPlayback.saved &&
        sPlayback.pbToken != 0) {
        return {&sPlayback, kPlaybackTokenBit | sPlaybackToken,
                sPlayback.pbToken, SAVE_SOURCE_PLAYBACK};
    }
    // The current attempt owns Save once its clock/route have settled. A
    // loaded race target must never mask a challenger.
    if (sRecord.valid &&
        (!sRecording ||
         (sClockPhase == CLOCK_ACTIVE && !sBoundaryPending))) {
        return {&sRecord, sRecordToken, sRecordIdentityToken,
                SAVE_SOURCE_RECORD};
    }
    // A pinned library target is playback-only while the race is active.
    if (sPlayback.valid && !sPlaybackPinned) {
        return {&sPlayback, kPlaybackTokenBit | sPlaybackToken,
                kPlaybackTokenBit | sPlaybackToken, SAVE_SOURCE_PLAYBACK};
    }
    return {nullptr, 0, 0, SAVE_SOURCE_NONE};
}

bool validRouteTuple(u8 area, u8 episode, u8 parentArea, u8 flags,
                     s32 variant) {
    return area <= SUSAMUNE_GHOST_ROUTE_AREA_MAX &&
           episode <= SUSAMUNE_GHOST_ROUTE_EPISODE_MAX &&
           (parentArea == SUSAMUNE_GHOST_ROUTE_PARENT_NONE ||
            parentArea <= SUSAMUNE_GHOST_ROUTE_AREA_MAX) &&
           !(flags & ~SUSAMUNE_GHOST_ROUTE_FLAGS_V1) &&
           (!!(flags & SUSAMUNE_GHOST_ROUTE_INTERNAL_SCENE) ==
            (parentArea != SUSAMUNE_GHOST_ROUTE_PARENT_NONE)) &&
           (!(flags & SUSAMUNE_GHOST_ROUTE_PARENT_START) ||
            parentArea != SUSAMUNE_GHOST_ROUTE_PARENT_NONE) &&
           variant >= SUSAMUNE_GHOST_ROUTE_VARIANT_NONE &&
           variant <= SUSAMUNE_GHOST_ROUTE_VARIANT_MAX;
}

bool validGameRegionPair(u32 gameId, u8 region) {
    switch (region) {
    case SUSAMUNE_GHOST_REGION_JP:
        return gameId == SUSAMUNE_GHOST_GAME_ID_JP;
    case SUSAMUNE_GHOST_REGION_US:
        return gameId == SUSAMUNE_GHOST_GAME_ID_US;
    case SUSAMUNE_GHOST_REGION_PAL:
        return gameId == SUSAMUNE_GHOST_GAME_ID_PAL;
    default:
        return false;
    }
}

s32 portableRouteParent(u8 area) {
    switch (area) {
#define PORTABLE_ROUTE_PARENT(routeArea, parentArea) \
    case routeArea: return parentArea;
        SUSAMUNE_GHOST_PORTABLE_ROUTE_LIST(PORTABLE_ROUTE_PARENT)
#undef PORTABLE_ROUTE_PARENT
    default:
        return -1;
    }
}

bool validPortableRouteTuple(u8 area, u8 episode, u8 parentArea, u8 flags,
                             s32 variant) {
    if (!validRouteTuple(area, episode, parentArea, flags, variant)) {
        return false;
    }
    const s32 requiredParent = portableRouteParent(area);
    if (requiredParent < 0) return false;
    if (requiredParent == SUSAMUNE_GHOST_ROUTE_PARENT_NONE) {
        return parentArea == SUSAMUNE_GHOST_ROUTE_PARENT_NONE && flags == 0;
    }
    return parentArea == static_cast<u8>(requiredParent) &&
           (flags == SUSAMUNE_GHOST_ROUTE_INTERNAL_SCENE ||
            flags == (SUSAMUNE_GHOST_ROUTE_INTERNAL_SCENE |
                      SUSAMUNE_GHOST_ROUTE_PARENT_START));
}

bool validSamplePosition(const Sample &sample) {
    return sampleX(sample) >= -SUSAMUNE_GHOST_MAX_POSITION_FIXED &&
           sampleX(sample) <= SUSAMUNE_GHOST_MAX_POSITION_FIXED &&
           sampleY(sample) >= -SUSAMUNE_GHOST_MAX_POSITION_FIXED &&
           sampleY(sample) <= SUSAMUNE_GHOST_MAX_POSITION_FIXED &&
           sampleZ(sample) >= -SUSAMUNE_GHOST_MAX_POSITION_FIXED &&
           sampleZ(sample) <= SUSAMUNE_GHOST_MAX_POSITION_FIXED;
}

bool validSampleAnimation(const Sample &sample, u16 version,
                          u8 attachmentCount, u16 attachmentFlags) {
    const u32 packed = readU24(sample.animation);
    if (sampleAnimationId(sample) > SUSAMUNE_GHOST_ANIMATION_ID_MAX) {
        return false;
    }
    if (version == SUSAMUNE_GHOST_FILE_VERSION_V3) {
        return !(packed & SUSAMUNE_GHOST_ANIMATION_RESERVED_MASK);
    }
    if (version != SUSAMUNE_GHOST_FILE_VERSION_V4) return false;
    const u8 yoshi = static_cast<u8>(
        (packed >> SUSAMUNE_GHOST_V4_YOSHI_SHIFT) &
        SUSAMUNE_GHOST_V4_YOSHI_MASK);
    const u8 held = static_cast<u8>(
        packed & SUSAMUNE_GHOST_V4_HELD_INDEX_MASK);
    return yoshi <= SUSAMUNE_GHOST_V4_YOSHI_UNKNOWN &&
           (held == 0 || (held <= attachmentCount) ||
            (held == SUSAMUNE_GHOST_V4_HELD_UNKNOWN &&
             (attachmentFlags &
              SUSAMUNE_GHOST_V4_ATTACHMENT_HELD_OVERFLOW)));
}

bool validCanonicalFile(const void *data, u32 size,
                        SusamuneGhostFileHeader *headerOut) {
    if (!data || !headerOut || size < SUSAMUNE_GHOST_FILE_HEADER_SIZE ||
        size > SUSAMUNE_GHOST_MAX_FILE_SIZE) {
        return false;
    }

    SusamuneGhostFileHeader header;
    memcpy(&header, data, sizeof(header));
    const bool v3 = header.version == SUSAMUNE_GHOST_FILE_VERSION_V3;
    const bool v4 = header.version == SUSAMUNE_GHOST_FILE_VERSION_V4;
    if (!v3 && !v4) return false;
    const u32 supportedFeatures = v4
        ? SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V4
        : SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V3;
    const bool foreign = header.gameId != runningGameId() ||
                         header.region != runningRegion();
    if (header.magic != SUSAMUNE_GHOST_FILE_MAGIC ||
        header.headerSize != SUSAMUNE_GHOST_FILE_HEADER_SIZE ||
        header.fileSize != size ||
        size > SUSAMUNE_GHOST_V4_MAX_FILE_SIZE ||
        header.checksumKind != SUSAMUNE_GHOST_CHECKSUM_CRC32 ||
        (header.requiredFeatures & ~supportedFeatures) ||
        (v4 && header.requiredFeatures !=
                   SUSAMUNE_GHOST_REQUIRED_EXTENDED_CODEC) ||
        !validGameRegionPair(header.gameId, header.region) ||
        header.discRevision != SUSAMUNE_GHOST_DISC_REVISION ||
        header.sourceProfile >= SUSAMUNE_GHOST_PROFILE_COUNT ||
        header.recordingMode != SUSAMUNE_GHOST_RECORDING_POSE_QF ||
        header.sampleCodec != (v4
            ? SUSAMUNE_GHOST_CODEC_POSE_ATTACHMENTS
            : SUSAMUNE_GHOST_CODEC_RAW) ||
        header.sampleStride != SUSAMUNE_GHOST_POSE_SAMPLE_SIZE ||
        header.sampleIntervalQf != SUSAMUNE_GHOST_TRANSFORM_INTERVAL_QF ||
        !validRouteTuple(header.routeArea, header.routeEpisode,
                         header.routeParentArea, header.routeFlags,
                         header.routeVariant) ||
        (foreign &&
         !validPortableRouteTuple(header.routeArea, header.routeEpisode,
                                  header.routeParentArea, header.routeFlags,
                                  header.routeVariant)) ||
        header.sampleCount < SUSAMUNE_GHOST_MIN_SAMPLE_COUNT ||
        header.sampleCount > SUSAMUNE_GHOST_MAX_SAMPLE_COUNT ||
        header.startQf > SUSAMUNE_GHOST_QF_MAX ||
        header.endQf > SUSAMUNE_GHOST_QF_MAX ||
        header.endQf < header.startQf ||
        header.durationQf != header.endQf - header.startQf ||
        header.durationQf == 0 ||
        header.durationQf > SUSAMUNE_GHOST_MAX_DURATION_QF ||
        (header.resultQf != SUSAMUNE_GHOST_RESULT_QF_NONE &&
         (header.resultQf < header.startQf ||
          header.resultQf > header.endQf)) ||
        (header.ghostIdHi == 0 && header.ghostIdLo == 0) ||
        !validText(header.author, sizeof(header.author), header.authorLength,
                   false) ||
        !validText(header.name, sizeof(header.name), header.nameLength, true) ||
        !validText(header.profileName, sizeof(header.profileName),
                   header.profileNameLength, false)) {
        return false;
    }
    const u8 *bytes = static_cast<const u8 *>(data);
    const u32 sampleDataSize =
        header.sampleCount * SUSAMUNE_GHOST_POSE_SAMPLE_SIZE;
    SusamuneGhostFileV4Extension extension;
    memcpy(&extension, header.reserved, sizeof(extension));
    if (extension.segmentCount == 0 ||
        extension.segmentCount > SUSAMUNE_GHOST_V4_MAX_SEGMENTS ||
        extension.segmentSize != SUSAMUNE_GHOST_V4_SEGMENT_SIZE ||
        extension.segmentTableOffset !=
            SUSAMUNE_GHOST_V4_SEGMENT_TABLE_OFFSET ||
        extension.segmentTableSize !=
            SUSAMUNE_GHOST_V4_SEGMENT_TABLE_SIZE ||
        extension.sampleDataOffset !=
            SUSAMUNE_GHOST_V4_SAMPLE_DATA_OFFSET ||
        extension.sampleDataSize != sampleDataSize ||
        header.payloadSize !=
            SUSAMUNE_GHOST_V4_SEGMENT_TABLE_SIZE + sampleDataSize ||
        header.fileSize != extension.sampleDataOffset + sampleDataSize) {
        return false;
    }
    if (v3) {
        const u8 *reserved = header.reserved + 24;
        for (u32 i = 0; i < 48; i++) {
            if (reserved[i] != 0) return false;
        }
    } else {
        if (extension.attachmentCount >
                SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_COUNT ||
            extension.attachmentSize !=
                SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_SIZE ||
            (extension.attachmentFlags &
             ~SUSAMUNE_GHOST_V4_ATTACHMENT_FLAGS) ||
            extension.attachmentReserved != 0) {
            return false;
        }
        for (u8 i = 0;
             i < SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_COUNT; i++) {
            const u8 *descriptor = reinterpret_cast<const u8 *>(
                &extension.attachments[i]);
            bool zero = true;
            for (u32 j = 0;
                 j < SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_SIZE; j++) {
                if (descriptor[j] != 0) zero = false;
            }
            if ((i < extension.attachmentCount) == zero) return false;
            if (i < extension.attachmentCount) {
                for (u8 prior = 0; prior < i; prior++) {
                    if (memcmp(&extension.attachments[prior],
                               &extension.attachments[i],
                               sizeof(extension.attachments[i])) == 0) {
                        return false;
                    }
                }
            }
        }
    }
    if (extension.segmentTableChecksum !=
        Checksum::crc32(bytes + extension.segmentTableOffset,
                        extension.segmentTableSize)) {
        return false;
    }

    if (header.headerChecksum !=
            Checksum::crc32(bytes, header.headerSize,
                            SUSAMUNE_GHOST_FILE_CHECKSUM_OFFSET, 8) ||
        header.fileChecksum !=
            Checksum::crc32(bytes, size,
                            SUSAMUNE_GHOST_FILE_CHECKSUM_OFFSET, 4) ||
        header.payloadChecksum !=
            Checksum::crc32(bytes + header.headerSize,
                            header.payloadSize)) {
        return false;
    }

    u32 coveredSamples = 0;
    u32 priorEndQf = 0;
    u32 finalStartQf = 0;
    for (u16 i = 0; i < SUSAMUNE_GHOST_V4_MAX_SEGMENTS; i++) {
            SusamuneGhostSegment segment;
            memcpy(&segment,
                   bytes + extension.segmentTableOffset +
                       i * sizeof(segment),
                   sizeof(segment));
            if (i >= extension.segmentCount) {
                const u8 *unused = reinterpret_cast<const u8 *>(&segment);
                for (u32 j = 0; j < sizeof(segment); j++) {
                    if (unused[j] != 0) return false;
                }
                continue;
            }
            if (segment.firstSample != coveredSamples ||
                segment.sampleCount == 0 ||
                segment.sampleCount > header.sampleCount - coveredSamples ||
                segment.startQf > segment.endQf ||
                (i != 0 && segment.startQf < priorEndQf) ||
                segment.reserved0 != 0 || segment.reserved1 != 0 ||
                !validRouteTuple(segment.routeArea, segment.routeEpisode,
                                 segment.routeParentArea,
                                 segment.routeFlags,
                                 segment.routeVariant) ||
                (foreign &&
                 !validPortableRouteTuple(segment.routeArea,
                                          segment.routeEpisode,
                                          segment.routeParentArea,
                                          segment.routeFlags,
                                          segment.routeVariant))) {
                return false;
            }
            if (i == 0 &&
                (segment.routeArea != header.routeArea ||
                 segment.routeEpisode != header.routeEpisode ||
                 segment.routeParentArea != header.routeParentArea ||
                 segment.routeFlags != header.routeFlags ||
                 segment.routeVariant != header.routeVariant ||
                 segment.startQf != header.startQf)) {
                return false;
            }

            u32 elapsed = 0;
            for (u32 j = 0; j < segment.sampleCount; j++) {
                Sample sample;
                memcpy(&sample,
                       bytes + extension.sampleDataOffset +
                           (segment.firstSample + j) * sizeof(Sample),
                       sizeof(sample));
                if (!validSamplePosition(sample) ||
                    !validSampleAnimation(sample, header.version,
                                          v4 ? extension.attachmentCount : 0,
                                          v4 ? extension.attachmentFlags : 0)) {
                    return false;
                }
                if (j == 0) {
                    if (sample.deltaQf != 0) return false;
                    continue;
                }
                const bool terminalShort = j + 1 == segment.sampleCount;
                const u32 segmentDuration =
                    segment.endQf - segment.startQf;
                if (sample.deltaQf == 0 ||
                    (sample.deltaQf < header.sampleIntervalQf &&
                     !terminalShort) ||
                    elapsed > segmentDuration ||
                    sample.deltaQf > segmentDuration - elapsed) {
                    return false;
                }
                elapsed += sample.deltaQf;
            }
            if (elapsed != segment.endQf - segment.startQf) return false;
            coveredSamples += segment.sampleCount;
            priorEndQf = segment.endQf;
            finalStartQf = segment.startQf;
    }
    if (coveredSamples != header.sampleCount ||
        priorEndQf != header.endQf ||
        (header.resultQf != SUSAMUNE_GHOST_RESULT_QF_NONE &&
         header.resultQf < finalStartQf)) {
        return false;
    }

    *headerOut = header;
    return true;
}

void installCanonicalTrack(Track &track, const void *data,
                           const SusamuneGhostFileHeader &header) {
    clearTrack(track);
    const u8 *bytes = static_cast<const u8 *>(data);
    SusamuneGhostFileV4Extension extension;
    memcpy(&extension, header.reserved, sizeof(extension));
    memcpy(track.samples, bytes + extension.sampleDataOffset,
           header.sampleCount * sizeof(Sample));
    memcpy(track.segments, bytes + extension.segmentTableOffset,
           extension.segmentCount * sizeof(Segment));
    track.segmentCount = extension.segmentCount;
    track.count = header.sampleCount;
    track.startQf = header.startQf;
    track.endQf = header.endQf;
    track.resultQf = header.resultQf;
    track.runFlags = header.runFlags;
    track.parentEpisode = header.routeVariant;
    track.area = header.routeArea;
    track.episode = header.routeEpisode;
    track.routeParentArea = header.routeParentArea;
    track.routeFlags = header.routeFlags;
    track.formatVersion = header.version;
    if (header.version == SUSAMUNE_GHOST_FILE_VERSION_V4) {
        track.attachmentFlags = extension.attachmentFlags;
        track.attachmentCount = extension.attachmentCount;
        memcpy(track.attachments, extension.attachments,
               sizeof(track.attachments));
    }
    track.valid = true;
    track.completed =
        header.resultQf != SUSAMUNE_GHOST_RESULT_QF_NONE &&
        !(header.runFlags & SUSAMUNE_GHOST_RUN_INCOMPLETE);
    track.saved = true;
}

bool project(const TVec3f &world, int *screenX, int *screenY, int *radius) {
    if (!gpCamera || !screenX || !screenY || !radius) return false;

    Vec cameraPos = {gpCamera->mTranslation.x, gpCamera->mTranslation.y,
                     gpCamera->mTranslation.z};
    Vec cameraUp = {gpCamera->mUpVector.x, gpCamera->mUpVector.y,
                    gpCamera->mUpVector.z};
    Vec cameraTarget = {gpCamera->mTargetPos.x, gpCamera->mTargetPos.y,
                        gpCamera->mTargetPos.z};
    Vec source = {world.x, world.y + 70.0f, world.z};
    Vec viewPosition;
    Mtx view;
    C_MTXLookAt(view, &cameraPos, &cameraUp, &cameraTarget);
    PSMTXMultVec(view, &source, &viewPosition);

    const f32 depth = -viewPosition.z;
    const f32 fovy = gpCamera->mProjectionFovy;
    const f32 aspect = gpCamera->mProjectionAspect;
    if (depth < gpCamera->mProjectionNear || depth > gpCamera->mProjectionFar ||
        fovy < 1.0f || fovy > 170.0f ||
        aspect < 0.5f || aspect > 2.0f) {
        return false;
    }

    const f32 cot = 1.0f / tanf(fovy * 0.00872664626f);
    const f32 ndcX = viewPosition.x * cot / (aspect * depth);
    const f32 ndcY = viewPosition.y * cot / depth;
    if (ndcX < -1.1f || ndcX > 1.1f || ndcY < -1.1f || ndcY > 1.1f) {
        return false;
    }

    *screenX = static_cast<int>((ndcX * 0.5f + 0.5f) * 640.0f);
    *screenY = static_cast<int>((0.5f - ndcY * 0.5f) * 480.0f);
    int r = static_cast<int>(45.0f * cot * 240.0f / depth);
    if (r < 6) r = 6;
    if (r > 22) r = 22;
    *radius = r;
    return true;
}

void drawMarker(Menu *menu, const TVec3f &position, bool secondary,
                u8 alpha) {
    int x;
    int y;
    int r;
    if (!project(position, &x, &y, &r)) return;

    static const s16 octagon[16] = {
         0, -10,  7, -7, 10,  0,  7,  7,
         0,  10, -7,  7,-10,  0, -7, -7,
    };
    static const s16 diamond[8] = {0, -10, 10, 0, 0, 10, -10, 0};
    const s16 *unit = secondary ? diamond : octagon;
    const int count = secondary ? 4 : 8;
    s16 xy[16];
    for (int i = 0; i < count; i++) {
        xy[i * 2] = static_cast<s16>(x + unit[i * 2] * r / 10);
        xy[i * 2 + 1] = static_cast<s16>(
            y + unit[i * 2 + 1] * r / 10);
    }

    const u8 fillAlpha = alpha == 255
        ? 255 : static_cast<u8>(alpha >> (secondary ? 3 : 2));
    const JUtility::TColor fill = secondary
        ? JUtility::TColor(30, 155, 205, fillAlpha)
        : JUtility::TColor(70, 210, 255, fillAlpha);
    const JUtility::TColor line = secondary
        ? JUtility::TColor(190, 250, 255, alpha)
        : JUtility::TColor(110, 235, 255, alpha);
    menu->fillPoly(xy, count, fill);
    menu->strokePoly(xy, count, line);
    if (secondary) {
        menu->drawText("2", x - 4, y - 7, 12, 12,
                       JUtility::TColor(230, 255, 255, alpha));
    }
}

}  // namespace

void init() {
    sRecord.samples = reinterpret_cast<Sample *>(SUSAMUNE_GHOST_RECORD_PPC_BASE);
    sPlayback.samples = reinterpret_cast<Sample *>(SUSAMUNE_GHOST_PLAY_PPC_BASE);
    sObserverSecondary.samples = sRecord.samples;
    sRecord.segments = reinterpret_cast<Segment *>(
        SUSAMUNE_GHOST_RECORD_PPC_BASE + kSegmentTableOffset);
    sPlayback.segments = reinterpret_cast<Segment *>(
        SUSAMUNE_GHOST_PLAY_PPC_BASE + kSegmentTableOffset);
    sObserverSecondary.segments = sRecord.segments;
    sRecordToken = 0;
    sRecordIdentityToken = 0;
    sPlaybackToken = 0;
    sPlaybackOriginRecordToken = 0;
    sPBTokenSerial = 0;
    clearRecord();
    clearTrack(sPlayback);
    clearTrack(sObserverSecondary);
    bumpPlaybackToken();
    sAttemptSerial = gQFTTimer.attemptSerial();
    sLastSampleQf = 0;
    sPlaybackCursor = 0;
    sPlaybackCursorQf = 0;
    sPlaybackSegment = 0xffff;
    sRecording = false;
    sGhostVisible = false;
    sStageRoutePending = false;
    sPendingHadLiveRoute = false;
    sPendingContinueRecording = false;
    sBoundaryPending = false;
    sLiveRouteValid = false;
    sPlaybackPinned = false;
    sClockPhase = CLOCK_UNAVAILABLE;
    sClockObservations = 0;
    sClockLastQf = 0;
    sClockEpochStartQf = 0;
    sPendingPreviousClockQf = 0;
    sGhostYaw = 0;
    sGhostAnimationId = 0;
    sGhostAnimationPhase = 0;
    sGhostHeldObjectId = 0;
    sGhostHeldNameKey = 0;
    sGhostYoshi = SUSAMUNE_GHOST_V4_YOSHI_NONE;
    sGhostIdSerial = 0;
    resetObserverRuntime();
}

void onStageSetup(TMarDirector *director) {
    sObserverCleanupWarp = false;
    if (sObserverPhase == OBSERVER_WARPING_ONE ||
        sObserverPhase == OBSERVER_WARPING_TWO) {
        // The old actor lived in the stage heap that setupObjects just freed.
        releaseObserverMario(false);
        sObserverStageReady = director != nullptr;
        sObserverStageAnchorQf = -1;
        sObserverClockReady = false;
        sAttemptSerial = gQFTTimer.attemptSerial();
        sRecording = false;
        sStageRoutePending = false;
        sLiveRouteValid = false;
        sClockPhase = CLOCK_UNAVAILABLE;
        sGhostVisible = false;
        sSecondaryGhostVisible = false;
        if (director) {
            updateObserverVisual(sObserverBaseQf);
            bindObserverMario();
        }
        return;
    }
    if (sObserverPhase == OBSERVER_ACTIVE_ONE ||
        sObserverPhase == OBSERVER_ACTIVE_TWO) {
        const bool two = observerHasTwo();
        releaseObserverMario(false);
        const u16 next = static_cast<u16>(sObserverPrimarySegment + 1);
        const Segment *expectedSegment = director &&
                next < sPlayback.segmentCount
            ? &sPlayback.segments[next] : nullptr;
        // The parent-episode flag can settle after setupObjects(). Match only
        // stable scene identity here; WARPING validates the exact tuple at QFT.
        const bool expected = expectedSegment &&
            director->mAreaID == expectedSegment->routeArea &&
            director->mEpisodeID == expectedSegment->routeEpisode &&
            routeParentArea(director->mAreaID) ==
                expectedSegment->routeParentArea;
        if (expected) {
            const s32 continuedOffset = sObserverQfOffset;
            sObserverPrimarySegment = next;
            if (configureObserverSegment()) {
                sObserverQfOffset = continuedOffset;
                sObserverContinuousClock = true;
                sObserverPhase = two ? OBSERVER_WARPING_TWO
                                     : OBSERVER_WARPING_ONE;
                sObserverStageReady = true;
                sObserverRouteValidated = false;
                sAttemptSerial = gQFTTimer.attemptSerial();
                sRecording = false;
                sStageRoutePending = false;
                sLiveRouteValid = false;
                sClockPhase = CLOCK_UNAVAILABLE;
                sObserverExitArmed = false;
                bindObserverMario();
                return;
            }
        }
        stopAll();
        sAttemptSerial = gQFTTimer.attemptSerial();
        if (gMenu) gMenu->toast("Observer stopped: unexpected stage change");
    }
    if (!director) {
        clearRecord();
        if (!sPlaybackPinned) {
            clearTrack(sPlayback);
            bumpPlaybackToken();
        }
        sRecording = false;
        sGhostVisible = false;
        sStageRoutePending = false;
        sPendingHadLiveRoute = false;
        sPendingContinueRecording = false;
        sLiveRouteValid = false;
        sClockPhase = CLOCK_UNAVAILABLE;
        return;
    }
    const bool continueUnsettledBoundary =
        sBoundaryPending && sRecording && sRecord.valid;
    if (sBoundaryPending) {
        rollbackBoundarySegment();
        sBoundaryPending = false;
        sRecording = continueUnsettledBoundary;
    }
    sPendingHadLiveRoute = sLiveRouteValid;
    sPendingPreviousClockQf = sClockLastQf;
    // Preserve the settled prefix while the next director reveals whether
    // this is a restart or another segment on the same absolute QFT timeline.
    sPendingContinueRecording = continueUnsettledBoundary ||
        (sRecording && sRecord.valid && sClockPhase == CLOCK_ACTIVE);
    sRecording = false;
    // Route flags can still be rewritten while setupObjects() is finishing.
    // Defer compatibility until QFT makes the loaded stage live: a reset then
    // gets first chance to promote the completed recording.
    sStageRoutePending = true;
    sLiveRouteValid = false;
    sGhostVisible = false;
    sClockPhase = CLOCK_WAIT_STAGE;
}

void beforeDirect() {
    if (!observerRunning() || !sObserverStageReady) return;
    if (sObserverMarioBaselineFinalized && gpMarDirector &&
        gpMarDirector->mCurState != TMarDirector::STATE_NORMAL) {
        // Restore the actor while its stage heap is still alive. A replacement
        // Mario can reuse the same address without reinitialising every flag.
        releaseObserverMario(true);
        return;
    }
    if (sObserverPhase == OBSERVER_ACTIVE_ONE ||
        sObserverPhase == OBSERVER_ACTIVE_TWO) {
        const s32 qf = observerQf(&sObserverPastEnd);
        updateObserverVisual(qf);
    }
    anchorObserverMario();
}

void afterDirect(s32 appState) {
    if (!observerRunning() ||
        !sObserverMarioBaselineFinalized || !sObserverMarioOwned) {
        return;
    }
    if (appState != 0 || !gpMarDirector ||
        gpMarDirector->mCurState != TMarDirector::STATE_NORMAL) {
        // direct() can begin teardown on the same frame as the loading-zone
        // hit. Restore before the next director reuses this stage heap.
        releaseObserverMario(true);
    }
}

void update() {
    if (sObserverPhase == OBSERVER_OFF && sObserverCleanupWarp &&
        gSettings.getBool(SETTING_DISABLE_WARPS)) {
        sObserverCleanupWarp = false;
    }
    // A freshly allocated director can reuse the previous director's address
    // before setupObjects() replaces the stage-owned Mario and camera globals.
    if (gpApplication.mContext != TApplication::CONTEXT_DIRECT_STAGE ||
        !gpMarDirector || gpMarDirector->_260 == 0) {
        sGhostVisible = false;
        sSecondaryGhostVisible = false;
        return;
    }

    if (sObserverPhase == OBSERVER_PREPARING_ONE ||
        sObserverPhase == OBSERVER_PREPARING_TWO) {
        if (!gMenu || !gMenu->shown()) {
            stopObserver();
            if (gMenu) gMenu->toast("Ghost watch canceled");
            return;
        }
        sGhostVisible = false;
        sSecondaryGhostVisible = false;
        return;
    }
    if (sObserverPhase == OBSERVER_WARPING_ONE ||
        sObserverPhase == OBSERVER_WARPING_TWO) {
        sObserverExitArmed = false;
        gBinds.suppressUntilRelease();
        if (gSettings.getBool(SETTING_DISABLE_WARPS) ||
            !gSettings.getBool(SETTING_GHOST_DISPLAY)) {
            endObserver(false, "Ghost watch ended");
            return;
        }
        if (!sObserverStageReady) return;
        if (actionsFastForwardActive()) {
            endObserver(true, "Observer stopped: fast-forward");
            return;
        }
        // QFT becomes readable during GAME_STARTING, before Mario materialises.
        // The file's leading hold defines its first pose through that interval.
        if (!sObserverClockReady) updateObserverVisual(sObserverBaseQf);
        s32 liveQf;
        if (!gQFTTimer.currentQf(&liveQf)) {
            return;
        }
        if (sObserverStageAnchorQf < 0) sObserverStageAnchorQf = liveQf;
        if (!sObserverRouteValidated) {
            if (!observerRouteLoaded()) {
                endObserver(false, "Observer stopped: route load failed");
                return;
            }
            ILing::invalidateForAssist();
            Records::invalidateAttempt();
            sObserverRouteValidated = true;
            return;
        }
        startObserverClock(liveQf);
        return;
    }
    if (sObserverPhase == OBSERVER_ACTIVE_ONE ||
        sObserverPhase == OBSERVER_ACTIVE_TWO) {
        gBinds.suppressUntilRelease();
        if (gSettings.getBool(SETTING_DISABLE_WARPS) ||
            !gSettings.getBool(SETTING_GHOST_DISPLAY)) {
            endObserver(false, "Ghost watch ended");
            return;
        }
        const u16 exitHeld = static_cast<u16>(
            JUTGamePad::mPadStatus[0].mButton &
            (JUTGamePad::B | JUTGamePad::START));
        if (!sObserverExitArmed) {
            sObserverExitArmed = exitHeld == 0;
        } else if (exitHeld != 0) {
            endObserver(true, "Ghost watch ended");
            gBinds.suppressUntilRelease();
            return;
        }
        if (actionsFastForwardActive()) {
            endObserver(true, "Observer stopped: fast-forward");
            return;
        }
        if (sObserverPastEnd) {
            // A retail loading zone may already own the departure. Let its
            // setup select the next segment instead of replacing that warp.
            const bool hasNext =
                sObserverPrimarySegment + 1 < sPlayback.segmentCount;
            if (hasNext &&
                gpMarDirector->mCurState != TMarDirector::STATE_NORMAL) return;
            if (!advanceObserverSegment()) {
                endObserver(true, "Ghost observer finished");
            }
            return;
        }
        return;
    }

    s32 qf;
    bool stopped;
    if (!gQFTTimer.currentQf(&qf, &stopped) || !gpMarDirector) {
        sGhostVisible = false;
        return;
    }

    const u32 serial = gQFTTimer.attemptSerial();
    if (serial != sAttemptSerial) {
        sAttemptSerial = serial;
        if (sBoundaryPending) rollbackBoundarySegment();
        sBoundaryPending = false;
        sRecording = false;
        sPendingContinueRecording = false;
        beginAttempt(qf, true);
    } else if (sStageRoutePending) {
        captureLiveRoute();
        sStageRoutePending = false;
        const bool hadLiveRoute = sPendingHadLiveRoute;
        sPendingHadLiveRoute = false;
        if (!liveRouteStorable()) {
            sPendingContinueRecording = false;
            sClockPhase = CLOCK_UNAVAILABLE;
            rewindPlayback();
            sGhostVisible = false;
            if (gMenu) gMenu->toast("Ghost: route ended");
            return;
        }
        if (!hadLiveRoute) {
            sPendingContinueRecording = false;
            beginAttempt(qf);
            return;
        }
        // Any no-serial director replacement can be a restart or a route
        // continuation. Keep a tentative segment until QFT proves which.
        sBoundaryPending = true;
        sBoundaryPriorQf = sPendingPreviousClockQf;
        sBoundaryBaseSegmentCount = sRecord.segmentCount;
        if (sPendingContinueRecording && sRecord.segmentCount != 0 &&
            sBoundaryPriorQf < static_cast<s32>(sRecord.endQf)) {
            sBoundaryPriorQf = static_cast<s32>(sRecord.endQf);
        }
        prepareClock(qf);
        rewindPlayback();
        if (sPendingContinueRecording) {
            sRecording = true;
            if (!appendSegment() || !startEpochSamples(qf)) {
                // Capacity stops preserve the last valid prefix. Hard sample
                // failures have already invalidated it in appendSample().
                rollbackBoundarySegment();
                sBoundaryPending = false;
                sClockPhase = CLOCK_FINISHED;
            }
        }
        sPendingContinueRecording = false;
        return;
    }

    if (!gpMarioOriginal) {
        if (sRecording) {
            failRecording(RECORD_FAILURE_MARIO,
                          "Ghost: Mario disappeared");
        }
        sGhostVisible = false;
        return;
    }

    if (actionsFastForwardActive()) {
        if (sRecording) {
            sRecording = false;
            sRecord.valid = false;
            sRecord.runFlags |= SUSAMUNE_GHOST_RUN_ASSISTED |
                                SUSAMUNE_GHOST_RUN_FAST_FORWARD_USED;
            sRecord.failure = RECORD_FAILURE_ASSIST;
            bumpRecordToken();
            if (gMenu) gMenu->toast("Ghost recording disabled: fast-forward");
        }
        // A saved race target remains intact, but is hidden while time is
        // advancing faster than the visual reference.
        sGhostVisible = false;
        return;
    }

    if (sClockPhase == CLOCK_UNAVAILABLE || sClockPhase == CLOCK_WAIT_STAGE) {
        sGhostVisible = false;
        return;
    }

    if (sBoundaryPending) {
        if (qf < sClockLastQf || qf < sBoundaryPriorQf) {
            rollbackBoundarySegment();
            sBoundaryPending = false;
            sRecording = false;
            beginAttempt(qf, true);
            return;
        }
        sClockLastQf = qf;
        if (sClockObservations != 0xffff) sClockObservations++;
        if (sRecording && !appendSample(qf)) {
            rollbackBoundarySegment();
            sBoundaryPending = false;
            sClockPhase = CLOCK_FINISHED;
            updatePlayback(qf);
            return;
        }

        bool ordered = true;
        if (sRecording && sBoundaryBaseSegmentCount != 0) {
            const Segment *segment = lastSegment(sRecord);
            const Segment &prior =
                sRecord.segments[sBoundaryBaseSegmentCount - 1];
            ordered = segment && segment->sampleCount != 0 &&
                      segment->startQf >= prior.endQf;
        }
        if (stopped && sRecording && !ordered) {
            rollbackBoundarySegment();
            sBoundaryPending = false;
            sRecording = false;
            sClockPhase = CLOCK_FINISHED;
            updatePlayback(qf);
        } else if (stopped) {
            // A stopped QFT is an authoritative attempt end. Do not lose a
            // short final segment merely because its director never settled.
            sBoundaryPending = false;
            sClockPhase = CLOCK_ACTIVE;
            finishRecording(qf, true);
            updatePlayback(qf);
        } else if (ordered &&
                   gpMarDirector->mCurState == TMarDirector::STATE_NORMAL &&
            sClockObservations >= kClockSettleObservations &&
            qf - sClockEpochStartQf >= 8) {
            sBoundaryPending = false;
            sClockPhase = CLOCK_ACTIVE;
            if (sRecording && sRecord.count >= 2) sRecord.valid = true;
            updatePlayback(qf);
        } else {
            updateProvisionalPlayback(qf);
        }
        return;
    }

    if (stopped &&
        (sClockPhase == CLOCK_PROVISIONAL || sClockPhase == CLOCK_ACTIVE)) {
        finishRecording(qf, true);
        updatePlayback(qf);
        return;
    }

    if (qf < sClockLastQf) {
        if (sRecording && sRecord.segmentCount > 1 &&
            sBoundaryBaseSegmentCount > 0 &&
            sBoundaryBaseSegmentCount < sRecord.segmentCount) {
            // A reset can surface after the boundary settle window. Promote
            // only the proven prefix; never rebase it onto the lower clock.
            rollbackBoundarySegment();
            sBoundaryPending = false;
            sRecording = false;
            beginAttempt(qf, true);
            return;
        }
        rebaseClock(qf);
    } else {
        sClockLastQf = qf;
        if (sClockObservations != 0xffff) sClockObservations++;
        if (sRecording) appendSample(qf);
    }

    if (sClockPhase == CLOCK_PROVISIONAL) {
        const bool enoughSamples = !sRecording || sRecord.count >= 2;
        if (gpMarDirector->mCurState == TMarDirector::STATE_NORMAL &&
            sClockObservations >= kClockSettleObservations &&
            qf - sClockEpochStartQf >= 8 &&
            enoughSamples) {
            sClockPhase = CLOCK_ACTIVE;
            if (sRecording) sRecord.valid = true;
        } else {
            // Playback follows the observed QFT while recording keeps its
            // conservative settle window.
            updateProvisionalPlayback(qf);
            return;
        }
    }

    updatePlayback(qf);
}

void draw(Menu *menu) {
    if (!menu || menu->shown() ||
        !gSettings.getBool(SETTING_GHOST_DISPLAY)) {
        return;
    }

    static const u8 opacity[] = {64, 128, 192, 255};
    u8 choice = gSettings.get(SETTING_GHOST_OPACITY);
    if (choice >= sizeof(opacity)) choice = 1;
    const u8 alpha = opacity[choice];
    if (sGhostVisible && !GhostModel::submitted(false)) {
        drawMarker(menu, sGhostPosition, false, alpha);
    }
    if (sSecondaryGhostVisible && !GhostModel::submitted(true)) {
        drawMarker(menu, sSecondaryGhostPosition, true, alpha);
    }
}

bool exportLatest(void *out, u32 capacity, u8 sourceProfile,
                  const char *profileName, u32 *outSize,
                  u32 *outRecordToken) {
    if (outSize) *outSize = 0;
    if (outRecordToken) *outRecordToken = 0;
    const SaveSelection selection = latestSaveableTrack();
    const Track *track = selection.track;
    if (!out || !outSize || !track || sourceProfile >=
            SUSAMUNE_GHOST_PROFILE_COUNT ||
        track->count < SUSAMUNE_GHOST_MIN_SAMPLE_COUNT ||
        track->count > SUSAMUNE_GHOST_MAX_SAMPLE_COUNT ||
        track->area > SUSAMUNE_GHOST_ROUTE_AREA_MAX ||
        track->episode > SUSAMUNE_GHOST_ROUTE_EPISODE_MAX ||
        track->parentEpisode < SUSAMUNE_GHOST_ROUTE_VARIANT_NONE ||
        track->parentEpisode > SUSAMUNE_GHOST_ROUTE_VARIANT_MAX ||
        track->segmentCount == 0 || track->segmentCount > kMaxSegments ||
        track->endQf < track->startQf ||
        track->formatVersion != SUSAMUNE_GHOST_FILE_VERSION_V4 ||
        track->attachmentCount >
            SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_COUNT ||
        (track->attachmentFlags &
         ~SUSAMUNE_GHOST_V4_ATTACHMENT_FLAGS)) {
        return false;
    }

    const u32 duration = track->endQf - track->startQf;
    const u32 sampleDataSize = track->count * sizeof(Sample);
    const u32 sampleDataOffset = SUSAMUNE_GHOST_V4_SAMPLE_DATA_OFFSET;
    const u32 payloadSize = sampleDataSize +
        SUSAMUNE_GHOST_V4_SEGMENT_TABLE_SIZE;
    const u32 fileSize = sampleDataOffset + sampleDataSize;
    if (duration > SUSAMUNE_GHOST_MAX_DURATION_QF ||
        sampleDataSize > SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE ||
        payloadSize > SUSAMUNE_GHOST_V4_MAX_PAYLOAD_SIZE ||
        fileSize > capacity) {
        return false;
    }

    u8 *bytes = static_cast<u8 *>(out);
    memset(bytes, 0, fileSize);
    memcpy(bytes + sampleDataOffset, track->samples, sampleDataSize);
    memcpy(bytes + SUSAMUNE_GHOST_V4_SEGMENT_TABLE_OFFSET,
           track->segments, track->segmentCount * sizeof(Segment));

    SusamuneGhostFileHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = SUSAMUNE_GHOST_FILE_MAGIC;
    header.version = SUSAMUNE_GHOST_FILE_VERSION_V4;
    header.headerSize = SUSAMUNE_GHOST_FILE_HEADER_SIZE;
    header.fileSize = fileSize;
    header.requiredFeatures = SUSAMUNE_GHOST_REQUIRED_EXTENDED_CODEC;
    header.runFlags = track->runFlags;
    header.gameId = runningGameId();
    header.discRevision = SUSAMUNE_GHOST_DISC_REVISION;
    header.region = runningRegion();
    header.sourceProfile = sourceProfile;
    header.recordingMode = SUSAMUNE_GHOST_RECORDING_POSE_QF;
    header.sampleCodec = SUSAMUNE_GHOST_CODEC_POSE_ATTACHMENTS;
    header.sampleStride = SUSAMUNE_GHOST_POSE_SAMPLE_SIZE;
    header.sampleIntervalQf = SUSAMUNE_GHOST_TRANSFORM_INTERVAL_QF;
    header.routeArea = track->area;
    header.routeEpisode = track->episode;
    header.routeParentArea = track->routeParentArea;
    header.routeFlags = track->routeFlags;
    header.routeVariant = track->parentEpisode;
    header.resultQf = track->completed
        ? track->resultQf
        : SUSAMUNE_GHOST_RESULT_QF_NONE;
    header.startQf = track->startQf;
    header.endQf = track->endQf;
    header.durationQf = duration;
    header.sampleCount = track->count;
    header.payloadSize = payloadSize;
    SusamuneGhostFileV4Extension extension;
    memset(&extension, 0, sizeof(extension));
    extension.segmentCount = track->segmentCount;
    extension.segmentSize = SUSAMUNE_GHOST_V4_SEGMENT_SIZE;
    extension.segmentTableOffset =
        SUSAMUNE_GHOST_V4_SEGMENT_TABLE_OFFSET;
    extension.segmentTableSize = SUSAMUNE_GHOST_V4_SEGMENT_TABLE_SIZE;
    extension.sampleDataOffset = SUSAMUNE_GHOST_V4_SAMPLE_DATA_OFFSET;
    extension.sampleDataSize = sampleDataSize;
    extension.segmentTableChecksum = Checksum::crc32(
        bytes + extension.segmentTableOffset,
        extension.segmentTableSize);
    extension.attachmentCount = track->attachmentCount;
    extension.attachmentSize =
        SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_SIZE;
    extension.attachmentFlags = track->attachmentFlags;
    memcpy(extension.attachments, track->attachments,
           sizeof(extension.attachments));
    memcpy(header.reserved, &extension, sizeof(extension));

    const OSTime ticks = OSGetTime();
    sGhostIdSerial++;
    header.ghostIdHi = static_cast<u32>(ticks >> 32) ^ runningGameId();
    header.ghostIdLo = static_cast<u32>(ticks) ^
        (sGhostIdSerial * 0x9E3779B9u);
    if (header.ghostIdHi == 0 && header.ghostIdLo == 0) header.ghostIdLo = 1;

    char name[SUSAMUNE_GHOST_NAME_SIZE];
    formatTrackName(*track, name, sizeof(name));
    header.authorLength = 0;
    header.nameLength = copyText(header.name, sizeof(header.name), name);
    header.profileNameLength = copyText(
        header.profileName, sizeof(header.profileName), profileName);
    header.checksumKind = SUSAMUNE_GHOST_CHECKSUM_CRC32;
    header.payloadChecksum = Checksum::crc32(
        bytes + SUSAMUNE_GHOST_FILE_HEADER_SIZE, payloadSize);

    memcpy(bytes, &header, sizeof(header));
    header.headerChecksum = Checksum::crc32(
        bytes, sizeof(header), SUSAMUNE_GHOST_FILE_CHECKSUM_OFFSET, 8);
    memcpy(bytes, &header, sizeof(header));
    header.fileChecksum = Checksum::crc32(
        bytes, fileSize, SUSAMUNE_GHOST_FILE_CHECKSUM_OFFSET, 4);
    memcpy(bytes, &header, sizeof(header));

    SusamuneGhostFileHeader checked;
    if (!validCanonicalFile(bytes, fileSize, &checked)) {
        memset(bytes, 0, fileSize);
        return false;
    }
    *outSize = fileSize;
    if (outRecordToken) *outRecordToken = selection.token;
    return true;
}

bool importPlayback(const void *data, u32 size) {
    SusamuneGhostFileHeader header;
    if (!validCanonicalFile(data, size, &header)) return false;
    if (sPlayback.valid && sPlayback.pb && !sPlayback.saved &&
        sPlayback.pbToken != 0) {
        return false;
    }

    if (sObserverPhase != OBSERVER_OFF) endObserver(false, nullptr);
    clearTrack(sPlayback);
    bumpPlaybackToken();
    installCanonicalTrack(sPlayback, data, header);
    sPlaybackPinned = true;
    rewindPlayback();
    sGhostVisible = false;
    // Playback has its own buffer. Keep the current recorder and clock alive
    // so selecting a race target cannot erase the challenger being captured.
    return true;
}

bool beginObserverPreparation(bool twoGhosts) {
    TMarioGamePad *pad = gpApplication.mGamePads[0];
    if (!gpMarDirector || !pad || pad->mState.mDisable ||
        gpMarDirector->mCurState != TMarDirector::STATE_NORMAL ||
        gSettings.getBool(SETTING_DISABLE_WARPS)) {
        return false;
    }
    captureLiveRoute();
    if (!liveRouteStorable() || hasUnsavedPB()) {
        return false;
    }

    stopAll();
    sAttemptSerial = gQFTTimer.attemptSerial();
    captureLiveRoute();
    sObserverPhase = twoGhosts ? OBSERVER_PREPARING_TWO
                               : OBSERVER_PREPARING_ONE;
    return true;
}

bool importObserverTrack(const void *data, u32 size, bool secondary) {
    SusamuneGhostFileHeader header;
    if (!validCanonicalFile(data, size, &header) ||
        (sObserverPhase != OBSERVER_PREPARING_ONE &&
         sObserverPhase != OBSERVER_PREPARING_TWO) ||
        (secondary &&
         (sObserverPhase != OBSERVER_PREPARING_TWO ||
          !sObserverPrimaryReady))) {
        return false;
    }

    if (secondary) {
        if (sRecord.samples == sPlayback.samples ||
            sRecord.segments == sPlayback.segments) {
            return false;
        }
        // Recording promotion swaps the two runtime slot pointers. Borrow the
        // record slot selected by that swap, not its boot-time address.
        sObserverSecondary.samples = sRecord.samples;
        sObserverSecondary.segments = sRecord.segments;
        clearTrack(sObserverSecondary);
        installCanonicalTrack(sObserverSecondary, data, header);
        sObserverSecondaryReady = true;
    } else {
        clearTrack(sPlayback);
        bumpPlaybackToken();
        installCanonicalTrack(sPlayback, data, header);
        sPlaybackPinned = false;
        sObserverPrimaryReady = true;
    }
    sRecording = false;
    sGhostVisible = false;
    sSecondaryGhostVisible = false;
    return true;
}

bool observerTrackReady(bool secondary) {
    return secondary ? sObserverSecondaryReady : sObserverPrimaryReady;
}

bool startObserver() {
    const bool two = sObserverPhase == OBSERVER_PREPARING_TWO;
    if ((sObserverPhase != OBSERVER_PREPARING_ONE && !two) ||
        !sObserverPrimaryReady || (two && !sObserverSecondaryReady) ||
        !gpMarDirector ||
        gpMarDirector->mCurState != TMarDirector::STATE_NORMAL ||
        actionsFastForwardActive() ||
        gSettings.getBool(SETTING_DISABLE_WARPS) ||
        !gSettings.getBool(SETTING_GHOST_DISPLAY) ||
        !observerRoutesCompatible()) {
        return false;
    }
    sObserverPrimarySegment = 0;
    if (!configureObserverSegment()) return false;
    StageLoader::cancel();
    sClockPhase = CLOCK_UNAVAILABLE;
    ILing::invalidateForAssist();
    Records::invalidateAttempt();
    armObserverSegment();
    gBinds.suppressUntilRelease();
    return true;
}

void stopObserver() {
    endObserver(observerRunning(), nullptr);
}

bool observerActive() {
    return observerRunning();
}

bool observerStatsSuppressed() {
    return observerRunning() || sObserverCleanupWarp;
}

bool observerCleanupPending() {
    return sObserverCleanupWarp;
}

bool observerPreparing() {
    return sObserverPhase == OBSERVER_PREPARING_ONE ||
           sObserverPhase == OBSERVER_PREPARING_TWO;
}

int observerGhostCount() {
    if (sObserverPhase == OBSERVER_WARPING_TWO ||
        sObserverPhase == OBSERVER_ACTIVE_TWO) return 2;
    if (sObserverPhase == OBSERVER_WARPING_ONE ||
        sObserverPhase == OBSERVER_ACTIVE_ONE) return 1;
    return 0;
}

bool observerLoading() {
    return sObserverPhase == OBSERVER_WARPING_ONE ||
           sObserverPhase == OBSERVER_WARPING_TWO;
}

int observerVisibleCount() {
    if (!observerRunning()) return 0;
    return (sGhostVisible ? 1 : 0) + (sSecondaryGhostVisible ? 1 : 0);
}

bool playbackInfo(PlaybackInfo *out) {
    if (!out || !sPlayback.valid) return false;
    out->sampleCount = sPlayback.count;
    out->startQf = sPlayback.startQf;
    out->endQf = sPlayback.endQf;
    out->resultQf = sPlayback.resultQf;
    out->routeVariant = sPlayback.parentEpisode;
    out->area = sPlayback.area;
    out->episode = sPlayback.episode;
    out->completed = sPlayback.completed;
    out->pinned = sPlaybackPinned;
    return true;
}

bool visualState(VisualState *out) {
    if (!out) return false;
    out->x = sGhostPosition.x;
    out->y = sGhostPosition.y;
    out->z = sGhostPosition.z;
    out->yaw = sGhostYaw;
    out->animationId = sGhostAnimationId;
    out->animationPhase = sGhostAnimationPhase;
    out->heldObjectId = sGhostHeldObjectId;
    out->heldNameKey = sGhostHeldNameKey;
    out->yoshi = sGhostYoshi;
    out->visible = sGhostVisible;
    return true;
}

bool secondaryVisualState(VisualState *out) {
    if (!out) return false;
    out->x = sSecondaryGhostPosition.x;
    out->y = sSecondaryGhostPosition.y;
    out->z = sSecondaryGhostPosition.z;
    out->yaw = sSecondaryGhostYaw;
    out->animationId = sSecondaryGhostAnimationId;
    out->animationPhase = sSecondaryGhostAnimationPhase;
    out->heldObjectId = sSecondaryGhostHeldObjectId;
    out->heldNameKey = sSecondaryGhostHeldNameKey;
    out->yoshi = sSecondaryGhostYoshi;
    out->visible = sSecondaryGhostVisible;
    return true;
}

void prepareVisual() {
    if (observerRunning()) return;
    sGhostVisible = false;
    sSecondaryGhostVisible = false;
    if (!gpMarDirector || !gpMarioOriginal || sStageRoutePending ||
        !sLiveRouteValid || actionsFastForwardActive() ||
        gQFTTimer.attemptSerial() != sAttemptSerial ||
        sClockPhase == CLOCK_UNAVAILABLE ||
        sClockPhase == CLOCK_WAIT_STAGE) {
        return;
    }

    s32 qf;
    if (!gQFTTimer.currentQf(&qf)) return;
    if (sBoundaryPending || sClockPhase == CLOCK_PROVISIONAL) {
        updateProvisionalPlayback(qf);
    } else if (sClockPhase == CLOCK_ACTIVE ||
               sClockPhase == CLOCK_FINISHED) {
        updatePlayback(qf);
    }
}

bool hasSaveableTrack() { return latestSaveableTrack().track != nullptr; }

bool copySaveableName(char *out, u32 size, u32 *outToken) {
    if (!out || size == 0) return false;
    out[0] = '\0';
    if (outToken) *outToken = 0;
    const SaveSelection selection = latestSaveableTrack();
    if (!selection.track) return false;

    char name[SUSAMUNE_GHOST_NAME_SIZE];
    formatTrackName(*selection.track, name, sizeof(name));
    const u32 length = strlen(name);
    if (length + 1 > size) return false;
    memcpy(out, name, length + 1);
    if (outToken) *outToken = selection.identityToken;
    return true;
}

bool copySaveableName(char *out, u32 size) {
    return copySaveableName(out, size, nullptr);
}

bool copyWarpDiscardName(u8 area, u8 episode, s32 routeVariant,
                         bool boundaryReset,
                         char *out, u32 size, u32 *outToken) {
    (void)area;
    (void)episode;
    (void)routeVariant;
    (void)boundaryReset;
    // Every explicit course departure is destructive product-wise, including
    // same-route restarts. The PB token, not a route prediction, is the guard.
    return copyUnsavedPBName(out, size, outToken);
}

bool copyUnsavedPBName(char *out, u32 size, u32 *outToken) {
    if (!out || size == 0) return false;
    out[0] = '\0';
    if (outToken) *outToken = 0;

    const Track *track = nullptr;
    if (sRecord.valid && sRecord.pb && !sRecord.saved &&
        sRecord.pbToken != 0) {
        track = &sRecord;
    } else if (sPlayback.valid && sPlayback.pb && !sPlayback.saved &&
               sPlayback.pbToken != 0) {
        track = &sPlayback;
    }
    if (!track) return false;

    char name[SUSAMUNE_GHOST_NAME_SIZE];
    formatTrackName(*track, name, sizeof(name));
    const u32 length = strlen(name);
    if (length + 1 > size) return false;
    memcpy(out, name, length + 1);
    if (outToken) *outToken = track->pbToken;
    return true;
}

bool copyPlaybackUnsavedPBName(char *out, u32 size, u32 *outToken) {
    if (!out || size == 0) return false;
    out[0] = '\0';
    if (outToken) *outToken = 0;
    if (!sPlayback.valid || !sPlayback.pb || sPlayback.saved ||
        sPlayback.pbToken == 0) {
        return false;
    }

    char name[SUSAMUNE_GHOST_NAME_SIZE];
    formatTrackName(sPlayback, name, sizeof(name));
    const u32 length = strlen(name);
    if (length + 1 > size) return false;
    memcpy(out, name, length + 1);
    if (outToken) *outToken = sPlayback.pbToken;
    return true;
}

bool hasUnsavedPB() {
    return (sRecord.valid && sRecord.pb && !sRecord.saved &&
            sRecord.pbToken != 0) ||
           (sPlayback.valid && sPlayback.pb && !sPlayback.saved &&
            sPlayback.pbToken != 0);
}

bool hasUnsavedPBToken(u32 token) {
    if (token == 0) return false;
    return (sRecord.valid && sRecord.pb && !sRecord.saved &&
            sRecord.pbToken == token) ||
           (sPlayback.valid && sPlayback.pb && !sPlayback.saved &&
            sPlayback.pbToken == token);
}

bool unprotectUnsavedPB(u32 token) {
    if (token == 0) return false;
    Track *track = nullptr;
    if (sRecord.valid && sRecord.pb && !sRecord.saved &&
        sRecord.pbToken == token) {
        track = &sRecord;
    } else if (sPlayback.valid && sPlayback.pb && !sPlayback.saved &&
               sPlayback.pbToken == token) {
        track = &sPlayback;
    }
    if (!track) return false;
    track->pb = false;
    track->pbToken = 0;
    return true;
}

bool discardUnsavedPB(u32 token) {
    if (token == 0) return false;
    if (sRecord.valid && sRecord.pb && !sRecord.saved &&
        sRecord.pbToken == token) {
        clearRecord();
        sRecording = false;
        sPendingContinueRecording = false;
        sBoundaryPending = false;
        return true;
    }
    if (sPlayback.valid && sPlayback.pb && !sPlayback.saved &&
        sPlayback.pbToken == token) {
        clearTrack(sPlayback);
        bumpPlaybackToken();
        sPlaybackPinned = false;
        sGhostVisible = false;
        rewindPlayback();
        return true;
    }
    return false;
}

bool markCurrentRecordingPB(s32 resultQf) {
    if (resultQf < 0 || !sRecord.valid || !sRecord.completed ||
        sRecord.resultQf != static_cast<u32>(resultQf)) {
        return false;
    }
    if (!sRecord.pb || sRecord.pbToken == 0) {
        sRecord.pbToken = nextPBToken();
    }
    sRecord.pb = true;
    sRecord.saved = false;
    return true;
}

bool playbackPinned() { return sPlaybackPinned; }

void unpinPlayback() { sPlaybackPinned = false; }

void clearPlayback() {
    if (sObserverPhase != OBSERVER_OFF) {
        stopObserver();
        return;
    }
    // Callers must consume the exact PB token first. This keeps a missed UI
    // guard from silently destroying a promoted accepted run.
    if (sPlayback.valid && sPlayback.pb && !sPlayback.saved &&
        sPlayback.pbToken != 0) {
        return;
    }
    clearTrack(sPlayback);
    bumpPlaybackToken();
    sPlaybackPinned = false;
    sGhostVisible = false;
    rewindPlayback();
}

void releaseSavedRecording(u32 recordToken) {
    if (recordToken & kPlaybackTokenBit) {
        if ((recordToken & ~kPlaybackTokenBit) == sPlaybackToken &&
            sPlayback.valid) {
            sPlayback.saved = true;
            sPlayback.pb = false;
            sPlayback.pbToken = 0;
        }
        return;
    }
    if (recordToken != 0 && recordToken == sPlaybackOriginRecordToken &&
        sPlayback.valid) {
        sPlayback.saved = true;
        sPlayback.pb = false;
        sPlayback.pbToken = 0;
        return;
    }
    if (recordToken != 0 && recordToken == sRecordToken && sRecord.valid) {
        sRecord.saved = true;
        sRecord.pb = false;
        sRecord.pbToken = 0;
        if (sPlaybackPinned && !sRecording) clearRecord();
    }
}

void onSavestateLoaded() {
    if (sObserverPhase != OBSERVER_OFF) {
        releaseObserverMario(false);
        endObserver(false, nullptr);
        if (gMenu) gMenu->toast("Observer stopped: savestate loaded");
        return;
    }
    sAttemptSerial = gQFTTimer.attemptSerial();
    clearRecord();
    sRecording = false;
    sGhostVisible = false;
    sStageRoutePending = false;
    sPendingHadLiveRoute = false;
    sPendingContinueRecording = false;
    sBoundaryPending = false;
    if (!sPlaybackPinned) {
        stopAll();
        sAttemptSerial = gQFTTimer.attemptSerial();
        if (gpMarDirector) captureLiveRoute();
        return;
    }

    captureLiveRoute();
    rewindPlayback();
    s32 qf;
    bool stopped;
    if (gQFTTimer.currentQf(&qf, &stopped)) {
        sClockPhase = stopped ? CLOCK_FINISHED : CLOCK_PROVISIONAL;
        sClockObservations = 1;
        sClockLastQf = qf;
        sClockEpochStartQf = qf;
    } else {
        sClockPhase = CLOCK_UNAVAILABLE;
    }
}

}  // namespace Ghost
