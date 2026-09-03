#ifndef _SUSAMUNE_GHOST_HXX
#define _SUSAMUNE_GHOST_HXX

#include <Dolphin/types.h>

class Menu;
class TMarDirector;

namespace Ghost {

struct PlaybackInfo {
    u32 sampleCount;
    u32 startQf;
    u32 endQf;
    u32 resultQf;
    s32 routeVariant;
    u8 area;
    u8 episode;
    bool completed;
    bool pinned;
};

enum RaceSource : u8 {
    RACE_SOURCE_NONE,
    RACE_SOURCE_PERSONAL,
    RACE_SOURCE_IMPORTED,
};

struct RaceContext {
    u32 attemptSerial;
    u32 targetQf;
    s32 startingPbQf;
    s32 routeVariant;
    s16 ilEntry;
    u8 area;
    u8 episode;
    u8 routeParentArea;
    u8 routeFlags;
    RaceSource source;
};

struct VisualState {
    f32 x;
    f32 y;
    f32 z;
    s16 yaw;
    u16 animationId;
    u16 animationPhase;
    u32 heldObjectId;
    u16 heldNameKey;
    u8 yoshi;
    bool visible;
};

void init();
void onStageSetup(TMarDirector *director);
void beforeDirect();
void afterDirect(s32 appState);
void update();
void draw(Menu *menu);
void onSavestateLoaded();

// Canonical SGHF bridge used by the asynchronous ARM storage service.
// Import validates the complete file before replacing the active playback.
bool exportLatest(void *out, u32 capacity, u8 sourceProfile,
                  const char *profileName, u32 *outSize,
                  u32 *outRecordToken);
bool importPlayback(const void *data, u32 size, bool imported);
// Frozen when a pinned library opponent begins a matching QFT attempt.
bool raceContext(RaceContext *out);
bool bindRaceContext(s16 ilEntry, s32 startingPbQf);
// Observer loads reuse the playback and record-slot payloads. The secondary
// track has independent metadata; recording stays disabled until Watch ends.
bool beginObserverPreparation(bool twoGhosts);
bool importObserverTrack(const void *data, u32 size, bool secondary);
bool observerTrackReady(bool secondary);
bool startObserver();
void stopObserver();
bool observerPreparing();
bool observerActive();
// Includes the viewer-owned reload after playback has stopped.
bool observerStatsSuppressed();
bool observerCleanupPending();
int observerGhostCount();
bool observerLoading();
int observerVisibleCount();
bool playbackInfo(PlaybackInfo *out);
// A successful snapshot may still be hidden outside its matching route/QFT.
bool visualState(VisualState *out);
bool secondaryVisualState(VisualState *out);
// Rendering runs inside director->direct(), one QF after the normal update.
// Refresh only the playback cursor there; recording remains post-direct.
void prepareVisual();
bool hasSaveableTrack();
bool copySaveableName(char *out, u32 size);
bool copySaveableName(char *out, u32 size, u32 *outToken);
// Compatibility entry point for course-departure guards. Every explicit
// departure threatens the accepted unsaved PB, including same-route restarts;
// destination arguments no longer predict buffer survival.
bool copyWarpDiscardName(u8 area, u8 episode, s32 routeVariant,
                         bool boundaryReset,
                         char *out, u32 size, u32 *outToken);
// PB tokens are stable logical identities, independent of mutable MEM2 buffer
// generations and record-to-playback promotion.
bool copyUnsavedPBName(char *out, u32 size, u32 *outToken);
// Target-row Clear only destroys playback; do not warn for a PB still owned by
// the independent record buffer.
bool copyPlaybackUnsavedPBName(char *out, u32 size, u32 *outToken);
bool hasUnsavedPB();
// Revalidate or consume the exact accepted PB named by a prior query. PB
// tokens survive record/playback buffer promotion and never name imports.
bool hasUnsavedPBToken(u32 token);
// Stop guarding the accepted PB without deleting its completed track. A
// subsequent restart can still promote it into the automatic race target.
bool unprotectUnsavedPB(u32 token);
bool discardUnsavedPB(u32 token);
// Protect only the just-completed in-memory recording whose result QF was
// accepted as a PB. Imported and pinned library ghosts never qualify.
bool markCurrentRecordingPB(s32 resultQf);
bool playbackPinned();
void unpinPlayback();
void clearPlayback();
void releaseSavedRecording(u32 recordToken);

}  // namespace Ghost

#endif  // _SUSAMUNE_GHOST_HXX
