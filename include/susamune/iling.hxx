#ifndef _SUSAMUNE_ILING_HXX
#define _SUSAMUNE_ILING_HXX

#include <Dolphin/types.h>

class Menu;
namespace LevelWarp {
struct Dest;
}

namespace ILing {

void init();
// Dolphin's CARD backend becomes available after init(), during the boot
// state. Console persistence is already loaded by init().
void onPersistenceReady();
int count();
const char *label(int entry);
const char *shortLabel(int entry);
// Streaking repeats episodes, so bonus/100-coin Shines are not start choices.
bool streakEntrySelectable(int entry);
// A Streaking finish may be any Shine collected from the selected start scene.
bool sameEpisodeShine(int selectedEntry, int completedEntry);
s32 pbQf(int entry);
// Stable across catalogue reorderings; shared by PBs and per-level targets.
int persistentSlot(int entry);
void formatTime(s32 qf, char *out, u32 size,
                const char *format = "%d:%02d.%03d");
// Available only on Any% and only after every route IL has a PB.
bool anyPercentTheoryQf(s32 *out);
int pbProfile();
const char *pbProfileName(int profile);
bool pbProfileNameEditable(int profile);
void cyclePbProfile(int direction);
void setPbProfileName(int profile, const char *name);
int jumpGroup(int entry, int direction);
bool beginsGroup(int entry);
const char *groupName(int entry);
// The ILs tab projects GBS into Gelato without changing persisted entry ids.
int menuEntryAt(int position);
int jumpMenuGroup(int position, int direction);
bool beginsMenuGroup(int position);
const char *menuGroupName(int position);
// Parent episode retained by the active IL attempt, or -1 when it does not
// describe `parentArea`. Direct internal starts use this for full restart.
int activeParentEpisode(u8 parentArea);
// True when the current route deliberately entered a normally-main internal
// area from its parent and Full Restart must return to that parent.
bool forceParentFullRestart(u8 parentArea);
// Pinna 1 deliberately uses retail Exit Area during both movie skips.
bool preserveRetailExitArea();
// Copy the exact selected start when its retail death would return to Plaza.
// Internal scenes and other retail special retries stay untouched.
bool copySessionDeathRetryDest(int entry, LevelWarp::Dest *out);

bool start(int entry);
bool start(int entry, u32 approvedDiscardToken);
bool copyWarpDiscardName(int entry, char *out, u32 size, u32 *outToken);
// A course warp can be cancelled by its final ghost-ownership check after the
// selected attempt was armed but before the director began leaving.
void cancelWarpStart();
void commitWarpStart();
void cancelPendingWarp();
void clearPB(int entry);
void update();
// Called at LevelWarp's transition tail, after the old director is finished
// but before the destination director is constructed.
void onWarpTail();
// Drop Watch's assisted attempt before the cleanup stage arms normal play.
void resetAfterObserver();
void beforeStageSetup();
void onStageSetup();
void onSavestateSaved();
void onSavestateLoaded();
// Revoke PB, Records and challenge credit without changing the QFT clock.
void invalidateForAssist();
bool achievementChimeBlocked();

// PB result banner, drawn through Menu's shared no-allocation renderer.
void draw(Menu *menu);
void drawRecentPreview(Menu *menu);

}  // namespace ILing

#endif  // _SUSAMUNE_ILING_HXX
