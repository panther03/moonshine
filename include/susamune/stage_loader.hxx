#ifndef _SUSAMUNE_STAGE_LOADER_HXX
#define _SUSAMUNE_STAGE_LOADER_HXX

#include <Dolphin/types.h>

class Menu;
class TMarDirector;
namespace LevelWarp {
struct Dest;
}

namespace StageLoader {

enum Mode {
    MODE_LOADER,
    MODE_STREAKING,
};

enum { QUEUE_CAPACITY = 120 };
enum { CUSTOM_PLAYLIST_COUNT = 7 };
enum { BUILTIN_PLAYLIST_COUNT = 3 };

struct SessionStats {
    u32 attempts;
    u32 eligibleCompletes;
    u32 qualifyingSuccesses;
    u64 totalObservedActiveQf;
    s32 completedAverageQf;
    u32 bestStreak;
    u32 golds;
};

void init();

int queueCount();
int queueEntry(int position);
bool appendQueue(int entry);
bool removeQueue(int position);
bool moveQueue(int position, int direction);
void clearQueue();
const char *builtinPlaylistName(int preset);
bool loadBuiltinPlaylist(int preset);
bool startLoader();
bool loadCustomPlaylist(int slot);
bool saveCustomPlaylist(int slot);
bool customPlaylistsAvailable();
bool customPlaylistSavePending();
int customPlaylistEntryCount(int slot);
u32 customPlaylistLastError();

// Start one exact IL route repeatedly. A negative target accepts any eligible
// finish; otherwise the result must be at or below targetQf.
bool startStreak(int entry, u16 finishes, s32 targetQf);
bool start(int entry, u16 finishes, s32 targetQf);
void cancel();
bool active();
Mode mode();
void getStats(SessionStats *out);
bool modal();
bool resultOwnsInput();
// True from an authoritative final result through dismissal.
bool resultPending();
// Also covers the bounded gap between Sunshine publishing a Shine event and
// ILing consuming its exact result. This gates departures, not general input.
bool departureResultPending();
// Preset-only action overlay. The persisted Fast Text preference is untouched.
bool fastTextSuppressed();
// True only while the active playlist item deliberately credits a different
// result than the level it starts in.
bool activeRouteMatches(int startEntry, int resultEntry);
// Restart inputs are sampled before IL results. Defer them until the result
// pass so a finish on the same frame wins deterministically.
bool deferRestartInput();
bool acceptDeferredRestart();
// A non-final result already owns the next departure. A restart captured from
// the completed attempt must not survive into the replacement director.
bool retryOwnsDeparture();
bool holdGameModeBeforeUpdate(TMarDirector *director);
// With streak auto-reset disabled, let retail own the save box and replace
// its eventual stage departure with the next exact IL start.
bool holdPostSaveDeparture();
// Main-scene deaths that would lose the selected route can reuse its exact
// start after the retail death sequence finishes. Internal retries stay put.
bool copyDeathRetryDest(LevelWarp::Dest *out);

void update();
void draw(Menu *menu);

// ILing owns attempt identity and reports only exact catalogue routes here.
void onILAttemptStarted(int entry);
void onILAttemptEnded();
void onILResult(int entry, s32 qf, bool eligible);
void onILWarpCancelled();
// Once an assist invalidates any attempt, the whole playlist run is excluded
// from persistent bests even if later retries are clean.
void invalidatePlaylistBest();

}  // namespace StageLoader

#endif  // _SUSAMUNE_STAGE_LOADER_HXX
