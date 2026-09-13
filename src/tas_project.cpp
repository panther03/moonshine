#include "susamune/tas_project.hxx"
#include "susamune/practice_session.hxx"
#include "susamune/savestate.hxx"
#include "susamune/state_storage.hxx"
#include "susamune/state_compatibility.h"
#include <Dolphin/string.h>
extern SavestateManager *gSavestateMgr;
#include <Dolphin/mem.h>

#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata" data=".foxtrot.data" bss=".foxtrot.bss"

namespace TasProject {
namespace {
enum Phase { IDLE, CAPTURE, STARTING, BEGIN_SAVE, EXPORT, COMMIT, READ, PLAN_OPEN, IMPORT, LOAD, CATALOG, RENAME, DELETE, TAPE_EXPORT, TAPE_IMPORT };
struct Ref { u32 slot, generation, key[2]; };
Ref sRefs[3], sPlan[3];
u32 sPublishedKeys[3][2], sSavedRevision;
SusamuneTasManifest sSaved, sPending;
u32 sPhase, sRole, sCurrent, sPreviousCrc;
bool sActive, sDirty, sReplace, sNew, sOpening, sCatalogReady, sOverwrite;
bool sHaveCheckpoint;
PracticeSession::SavestateData sLoadedCheckpoint;
const char *sStatus = "New TAS captures your beginning automatically.";

bool valid(const Ref &ref, PracticeSession::SavestateData *out = nullptr) {
    PracticeSession::SavestateData data;
    if (!gSavestateMgr || ref.slot >= 3 || !ref.generation ||
        gSavestateMgr->slotInfo(ref.slot).generation != ref.generation ||
        !gSavestateMgr->practiceData(ref.slot, &data) ||
        data.stateKey[0] != ref.key[0] || data.stateKey[1] != ref.key[1]) return false;
    if (out) *out = data;
    return true;
}
void finish(const char *text) {
    sPhase = IDLE; sReplace = sNew = sOpening = sOverwrite = false; sStatus = text;
}
void failed(u32 result) {
    finish(result == SUSAMUNE_STATE_FULL ? "Not enough room; existing saved TAS files are safe." :
        result == SUSAMUNE_STATE_WRONG_CONFIG ? "Use this TAS's game version, episode and settings." :
        result == SUSAMUNE_STATE_STALE ? "TAS changed on SD; open it again." :
        result == SUSAMUNE_STATE_UNAVAILABLE ? "TAS files need the matching Moonshine launcher." :
        "TAS could not be opened or saved. Existing SD projects are safe.");
}
bool ready() {
    if (sPhase != IDLE || !gSavestateMgr || SavestateManager::diskBusy() || PracticeSession::starting()) return false;
    return true;
}
void remember(Ref &ref, u32 slot) {
    PracticeSession::SavestateData data;
    ref.slot = slot; ref.generation = gSavestateMgr->slotInfo(slot).generation;
    if (gSavestateMgr->practiceData(slot, &data)) {
        ref.key[0] = data.stateKey[0]; ref.key[1] = data.stateKey[1];
    } else ref.generation = 0;
}
bool chooseEmpty() {
    for (u32 slot = 0; slot < 3; ++slot) {
        if (!replacementAllowed(slot)) continue;
        bool owned = false;
        if (sOpening) for (u32 role = 0; role < 3; ++role)
            if (valid(sRefs[role]) && sRefs[role].slot == slot) owned = true;
        if (!gSavestateMgr->slotInfo(slot).valid || owned) {
            sPlan[sRole] = {slot, gSavestateMgr->slotInfo(slot).generation, {0, 0}};
            return true;
        }
    }
    sReplace = true;
    sStatus = "Choose a memory state to replace, or B to cancel.";
    return false;
}
SusamuneTasRequest context(u32 role, u32 component) {
    SusamuneTasRequest request = {};
    request.projectId = sPending.projectId;
    request.componentId = component;
    request.projectGeneration = sPending.generation;
    request.role = role;
    request.expectedProjectCrc = sOpening ? sPending.checksum : sPreviousCrc;
    request.checksum = SusamuneTasRequestCrc(&request);
    return request;
}
void exportTape() {
    SusamuneTasTakeData take;
    StateCodec::ReadSpan spans[2];
    if (!PracticeSession::captureTake(take, spans) ||
        take.originKey[0] != sRefs[0].key[0] || take.originKey[1] != sRefs[0].key[1] ||
        take.startScene != sPending.sceneKey) {
        finish("This recording cannot be saved with its Beginning."); return;
    }
    SusamuneStateArchiveHeader identity = {};
    identity.gameId = sPending.gameId; identity.buildCrc = sPending.buildCrc;
    identity.configId = sPending.configId; identity.sceneKey = sPending.sceneKey;
    memcpy(identity.name, sPending.name, sizeof(identity.name));
    const auto request = context(SUSAMUNE_TAS_TAPE_ROLE, 0);
    if (!StateStorage::startTapeExport(identity, take, spans[0].data, spans[1].data, request)) {
        failed(SUSAMUNE_STATE_BAD_REQUEST); return;
    }
    sPending.tapeFrames = take.frames;
    sPhase = TAPE_EXPORT; sStatus = "Saving TAS recording. Keep SD connected.";
}
void nextExport() {
    while (sRole < 3) {
        if (!valid(sRefs[sRole])) { ++sRole; continue; }
        const auto &old = sSaved.components[sRole];
        // A smaller repacked RAM state must replace an older, larger file too.
        if (sPending.projectId == sSaved.projectId && old.componentId &&
            sRefs[sRole].key[0] == sPublishedKeys[sRole][0] &&
            sRefs[sRole].key[1] == sPublishedKeys[sRole][1] &&
            old.packedBytes <= gSavestateMgr->slotInfo(sRefs[sRole].slot).packedBytes) {
            sPending.components[sRole] = old; ++sPending.componentCount; ++sRole; continue;
        }
        break;
    }
    if (sRole == 3) {
        exportTape(); return;
    }
    const SusamuneTasRequest request = context(sRole, 0);
    if (!gSavestateMgr->exportSlotExplicit(sRefs[sRole].slot, sRefs[sRole].generation, &request)) {
        finish(gSavestateMgr->sdStatus()); return;
    }
    sPhase = EXPORT; sStatus = "Saving TAS and checkpoints. Keep SD connected.";
}
void beginExports() {
    memset(sPending.components, 0, sizeof(sPending.components));
    sPending.componentCount = 0;
    sPending.currentRole = valid(sRefs[sCurrent]) ? sCurrent : 0;
    sPending.startKey[0] = sRefs[0].key[0]; sPending.startKey[1] = sRefs[0].key[1];
    sRole = 0; nextExport();
}
void importTape() {
    if (!StateStorage::startTapeImport(sPending)) { failed(SUSAMUNE_STATE_BAD_REQUEST); return; }
    sPhase = TAPE_IMPORT; sStatus = "Opening TAS recording...";
}
void nextImport() {
    while (sRole < 3 && !sPending.components[sRole].componentId) ++sRole;
    if (sRole == 3) {
        sHaveCheckpoint = false;
        sRole = sPending.currentRole;
        if (gSavestateMgr->slotSceneKey(sPlan[sRole].slot) != gSavestateMgr->currentSceneKey()) sRole = 0;
        if (gSavestateMgr->slotSceneKey(sPlan[sRole].slot) != gSavestateMgr->currentSceneKey()) {
            importTape(); return;
        }
        sPhase = LOAD;
        sStatus = "Opening the current TAS checkpoint..."; return;
    }
    const SusamuneTasComponent &component = sPending.components[sRole];
    const SusamuneTasRequest request = context(sRole, component.componentId);
    const Ref &target = sPlan[sRole];
    if (!gSavestateMgr->importSlotExplicit(target.slot, target.generation, component.componentId,
        component.headerCrc, component.packedBytes, &request, &sPending)) {
        finish(gSavestateMgr->sdStatus()); return;
    }
    sPhase = IMPORT; sStatus = "Opening TAS and checkpoints. Keep SD connected.";
}
void planOpen() {
    while (sRole < 3) {
        if (sPending.components[sRole].componentId && !chooseEmpty()) return;
        ++sRole;
    }
    // The user has saved/discarded the previous project before opening another.
    // Clearing only its owned slots creates room to stage incoming components.
    for (u32 role = 0; role < 3; ++role) {
        if (!valid(sRefs[role])) continue;
        const Ref old = sRefs[role];
        if (!gSavestateMgr->clearSlot(old.slot, old.generation)) {
            finish("Previous TAS changed; open the project again."); return;
        }
        for (u32 target = 0; target < 3; ++target)
            if (sPending.components[target].componentId && sPlan[target].slot == old.slot)
                sPlan[target].generation = gSavestateMgr->slotInfo(old.slot).generation;
    }
    sRole = 0; nextImport();
}
}
bool active() { return sActive && valid(sRefs[0]); }
bool busy() { return sPhase != IDLE; }
bool dirty() { return sDirty || (sActive && PracticeSession::editRevision() != sSavedRevision); }
bool named() { return sSaved.projectId != 0; }
const char *name() { return sSaved.name[0] ? sSaved.name : "Unsaved TAS"; }
const char *status() { return sStatus ? sStatus : PracticeSession::status(); }
const char *roleName(u32 role) {
    static const char *const names[] = {"Beginning", "Checkpoint 1", "Checkpoint 2"};
    return role < 3 ? names[role] : "Checkpoint";
}
Checkpoint checkpoint(u32 role) {
    PracticeSession::SavestateData data;
    const bool present = active() && role < 3 && valid(sRefs[role], &data);
    return {present, present ? data.frames : 0,
        present && gSavestateMgr->slotSceneKey(sRefs[role].slot) == gSavestateMgr->currentSceneKey()};
}
bool replacementNeeded() { return sReplace; }
bool replacementAllowed(u32 slot) {
    if (!gSavestateMgr || slot >= 3) return false;
    if (sOpening) {
        for (u32 role = 0; role < sRole; ++role)
            if (sPending.components[role].componentId && sPlan[role].slot == slot) return false;
    } else if (!sNew) {
        for (u32 role = 0; role < 3; ++role)
            if (role != sRole && valid(sRefs[role]) && sRefs[role].slot == slot) return false;
    }
    return true;
}
bool replace(u32 slot, u32 generation) {
    if (!sReplace || !replacementAllowed(slot) || gSavestateMgr->slotInfo(slot).generation != generation) return false;
    sPlan[sRole] = {slot, generation, {0, 0}}; sReplace = false;
    if (sOpening) { ++sRole; planOpen(); }
    return true;
}
void cancelReplacement() { if (sReplace) finish("TAS action canceled. Memory states were not changed."); }
bool newProject() {
    if (!ready() || !PracticeSession::projectAvailable()) {
        sStatus = "Start a TAS when Mario can move in the level."; return false;
    }
    sNew = true; sRole = 0; sPhase = CAPTURE;
    if (active() && valid(sRefs[0])) sPlan[0] = sRefs[0];
    else chooseEmpty();
    return true;
}
static bool captureCheckpoint(u32 role, bool confirm) {
    if (ready() && active() && role > 0 && role < 3 &&
        PracticeSession::attachedTo(sRefs[0].key) && !PracticeSession::atRecordedScene()) {
        sStatus = "Checkpoint unavailable here after desync. Replay can continue."; return false;
    }
    if (!ready() || !active() || role == 0 || role >= 3 || !PracticeSession::attachedTo(sRefs[0].key) ||
        !PracticeSession::checkpointReady()) {
        sStatus = "Continue or open this TAS before saving a checkpoint."; return false;
    }
    PracticeSession::pauseForCheckpoint();
    sRole = role; sPhase = CAPTURE; sNew = false;
    if (valid(sRefs[role])) {
        sPlan[role] = sRefs[role];
        sOverwrite = confirm;
        if (confirm) sStatus = "Replace this TAS checkpoint?";
    } else chooseEmpty();
    return true;
}
bool saveCheckpoint(u32 role) { return captureCheckpoint(role, true); }
bool checkpointOverwritePending() { return sOverwrite; }
bool promptPending() { return sReplace || sOverwrite; }
u32 pendingCheckpointRole() { return sRole; }
bool confirmCheckpointOverwrite(bool accept) {
    if (!sOverwrite) return false;
    if (!accept) { finish("Checkpoint kept. Your TAS is paused."); return true; }
    if (sRole == 0 || sRole >= 3 || !valid(sPlan[sRole]) ||
        !active() || !PracticeSession::attachedTo(sRefs[0].key) ||
        !PracticeSession::checkpointReady()) {
        finish("Checkpoint changed; choose Save Checkpoint again."); return false;
    }
    sOverwrite = false; return true;
}
bool dispatchShortcut(BindId id) {
    if (id < BIND_TAS_BEGINNING || id > BIND_TAS_REPLAY) return false;
    if (!ready()) return true;
    switch (id) {
    case BIND_TAS_BEGINNING: loadCheckpoint(0); break;
    case BIND_TAS_SAVE_CHECKPOINT1: saveCheckpoint(1); break;
    case BIND_TAS_LOAD_CHECKPOINT1: loadCheckpoint(1); break;
    case BIND_TAS_SAVE_CHECKPOINT2: saveCheckpoint(2); break;
    case BIND_TAS_LOAD_CHECKPOINT2: loadCheckpoint(2); break;
    case BIND_TAS_CONTINUE: continueEditing(); break;
    case BIND_TAS_REPLAY: replay(); break;
    default: break;
    }
    return true;
}
bool loadCheckpoint(u32 role) {
    if (!ready() || !checkpoint(role).present) { sStatus = "This checkpoint has not been saved yet."; return false; }
    if (!checkpoint(role).loadableHere) {
        sStatus = role ? "This checkpoint is in another area. Return there to load it." :
            "Return to the area where this TAS begins, then Go to Beginning.";
        return false;
    }
    if (!role) {
        if (!PracticeSession::takeBelongsTo(sRefs[0].key)) {
            sStatus = "Open this TAS or load one of its checkpoints first."; return false;
        }
        if (!PracticeSession::requestBeginning()) { sStatus = PracticeSession::status(); return false; }
        sCurrent = 0; sStatus = nullptr; return true;
    }
    sRole = role; sPlan[role] = sRefs[role]; sPhase = LOAD; sOpening = false;
    return true;
}
bool continueEditing() {
    if (active() && PracticeSession::attachedTo(sRefs[0].key) && !PracticeSession::atRecordedScene()) {
        sStatus = "Area timing differs - return to a checkpoint to edit"; return false;
    }
    if (!active() || !PracticeSession::attachedTo(sRefs[0].key) || !PracticeSession::requestContinue()) {
        sStatus = "Open this TAS or load one of its checkpoints first."; return false;
    }
    sStatus = nullptr; return true;
}
bool replay() {
    if (active() && !checkpoint(0).loadableHere) {
        sStatus = "Return to the area where this TAS begins, then Replay."; return false;
    }
    if (!active() || !PracticeSession::takeBelongsTo(sRefs[0].key) || !PracticeSession::requestPlayback()) {
        sStatus = PracticeSession::status(); return false;
    }
    sStatus = nullptr; return true;
}
bool save(const char *text) {
    if (!text || !text[0] || !SusamuneStateNameValid(text) || !StateStorage::available()) {
        sStatus = "Enter a name and use the matching Moonshine launcher."; return false;
    }
    if (!ready() || !active() || !PracticeSession::takeBelongsTo(sRefs[0].key)) {
        sStatus = "Open this TAS or load one of its checkpoints before saving."; return false;
    }
    PracticeSession::pauseEditing();
    sPending = sSaved;
    // Legacy components stay immutable; their first new save gets its own project.
    if (sPending.buildCrc != SUSAMUNE_STATE_COMPATIBILITY_ID) {
        sPending.projectId = sPending.generation = sPending.checksum = 0;
    }
    sPending.magic = SUSAMUNE_TAS_MAGIC; sPending.version = SUSAMUNE_TAS_VERSION;
    memset(sPending.name, 0, sizeof(sPending.name));
    for (u32 i = 0; i < 31 && text[i]; ++i) sPending.name[i] = text[i];
    sPreviousCrc = sPending.checksum;
    if (!sPending.projectId) {
        if (!StateStorage::projectBegin(sPending.name)) { failed(SUSAMUNE_STATE_UNAVAILABLE); return false; }
        sPhase = BEGIN_SAVE;
    } else beginExports();
    return true;
}
bool open(u32 id, u32 checksum) {
    if (!ready() || !StateStorage::projectRead(id, checksum)) return false;
    sOpening = true; sPhase = READ; sStatus = "Reading TAS project..."; return true;
}
bool refresh(u32 afterId) {
    if (!ready() || !StateStorage::projectCatalog(afterId)) {
        sStatus = "TAS files need the matching Moonshine launcher."; return false;
    }
    sCatalogReady = false; sPhase = CATALOG; sStatus = "Finding saved TAS projects..."; return true;
}
bool rename(u32 id, u32 checksum, const char *text) {
    if (!ready() || !StateStorage::projectRename(id, checksum, text)) return false;
    sPreviousCrc = checksum; sPhase = RENAME; sCatalogReady = false;
    sStatus = "Renaming TAS project..."; return true;
}
bool remove(u32 id, u32 checksum) {
    if (!ready() || !StateStorage::projectDelete(id, checksum)) return false;
    sPhase = DELETE; sCatalogReady = false;
    sStatus = "Deleting the SD project. Memory checkpoints stay."; return true;
}
bool catalogReady() { return sCatalogReady && StateStorage::catalogReady(); }
const SusamuneStateCatalog &catalog() { return StateStorage::catalog(); }
void update() {
    if (sPhase == IDLE || promptPending()) return;
    if (sPhase == CAPTURE) {
        const Ref target = sPlan[sRole];
        if (gSavestateMgr->slotInfo(target.slot).generation != target.generation ||
            !gSavestateMgr->saveSlotExplicit(target.slot, true, sNew)) {
            finish("Checkpoint could not be saved. Existing memory states are safe."); return;
        }
        if (sNew) {
            for (u32 role = 0; role < 3; ++role)
                if (valid(sRefs[role]) && sRefs[role].slot != target.slot)
                    gSavestateMgr->clearSlot(sRefs[role].slot, sRefs[role].generation);
            memset(sRefs, 0, sizeof(sRefs)); memset(&sSaved, 0, sizeof(sSaved));
            sActive = true;
        }
        remember(sRefs[sRole], target.slot); sCurrent = sRole; sDirty = true;
        if (sNew) {
            if (!PracticeSession::requestRecordFrom(target.slot, sRefs[0].generation)) {
                finish(PracticeSession::status()); return;
            }
            sPhase = STARTING; sStatus = "Release A to begin. Gameplay will stay paused.";
        } else finish("Checkpoint saved. Your TAS is paused here.");
        return;
    }
    if (sPhase == STARTING) {
        if (!PracticeSession::starting()) finish(PracticeSession::recording() ? nullptr : PracticeSession::status());
        return;
    }
    if (sPhase == EXPORT || sPhase == IMPORT) {
        SavestateManager::TransferResult result;
        if (!gSavestateMgr->takeTransferResult(result)) return;
        if (result.status != SUSAMUNE_STATE_OK) { failed(result.status); return; }
        if (sPhase == EXPORT) {
            const SusamuneStateArchiveHeader &h = result.header;
            if (sRole && (h.gameId != sPending.gameId || h.buildCrc != sPending.buildCrc ||
                h.configId != sPending.configId)) { failed(SUSAMUNE_STATE_WRONG_CONFIG); return; }
            sPending.gameId = h.gameId; sPending.buildCrc = h.buildCrc;
            sPending.configId = h.configId;
            if (!sRole) sPending.sceneKey = h.sceneKey;
            sPending.components[sRole] = {result.id, h.headerCrc, h.packedSize,
                checkpoint(sRole).frames, h.sceneKey};
            ++sPending.componentCount; ++sRole; nextExport();
        } else {
            remember(sPlan[sRole], result.slot);
            PracticeSession::SavestateData data;
            const u32 key[2] = {sPending.startKey[0], sPending.startKey[1]};
            if (!valid(sPlan[sRole], &data) || !PracticeSession::projectSavestateMatches(
                    data, key, sRole, sPending.components[sRole].frames)) {
                failed(SUSAMUNE_STATE_BAD_FILE); return;
            }
            ++sRole; nextImport();
        }
        return;
    }
    StateStorage::Result result;
    if (sPhase == TAPE_EXPORT || sPhase == TAPE_IMPORT) {
        if (!StateStorage::takeResult(result)) return;
        if (result.status != SUSAMUNE_STATE_OK) { failed(result.status); return; }
        if (sPhase == TAPE_EXPORT) {
            const auto &h = result.header;
            if (h.gameId != sPending.gameId || h.buildCrc != sPending.buildCrc ||
                h.configId != sPending.configId || h.sceneKey != sPending.sceneKey ||
                !SusamuneTasTapeHeaderValid(&h)) { failed(SUSAMUNE_STATE_BAD_FILE); return; }
            sPending.tape = {result.id, h.headerCrc, h.packedSize};
            ++sPending.generation;
            if (!sPending.generation) { finish("TAS save version limit reached; use a new project."); return; }
            sPending.checksum = SusamuneTasManifestCrc(&sPending);
            if (!SusamuneTasManifestValid(&sPending) || !StateStorage::projectCommit(sPending, sPreviousCrc)) {
                failed(SUSAMUNE_STATE_BAD_REQUEST); return;
            }
            sPhase = COMMIT; sStatus = "Finishing TAS save...";
        } else {
            SusamuneTasTakeData take;
            const void *frames, *transitions;
            if (!StateStorage::tapePayload(result, take, frames, transitions) ||
                take.frames != sPending.tapeFrames || take.startScene != sPending.sceneKey ||
                take.originKey[0] != sPending.startKey[0] || take.originKey[1] != sPending.startKey[1]) {
                failed(SUSAMUNE_STATE_BAD_FILE); return;
            }
            bool restored = sHaveCheckpoint && PracticeSession::restoreTake(take, frames, transitions, &sLoadedCheckpoint);
            if (!restored) {
                sHaveCheckpoint = false;
                restored = PracticeSession::restoreTake(take, frames, transitions);
            }
            if (!restored) { failed(SUSAMUNE_STATE_BAD_FILE); return; }
            memcpy(sRefs, sPlan, sizeof(sRefs)); sSaved = sPending; sActive = true; sDirty = false;
            sSavedRevision = PracticeSession::editRevision();
            for (u32 role = 0; role < 3; ++role) {
                sPublishedKeys[role][0] = sRefs[role].key[0];
                sPublishedKeys[role][1] = sRefs[role].key[1];
            }
            sCurrent = sRole;
            finish(sHaveCheckpoint ? "TAS opened at a checkpoint. Continue edits; Replay watches the full take." :
                "TAS recording kept. Return to its beginning area to Replay.");
        }
        return;
    }
    if (!StateStorage::takeProjectResult(result)) return;
    if (result.status != SUSAMUNE_STATE_OK) { failed(result.status); return; }
    if (sPhase == BEGIN_SAVE) {
        sPending.projectId = result.id; beginExports();
    } else if (sPhase == COMMIT) {
        sSaved = sPending; sDirty = false; sSavedRevision = PracticeSession::editRevision();
        for (u32 role = 0; role < 3; ++role) {
            sPublishedKeys[role][0] = sRefs[role].key[0];
            sPublishedKeys[role][1] = sRefs[role].key[1];
        }
        finish("TAS recording, Beginning and checkpoints saved to SD.");
    } else if (sPhase == READ) {
        if (!SusamuneTasManifestValid(&result.project)) { failed(SUSAMUNE_STATE_BAD_FILE); return; }
        if (!gSavestateMgr->projectCompatible(result.project)) { failed(SUSAMUNE_STATE_WRONG_CONFIG); return; }
        sPending = result.project; memset(sPlan, 0, sizeof(sPlan));
        sRole = 0; sPhase = PLAN_OPEN; planOpen();
    } else if (sPhase == CATALOG) {
        sCatalogReady = true; finish("Choose a TAS project to open.");
    } else if (sPhase == RENAME || sPhase == DELETE) {
        const bool renaming = sPhase == RENAME;
        if (result.id == sSaved.projectId) {
            if (renaming && sSaved.checksum == sPreviousCrc) sSaved = result.project;
            else { memset(&sSaved, 0, sizeof(sSaved)); sDirty = true; }
        }
        finish(renaming ? "TAS renamed on SD." : "TAS deleted from SD. Memory checkpoints were kept.");
    }
}
void afterDraw() {
    if (sPhase != LOAD || sReplace) return;
    const Ref ref = sPlan[sRole];
    if (!valid(ref) || !gSavestateMgr->loadSlot(ref.slot, ref.generation)) {
        if (sOpening) {
            sHaveCheckpoint = false;
            importTape(); return;
        }
        finish("Checkpoint could not be loaded. Check the episode and settings."); return;
    }
    PracticeSession::pauseEditing();
    if (sOpening) {
        if (!gSavestateMgr->practiceData(ref.slot, &sLoadedCheckpoint)) {
            failed(SUSAMUNE_STATE_BAD_FILE); return;
        }
        sHaveCheckpoint = true;
        importTape(); return;
    }
    const u32 expected[2] = {sRefs[0].key[0], sRefs[0].key[1]};
    if (!PracticeSession::attachedTo(expected) || !PracticeSession::checkpointReady()) {
        finish("TAS checkpoint could not resume. Check its gameplay settings."); return;
    }
    sCurrent = sRole;
    finish("TAS is paused here. Continue to edit, or Replay to watch.");
}
}
