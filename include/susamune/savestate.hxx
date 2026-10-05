#ifndef _SUSAMUNE_SAVESTATE_HXX
#define _SUSAMUNE_SAVESTATE_HXX

#include <Dolphin/types.h>
#include "susamune/state_storage.h"

class Menu;
namespace PracticeSession { struct SavestateData; }

class SavestateManager {
public:
    enum { kSlotCount = 3 };
    struct SlotInfo {
        bool valid;
        u8 area;
        u8 episode;
        u32 generation;
        u32 packedBytes;
    };

    SavestateManager();

    u32 activeSlot() const;
    u32 saveSlot() const;
    u32 loadSlot() const;
    SlotInfo slotInfo(u32 slot) const;
    bool practiceData(u32 slot, PracticeSession::SavestateData *out) const;
    bool selectSlot(u32 slot);
    bool cycleSlot();
    bool selectSaveSlot(u32 slot);
    bool selectLoadSlot(u32 slot);
    bool cycleSaveSlot();
    bool cycleLoadSlot();
    bool clearSlot(u32 slot, u32 expectedGeneration);

    static bool diskBusy();
    static bool saveDialogOpen();
    void updateDisk();
    struct TransferResult {
        u32 command, status, id, slot, generation;
        SusamuneStateArchiveHeader header;
    };
    bool takeTransferResult(TransferResult &out);
    bool projectCompatible(const SusamuneTasManifest &project) const;
    u32 slotSceneKey(u32 slot) const;
    u32 currentSceneKey() const;
    bool exportSlotExplicit(u32 slot, u32 expectedGeneration, const SusamuneTasRequest *project);
    bool importSlotExplicit(u32 slot, u32 expectedGeneration, u32 archiveId,
                            u32 expectedHeaderCrc, u32 packedBytes, const SusamuneTasRequest *project,
                            const SusamuneTasManifest *manifest = nullptr);
    bool saveToSD(const char *name = nullptr);
    bool loadFromSD(u32 archiveId, u32 expectedHeaderCrc, u32 packedBytes);
    bool selectSDForLoad(u32 archiveId, u32 expectedHeaderCrc, u32 packedBytes, const char *name);
    bool loadSourceIsSD() const;
    u32 selectedSDId() const;
    const char *selectedSDName() const;
    bool renameSD(u32 archiveId, u32 expectedHeaderCrc, const char *name);
    bool deleteSD(u32 archiveId, u32 expectedHeaderCrc);
    bool refreshSD(u32 afterId = 0);
    bool cancelSD();
    bool sdAvailable() const;
    bool sdCatalogReady() const;
    const SusamuneStateCatalog &sdCatalog() const;
    const char *sdStatus() const;

    // Called once per frame from main.cpp's onUpdate hook. Polls the d-pad
    // and triggers saves. Loads are queued until the post-render hook so the
    // current frame cannot consume a mixture of live and restored state.
    void updateHook();

    // Called after the game's THPPlayerDrawDone()/GXDrawDone barrier. A queued
    // load is restored here, after director, fader, audio, and rendering work
    // for the current frame has finished.
    void processPendingLoad();

    // Drawn after the scene each frame; the production prompt uses the
    // configurable Creation style and the optional debug label stays separate.
    void draw(Menu *menu);

    // Public so callers can trigger from elsewhere (e.g. a debug menu).
    bool saveState();
    bool saveSlotExplicit(u32 slot, bool forceRng = false, bool omitPracticeTake = false);
    bool loadState();
    // Replay loads its original slot even if the menu selection changed.
    bool loadSlot(u32 slot, u32 expectedGeneration);

private:
    void feedback(const char *debug, const char *message);
    bool beginSDExport(u32 slot, u32 expectedGeneration, const char *name,
                       const SusamuneTasRequest *project);
    bool beginSDLoad(u32 archiveId, u32 expectedHeaderCrc, u32 packedBytes, bool restore,
                     u32 slot = kSlotCount, const SusamuneTasRequest *project = nullptr);

#if ENABLE_SAVESTATE_DBG
    void setStatus(const char *msg);
#endif
    char mFeedback[48];
    int  mFeedbackFrames;
    bool mLoadPending;
    u16  mLoadWaitFrames;
};
static_assert(sizeof(SavestateManager) == 56,
              "savestate controller layout changed");

#endif // _SUSAMUNE_SAVESTATE_HXX
