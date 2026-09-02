#include "susamune/ghost_storage.hxx"

#include "Dolphin/OS.h"
#include "Dolphin/mem.h"
#include "susamune/ghost.hxx"
#include "susamune/iling.hxx"
#include "susamune/menu.hxx"
#include "susamune/records.hxx"
#include "susamune/records_persistence.hxx"

namespace GhostStorage {
namespace {

const u32 kTimeoutFrames = 30u * 60u;
const u32 kCatalogBytes =
    sizeof(SusamuneGhostSlotInfo) * SUSAMUNE_GHOST_SLOT_COUNT;
const u32 kImportedCatalogBytes =
    sizeof(SusamuneGhostSlotInfo) * SUSAMUNE_GHOST_IMPORTED_MAX_ENTRIES;

const char kUnavailable[] = "Ghost storage unavailable";
const char kDolphinUnavailable[] = "Ghost storage unavailable in Dolphin";
const char kReady[] = "Ghost storage ready";
const char kRefreshing[] = "Refreshing ghost slots";
const char kSaving[] = "Saving ghost";
const char kLoading[] = "Loading ghost";
const char kDeleting[] = "Deleting ghost";
const char kExportingShare[] = "Exporting .smsghost file";
const char kImportingShare[] = "Scanning imported ghosts";
const char kSaved[] = "Ghost saved";
const char kLoaded[] = "Ghost loaded";
const char kRaceLoaded[] = "Ghost ready - restart to race";
const char kDeleted[] = "Ghost deleted";
const char kExportedShare[] = "Ghost exported to share folder";
const char kExportExists[] = "Export filename already exists";
const char kImportedShare[] = "Imported ghosts refreshed";
const char kImportedOverflow[] = "12 imported ghosts shown; more found";
const char kSaveChanged[] = "Ghost changed; confirm save again";
const char kLoadIgnored[] = "Stale ghost load ignored";
const char kNoRecording[] = "No ghost ready to save";
const char kCatalogNeeded[] = "Refresh ghost slots first";
const char kBusy[] = "Ghost storage busy";
const char kBadSlot[] = "Invalid ghost slot";
const char kOccupied[] = "Ghost slot occupied";
const char kEmpty[] = "Ghost slot is empty";
const char kUnsafe[] = "Ghost slot is unsafe";
const char kExportFailed[] = "Could not prepare ghost file";
const char kImportFailed[] = "Loaded ghost failed validation";
const char kTimeout[] = "Ghost storage timed out; still waiting";
const char kProtocol[] = "Ghost storage protocol error";
const char kInvalidRequest[] = "Ghost storage request rejected";
const char kFileTooLarge[] = "Ghost file is too large";
const char kInvalidFile[] = "Ghost file is corrupt";
const char kForwardVersion[] = "Ghost version is unsupported";
const char kQuota[] = "Ghost profile quota reached";
const char kNotFound[] = "Ghost slot was not found";
const char kIoError[] = "Ghost storage I/O error";
const char kCatalogInvalid[] = "Ghost catalog failed validation";
const char kEmptyName[] = "Empty slot";
const char kUnsafeName[] = "Unsafe ghost";
const char kUnnamedName[] = "Unnamed ghost";

enum LoadDestination {
    LOAD_DESTINATION_RACE,
    LOAD_DESTINATION_OBSERVER_PRIMARY,
    LOAD_DESTINATION_OBSERVER_SECONDARY,
};

SusamuneGhostSlotInfo *const sCatalog =
    reinterpret_cast<SusamuneGhostSlotInfo *>(
        SUSAMUNE_GHOST_CATALOG_CACHE_PPC_BASE);
SusamuneGhostSlotInfo *const sImportedCatalog =
    sCatalog + SUSAMUNE_GHOST_SLOT_COUNT;
static_assert(kCatalogBytes + kImportedCatalogBytes ==
                  SUSAMUNE_GHOST_CATALOG_CACHE_SIZE,
              "ghost catalog cache layout changed");
const char *sStatus = kUnavailable;
u32 sSequence;
u32 sPendingSequence;
u32 sPendingRecordToken;
u32 sPendingDurationQf;
u32 sPendingExpectedGeneration;
u32 sWaitFrames;
u32 sEpoch;
u32 sPendingEpoch;
u32 sTotalDuration;
u32 sImportedTotalDuration;
u32 sImportedOverflow;
u16 sPendingCommand;
u16 sPendingProfile;
u16 sPendingSlot;
u8 sPendingLoadDestination;
u8 sProfile;
s8 sLoadedSlot;
u8 sLoadedProfile;
bool sAvailable;
bool sCatalogReady;
bool sImportedCatalogReady;
bool sRefreshQueued;
bool sImportedRefreshQueued;
bool sTimedOut;

void notify(const char *status) {
    sStatus = status;
    if (gMenu) gMenu->toast(status);
}

void observeLoadedPlayback() {
    if (sLoadedSlot < 0 || Ghost::playbackPinned()) return;
    sLoadedSlot = -1;
    sLoadedProfile = 0xff;
}

void detachImportedAssociation(bool clearPlayback) {
    // Imported row numbers are lexical positions and can change after a scan.
    if (sLoadedProfile != SUSAMUNE_GHOST_IMPORTED_PROFILE) return;
    if (clearPlayback && Ghost::playbackPinned()) Ghost::clearPlayback();
    sLoadedSlot = -1;
    sLoadedProfile = 0xff;
}

u8 activeProfile() {
    const int profile = ILing::pbProfile();
    return profile >= 0 && profile < (int)SUSAMUNE_GHOST_PROFILE_COUNT
        ? static_cast<u8>(profile) : 0;
}

void clearCatalog() {
    memset(sCatalog, 0, kCatalogBytes);
    sCatalogReady = false;
    sTotalDuration = 0;
}

void clearImportedCatalog() {
    memset(sImportedCatalog, 0, kImportedCatalogBytes);
    sImportedCatalogReady = false;
    sImportedTotalDuration = 0;
    sImportedOverflow = 0;
}

void observeProfile() {
    const u8 profile = activeProfile();
    if (profile == sProfile) return;
    if (sPendingCommand == SUSAMUNE_GHOST_CMD_LOAD &&
        (sPendingLoadDestination == LOAD_DESTINATION_OBSERVER_PRIMARY ||
         sPendingLoadDestination == LOAD_DESTINATION_OBSERVER_SECONDARY)) {
        Ghost::stopObserver();
    }
    sProfile = profile;
    clearCatalog();
    sRefreshQueued = sAvailable;
}

const char *statusForCode(s32 status) {
    switch (status) {
    case SUSAMUNE_GHOST_STATUS_OK: return kReady;
    case SUSAMUNE_GHOST_STATUS_INVALID_REQUEST: return kInvalidRequest;
    case SUSAMUNE_GHOST_STATUS_INVALID_SLOT: return kBadSlot;
    case SUSAMUNE_GHOST_STATUS_PAYLOAD_TOO_LARGE: return kFileTooLarge;
    case SUSAMUNE_GHOST_STATUS_INVALID_FILE: return kInvalidFile;
    case SUSAMUNE_GHOST_STATUS_FORWARD_VERSION: return kForwardVersion;
    case SUSAMUNE_GHOST_STATUS_QUOTA_EXCEEDED: return kQuota;
    case SUSAMUNE_GHOST_STATUS_STORAGE_UNAVAILABLE: return kUnavailable;
    case SUSAMUNE_GHOST_STATUS_SLOT_UNSAFE: return kUnsafe;
    case SUSAMUNE_GHOST_STATUS_NOT_FOUND: return kNotFound;
    case SUSAMUNE_GHOST_STATUS_SLOT_OCCUPIED: return kOccupied;
    default:
        return status >= SUSAMUNE_GHOST_STATUS_IO_BASE ? kIoError : kProtocol;
    }
}

bool safeText(const char *text, u32 capacity, u32 length, bool required) {
    if (length > capacity || (required && length == 0)) return false;
    for (u32 i = 0; i < length; i++) {
        const u8 value = static_cast<u8>(text[i]);
        if (value < SUSAMUNE_GHOST_TEXT_MIN ||
            value > SUSAMUNE_GHOST_TEXT_MAX || value == '/' || value == '\\') {
            return false;
        }
    }
    for (u32 i = length; i < capacity; i++) {
        if (text[i] != 0) return false;
    }
    return true;
}

#if defined(SUSAMUNE_VERSION_JP)
const u32 kGameId = SUSAMUNE_GHOST_GAME_ID_JP;
const u8 kRegion = SUSAMUNE_GHOST_REGION_JP;
#elif defined(SUSAMUNE_VERSION_US)
const u32 kGameId = SUSAMUNE_GHOST_GAME_ID_US;
const u8 kRegion = SUSAMUNE_GHOST_REGION_US;
#elif defined(SUSAMUNE_VERSION_PAL)
const u32 kGameId = SUSAMUNE_GHOST_GAME_ID_PAL;
const u8 kRegion = SUSAMUNE_GHOST_REGION_PAL;
#else
#error "Unknown game version"
#endif

bool gameRegionPairIsValid(u32 gameId, u8 region) {
    return (gameId == SUSAMUNE_GHOST_GAME_ID_JP &&
            region == SUSAMUNE_GHOST_REGION_JP) ||
           (gameId == SUSAMUNE_GHOST_GAME_ID_US &&
            region == SUSAMUNE_GHOST_REGION_US) ||
           (gameId == SUSAMUNE_GHOST_GAME_ID_PAL &&
            region == SUSAMUNE_GHOST_REGION_PAL);
}

bool portableRouteIsValid(u8 area, u8 episode, u8 parentArea,
                          u8 routeFlags, s32 routeVariant) {
    u8 expectedParent;
    switch (area) {
#define PORTABLE_ROUTE_CASE(routeArea, parent) \
    case routeArea: expectedParent = parent; break;
        SUSAMUNE_GHOST_PORTABLE_ROUTE_LIST(PORTABLE_ROUTE_CASE)
#undef PORTABLE_ROUTE_CASE
    default: return false;
    }
    if (episode > SUSAMUNE_GHOST_ROUTE_EPISODE_MAX ||
        routeVariant < SUSAMUNE_GHOST_ROUTE_VARIANT_NONE ||
        routeVariant > SUSAMUNE_GHOST_ROUTE_VARIANT_MAX ||
        parentArea != expectedParent) {
        return false;
    }
    if (expectedParent == SUSAMUNE_GHOST_ROUTE_PARENT_NONE) {
        return routeFlags == 0;
    }
    return (routeFlags & SUSAMUNE_GHOST_ROUTE_INTERNAL_SCENE) != 0 &&
           (routeFlags & ~(SUSAMUNE_GHOST_ROUTE_INTERNAL_SCENE |
                           SUSAMUNE_GHOST_ROUTE_PARENT_START)) == 0;
}

void makeUnsafe(SusamuneGhostSlotInfo *out, u32 generation) {
    memset(out, 0, sizeof(*out));
    out->generation = generation;
    out->flags = SUSAMUNE_GHOST_SLOT_UNSAFE;
    out->status = SUSAMUNE_GHOST_STATUS_INVALID_FILE;
}

bool sanitizeSlot(const SusamuneGhostSlotInfo &raw,
                  SusamuneGhostSlotInfo *out, bool imported) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    const u16 knownFlags = SUSAMUNE_GHOST_SLOT_PRESENT |
        SUSAMUNE_GHOST_SLOT_UNSAFE | SUSAMUNE_GHOST_SLOT_IMPORTED;
    if ((raw.flags & ~knownFlags) != 0 || raw.reserved[0] != 0) {
        makeUnsafe(out, raw.generation);
        return false;
    }
    if ((raw.flags & SUSAMUNE_GHOST_SLOT_UNSAFE) != 0) {
        if (imported &&
            (raw.flags & (SUSAMUNE_GHOST_SLOT_PRESENT |
                          SUSAMUNE_GHOST_SLOT_IMPORTED)) !=
                (SUSAMUNE_GHOST_SLOT_PRESENT |
                 SUSAMUNE_GHOST_SLOT_IMPORTED)) {
            makeUnsafe(out, raw.generation);
            return false;
        }
        makeUnsafe(out, raw.generation);
        if (imported) {
            out->flags |= SUSAMUNE_GHOST_SLOT_PRESENT |
                          SUSAMUNE_GHOST_SLOT_IMPORTED;
        }
        out->status = SUSAMUNE_GHOST_STATUS_SLOT_UNSAFE;
        return true;
    }
    if ((raw.flags & SUSAMUNE_GHOST_SLOT_PRESENT) == 0) {
        return (raw.flags & SUSAMUNE_GHOST_SLOT_IMPORTED) == 0;
    }

    const bool hasParent =
        raw.routeParentArea != SUSAMUNE_GHOST_ROUTE_PARENT_NONE;
    const bool internal =
        (raw.routeFlags & SUSAMUNE_GHOST_ROUTE_INTERNAL_SCENE) != 0;
    const bool parentStart =
        (raw.routeFlags & SUSAMUNE_GHOST_ROUTE_PARENT_START) != 0;
    const u32 expectedCanonicalSize = SUSAMUNE_GHOST_V4_SAMPLE_DATA_OFFSET +
        raw.sampleCount * SUSAMUNE_GHOST_POSE_SAMPLE_SIZE;
    const bool canonicalV3 =
        raw.canonicalVersion == SUSAMUNE_GHOST_FILE_VERSION_V3 &&
        raw.requiredFeatures ==
            SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V3 &&
        raw.sampleCodec == SUSAMUNE_GHOST_CODEC_RAW;
    const bool canonicalV4 =
        raw.canonicalVersion == SUSAMUNE_GHOST_FILE_VERSION_V4 &&
        raw.requiredFeatures ==
            SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V4 &&
        raw.sampleCodec == SUSAMUNE_GHOST_CODEC_POSE_ATTACHMENTS;
    const bool foreign = raw.gameId != kGameId;
    const bool namespaceSane = imported
        ? (raw.flags & SUSAMUNE_GHOST_SLOT_IMPORTED) != 0 &&
              gameRegionPairIsValid(raw.gameId, raw.region) &&
              (!foreign || portableRouteIsValid(
                  raw.routeArea, raw.routeEpisode, raw.routeParentArea,
                  raw.routeFlags, raw.routeVariant))
        : (raw.flags & SUSAMUNE_GHOST_SLOT_IMPORTED) == 0 &&
              raw.gameId == kGameId && raw.region == kRegion;
    const bool sane = raw.generation != 0 && raw.status == 0 &&
        namespaceSane &&
        raw.discRevision == SUSAMUNE_GHOST_DISC_REVISION &&
        (canonicalV3 || canonicalV4) &&
        raw.recordingMode == SUSAMUNE_GHOST_RECORDING_POSE_QF &&
        raw.sampleIntervalQf == SUSAMUNE_GHOST_TRANSFORM_INTERVAL_QF &&
        raw.sampleCount >= SUSAMUNE_GHOST_MIN_SAMPLE_COUNT &&
        raw.sampleCount <= SUSAMUNE_GHOST_MAX_SAMPLE_COUNT &&
        raw.durationQf > 0 &&
        raw.durationQf <= SUSAMUNE_GHOST_MAX_DURATION_QF &&
        raw.payloadSize == expectedCanonicalSize &&
        raw.payloadSize <= SUSAMUNE_GHOST_MAX_FILE_SIZE &&
        (raw.resultQf == SUSAMUNE_GHOST_RESULT_QF_NONE ||
         raw.resultQf <= SUSAMUNE_GHOST_QF_MAX) &&
        raw.routeArea <= SUSAMUNE_GHOST_ROUTE_AREA_MAX &&
        raw.routeEpisode <= SUSAMUNE_GHOST_ROUTE_EPISODE_MAX &&
        (!hasParent ||
         raw.routeParentArea <= SUSAMUNE_GHOST_ROUTE_AREA_MAX) &&
        (raw.routeFlags & ~SUSAMUNE_GHOST_ROUTE_FLAGS_V1) == 0 &&
        internal == hasParent && (!parentStart || hasParent) &&
        raw.routeVariant >= SUSAMUNE_GHOST_ROUTE_VARIANT_NONE &&
        raw.routeVariant <= SUSAMUNE_GHOST_ROUTE_VARIANT_MAX &&
        safeText(raw.author, sizeof(raw.author), raw.authorLength, false) &&
        safeText(raw.name, sizeof(raw.name), raw.nameLength, true);
    if (!sane) {
        makeUnsafe(out, raw.generation);
        return false;
    }
    memcpy(out, &raw, sizeof(*out));
    return true;
}

bool adoptCatalog(const SusamuneGhostStorageResponse &response) {
#if IS_EMULATOR
    (void)response;
    return false;
#else
    volatile SusamuneGhostStorageMailbox *mailbox =
        SUSAMUNE_GHOST_STORAGE_PPC_PTR;
    const bool imported = response.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE;
    const u32 capacity = imported ? SUSAMUNE_GHOST_IMPORTED_MAX_ENTRIES
                                  : SUSAMUNE_GHOST_SLOT_COUNT;
    const u32 catalogBytes = imported ? kImportedCatalogBytes : kCatalogBytes;
    const u32 durationLimit = imported
        ? SUSAMUNE_GHOST_IMPORTED_MAX_DURATION_QF
        : SUSAMUNE_GHOST_PROFILE_MAX_DURATION_QF;
    SusamuneGhostSlotInfo *catalog = imported
        ? sImportedCatalog : sCatalog;
    DCInvalidateRange((void *)mailbox->payload, catalogBytes);

    if (imported) {
        // Never carry a lexical row identity across catalog adoption.
        detachImportedAssociation(false);
        clearImportedCatalog();
    }
    else clearCatalog();
    u32 duration = 0;
    u16 count = 0;
    for (u32 i = 0; i < capacity; i++) {
        SusamuneGhostSlotInfo raw;
        memcpy(&raw, (const void *)(mailbox->payload +
                   i * sizeof(SusamuneGhostSlotInfo)), sizeof(raw));
        if (!imported &&
            i >= SUSAMUNE_GHOST_PROFILE_WRITABLE_ENTRIES) {
            const u8 *bytes = reinterpret_cast<const u8 *>(&raw);
            for (u32 j = 0; j < sizeof(raw); j++) {
                if (bytes[j] != 0) {
                    clearCatalog();
                    return false;
                }
            }
            continue;
        }
        sanitizeSlot(raw, &catalog[i], imported);
        if ((catalog[i].flags & SUSAMUNE_GHOST_SLOT_PRESENT) == 0) continue;
        if (catalog[i].durationQf > durationLimit - duration) {
            if (imported) clearImportedCatalog();
            else clearCatalog();
            return false;
        }
        duration += catalog[i].durationQf;
        count++;
    }
    if (response.payloadSize != catalogBytes ||
        response.slotCount != count || response.totalDurationQf != duration ||
        duration > durationLimit) {
        if (imported) clearImportedCatalog();
        else clearCatalog();
        return false;
    }
    if (imported) {
        sImportedTotalDuration = duration;
        sImportedOverflow = response.generation;
        sImportedCatalogReady = true;
    } else {
        if (response.generation != 0) {
            clearCatalog();
            return false;
        }
        sTotalDuration = duration;
        sCatalogReady = true;
    }
    return true;
#endif
}

bool responseShapeIsValid(const SusamuneGhostStorageResponse &response,
                          u16 command) {
    const bool imported = sPendingProfile == SUSAMUNE_GHOST_IMPORTED_PROFILE;
    const u16 slotLimit = imported ? SUSAMUNE_GHOST_IMPORTED_MAX_ENTRIES
        : SUSAMUNE_GHOST_PROFILE_WRITABLE_ENTRIES;
    const u32 durationLimit = imported
        ? SUSAMUNE_GHOST_IMPORTED_MAX_DURATION_QF
        : SUSAMUNE_GHOST_PROFILE_MAX_DURATION_QF;
    if (response.profile != sPendingProfile ||
        (response.flags & ~(SUSAMUNE_GHOST_RESPONSE_READY |
                            SUSAMUNE_GHOST_RESPONSE_BUSY)) != 0 ||
        (response.flags & SUSAMUNE_GHOST_RESPONSE_BUSY) != 0 ||
        response.slotCount > slotLimit ||
        response.totalDurationQf > durationLimit) {
        return false;
    }
    if (response.status == SUSAMUNE_GHOST_STATUS_OK &&
        (response.flags & SUSAMUNE_GHOST_RESPONSE_READY) == 0) {
        return false;
    }
    if (response.status != SUSAMUNE_GHOST_STATUS_OK)
        return response.payloadSize == 0;
    if (command == SUSAMUNE_GHOST_CMD_LIST ||
        command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN) {
        return response.payloadSize ==
            (imported ? kImportedCatalogBytes : kCatalogBytes);
    }
    if (command == SUSAMUNE_GHOST_CMD_LOAD)
        return response.payloadSize >= SUSAMUNE_GHOST_FILE_HEADER_SIZE &&
               response.payloadSize <= SUSAMUNE_GHOST_MAX_FILE_SIZE;
    return response.payloadSize == 0;
}

#if !IS_EMULATOR
void beginRequest(u16 command, u16 profile, u16 slot, u32 payloadSize,
                  u32 recordToken, const char *status,
                  u32 durationQf = 0) {
    volatile SusamuneGhostStorageMailbox *mailbox =
        SUSAMUNE_GHOST_STORAGE_PPC_PTR;
    sSequence++;
    if (sSequence == 0) sSequence++;

    mailbox->request.requestMagic = SUSAMUNE_GHOST_STORAGE_MAGIC;
    mailbox->request.protocolVersion = SUSAMUNE_GHOST_STORAGE_VERSION;
    mailbox->request.command = command;
    mailbox->request.requestSeq = sSequence;
    mailbox->request.profile = profile;
    mailbox->request.slot = slot;
    mailbox->request.payloadSize = payloadSize;
    mailbox->request.flags = 0;
    mailbox->request.reserved[0] = 0;
    mailbox->request.reserved[1] = 0;
    DCFlushRange((void *)&mailbox->request, sizeof(mailbox->request));

    sPendingCommand = command;
    sPendingSequence = sSequence;
    sPendingRecordToken = command == SUSAMUNE_GHOST_CMD_SAVE
        ? recordToken : 0;
    sPendingDurationQf = command == SUSAMUNE_GHOST_CMD_SAVE
        ? durationQf : 0;
    sPendingProfile = profile;
    sPendingSlot = slot;
    sPendingEpoch = sEpoch;
    sWaitFrames = 0;
    sTimedOut = false;
    sStatus = status;
}
#endif

bool beginRefresh() {
    if (!sAvailable || sPendingCommand != SUSAMUNE_GHOST_CMD_NONE) return false;
    clearCatalog();
#if !IS_EMULATOR
    beginRequest(SUSAMUNE_GHOST_CMD_LIST, sProfile, 0, 0, 0, kRefreshing);
    return true;
#else
    return false;
#endif
}

bool beginImportedRefresh(u16 command) {
    if (!sAvailable || sPendingCommand != SUSAMUNE_GHOST_CMD_NONE) return false;
    clearImportedCatalog();
#if !IS_EMULATOR
    if (command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN)
        detachImportedAssociation(false);
    beginRequest(command, SUSAMUNE_GHOST_IMPORTED_PROFILE, 0, 0, 0,
                 command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN
                     ? kImportingShare : kRefreshing);
    return true;
#else
    (void)command;
    return false;
#endif
}

void queueRefresh() {
    clearCatalog();
    sRefreshQueued = sAvailable;
}

void queueImportedRefresh() {
    clearImportedCatalog();
    sImportedRefreshQueued = sAvailable;
}

void queueNamespaceRefresh(u16 profile) {
    if (profile == SUSAMUNE_GHOST_IMPORTED_PROFILE) queueImportedRefresh();
    else queueRefresh();
}

void clearNamespaceCatalog(u16 profile) {
    if (profile == SUSAMUNE_GHOST_IMPORTED_PROFILE) clearImportedCatalog();
    else clearCatalog();
}

bool observerLoadDestination(u8 destination) {
    return destination == LOAD_DESTINATION_OBSERVER_PRIMARY ||
           destination == LOAD_DESTINATION_OBSERVER_SECONDARY;
}

void cancelFailedObserverLoad(u8 destination) {
    if (observerLoadDestination(destination)) Ghost::stopObserver();
}

void completeRequest(const SusamuneGhostStorageResponse &response) {
    const u16 command = sPendingCommand;
    const u16 requestProfile = sPendingProfile;
    const u16 requestSlot = sPendingSlot;
    const u32 requestEpoch = sPendingEpoch;
    const u32 recordToken = sPendingRecordToken;
    const u32 durationQf = sPendingDurationQf;
    const u32 expectedGeneration = sPendingExpectedGeneration;
    const u8 loadDestination = sPendingLoadDestination;
    sPendingCommand = SUSAMUNE_GHOST_CMD_NONE;
    sPendingRecordToken = 0;
    sPendingDurationQf = 0;
    sPendingExpectedGeneration = 0;
    sPendingLoadDestination = LOAD_DESTINATION_RACE;
    sWaitFrames = 0;
    sTimedOut = false;
    sAvailable = (response.flags & SUSAMUNE_GHOST_RESPONSE_READY) != 0;

    if (!responseShapeIsValid(response, command)) {
        sAvailable = false;
        cancelFailedObserverLoad(loadDestination);
        notify(kProtocol);
        if (command == SUSAMUNE_GHOST_CMD_SAVE ||
            command == SUSAMUNE_GHOST_CMD_DELETE ||
            command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN ||
            command == SUSAMUNE_GHOST_CMD_LOAD ||
            command == SUSAMUNE_GHOST_CMD_EXPORT) {
            clearNamespaceCatalog(requestProfile);
        }
        return;
    }
    if (response.status != SUSAMUNE_GHOST_STATUS_OK) {
        cancelFailedObserverLoad(loadDestination);
        const char *status = command == SUSAMUNE_GHOST_CMD_EXPORT &&
                                     response.status ==
                                         SUSAMUNE_GHOST_STATUS_SLOT_OCCUPIED
            ? kExportExists : statusForCode(response.status);
        if (command == SUSAMUNE_GHOST_CMD_LIST) sStatus = status;
        else notify(status);
        if (command == SUSAMUNE_GHOST_CMD_SAVE ||
            command == SUSAMUNE_GHOST_CMD_DELETE ||
            command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN ||
            command == SUSAMUNE_GHOST_CMD_LOAD ||
            command == SUSAMUNE_GHOST_CMD_EXPORT) {
            queueNamespaceRefresh(requestProfile);
        }
        return;
    }

    if (command == SUSAMUNE_GHOST_CMD_LIST ||
        command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN) {
        const bool stalePersonal =
            requestProfile != SUSAMUNE_GHOST_IMPORTED_PROFILE &&
            requestProfile != sProfile;
        if (stalePersonal || !adoptCatalog(response)) {
            sStatus = stalePersonal ? kReady : kCatalogInvalid;
            if (stalePersonal) sRefreshQueued = sAvailable;
        } else {
            sStatus = requestProfile == SUSAMUNE_GHOST_IMPORTED_PROFILE &&
                              sImportedOverflow != 0
                ? kImportedOverflow
                : command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN
                    ? kImportedShare : kReady;
            if (command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN) notify(sStatus);
        }
        return;
    }
    if (command == SUSAMUNE_GHOST_CMD_SAVE) {
        Records::onGhostSaved(durationQf);
        RecordsPersistence::checkpoint();
        Ghost::releaseSavedRecording(recordToken);
        notify(kSaved);
        queueRefresh();
        return;
    }
    if (command == SUSAMUNE_GHOST_CMD_DELETE) {
        if (requestProfile == sLoadedProfile &&
            requestSlot == static_cast<u16>(sLoadedSlot)) {
            if (Ghost::playbackPinned()) Ghost::clearPlayback();
            sLoadedSlot = -1;
            sLoadedProfile = 0xff;
        }
        notify(kDeleted);
        queueNamespaceRefresh(requestProfile);
        return;
    }
    if (command == SUSAMUNE_GHOST_CMD_EXPORT) {
        notify(kExportedShare);
        queueRefresh();
        return;
    }
    if (command == SUSAMUNE_GHOST_CMD_LOAD) {
        if (requestEpoch != sEpoch ||
            (requestProfile != SUSAMUNE_GHOST_IMPORTED_PROFILE &&
             requestProfile != sProfile) ||
            (requestProfile != SUSAMUNE_GHOST_IMPORTED_PROFILE &&
             expectedGeneration != 0 &&
             response.generation != expectedGeneration)) {
            cancelFailedObserverLoad(loadDestination);
            notify(kLoadIgnored);
            queueNamespaceRefresh(requestProfile);
            return;
        }
#if !IS_EMULATOR
        volatile SusamuneGhostStorageMailbox *mailbox =
            SUSAMUNE_GHOST_STORAGE_PPC_PTR;
        DCInvalidateRange((void *)mailbox->payload, response.payloadSize);
        const bool imported = loadDestination == LOAD_DESTINATION_RACE
            ? Ghost::importPlayback((const void *)mailbox->payload,
                                    response.payloadSize)
            : Ghost::importObserverTrack(
                  (const void *)mailbox->payload, response.payloadSize,
                  loadDestination == LOAD_DESTINATION_OBSERVER_SECONDARY);
        if (!imported) {
            cancelFailedObserverLoad(loadDestination);
            notify(kImportFailed);
            queueNamespaceRefresh(requestProfile);
            return;
        }
#endif
        if (observerLoadDestination(loadDestination)) {
            notify(kLoaded);
            return;
        }
        sLoadedProfile = static_cast<u8>(requestProfile);
        sLoadedSlot = static_cast<s8>(requestSlot);
        notify(kRaceLoaded);
        if (requestProfile != SUSAMUNE_GHOST_IMPORTED_PROFILE) queueRefresh();
    }
}

#if !IS_EMULATOR
void pollResponse() {
    volatile SusamuneGhostStorageMailbox *mailbox =
        SUSAMUNE_GHOST_STORAGE_PPC_PTR;
    DCInvalidateRange((void *)&mailbox->response, sizeof(mailbox->response));
    SusamuneGhostStorageResponse response;
    memcpy(&response, (const void *)&mailbox->response, sizeof(response));

    if (response.responseMagic == SUSAMUNE_GHOST_STORAGE_MAGIC &&
        response.protocolVersion == SUSAMUNE_GHOST_STORAGE_VERSION &&
        response.ackSeq == sPendingSequence) {
        completeRequest(response);
        return;
    }
    if (sWaitFrames != 0xffffffffu) sWaitFrames++;
    if (!sTimedOut && sWaitFrames > kTimeoutFrames) {
        sTimedOut = true;
        cancelFailedObserverLoad(sPendingLoadDestination);
        notify(kTimeout);
    }
}
#endif

__attribute__((noinline)) bool requestIdle() {
    if (!sAvailable) {
        sStatus = IS_EMULATOR ? kDolphinUnavailable : kUnavailable;
        return false;
    }
    if (sPendingCommand != SUSAMUNE_GHOST_CMD_NONE) {
        sStatus = sTimedOut ? kTimeout : kBusy;
        return false;
    }
    return true;
}

bool requestAllowed(u16 profile, int slot, bool requirePresent,
                    bool allowUnsafe = false) {
    observeProfile();
    if (!requestIdle()) return false;
    const bool imported = profile == SUSAMUNE_GHOST_IMPORTED_PROFILE;
    const int slotLimit = imported
        ? static_cast<int>(SUSAMUNE_GHOST_IMPORTED_MAX_ENTRIES)
        : static_cast<int>(SUSAMUNE_GHOST_PROFILE_WRITABLE_ENTRIES);
    if (slot < 0 || slot >= slotLimit) {
        sStatus = kBadSlot;
        return false;
    }
    if (imported ? !sImportedCatalogReady : !sCatalogReady) {
        sStatus = kCatalogNeeded;
        return false;
    }
    const SusamuneGhostSlotInfo &info = imported
        ? sImportedCatalog[slot] : sCatalog[slot];
    if ((info.flags & SUSAMUNE_GHOST_SLOT_UNSAFE) != 0 && !allowUnsafe) {
        sStatus = kUnsafe;
        return false;
    }
    const bool present = (info.flags & SUSAMUNE_GHOST_SLOT_PRESENT) != 0;
    if (requirePresent && !present) {
        sStatus = kEmpty;
        return false;
    }
    if (!requirePresent && present) {
        sStatus = kOccupied;
        return false;
    }
    return true;
}

__attribute__((noinline)) bool requestImportedRefresh(u16 command) {
    if (!requestIdle()) return false;
    sImportedRefreshQueued = false;
    return beginImportedRefresh(command);
}

__attribute__((noinline)) bool loadTrack(bool imported, int slot,
                                         u8 destination) {
    if (!imported) observeProfile();
    const u16 profile = imported ? SUSAMUNE_GHOST_IMPORTED_PROFILE : sProfile;
    if (!requestAllowed(profile, slot, true)) return false;
#if !IS_EMULATOR
    sPendingLoadDestination = destination;
    beginRequest(SUSAMUNE_GHOST_CMD_LOAD, profile,
                 static_cast<u16>(slot), 0, 0, kLoading);
    sPendingExpectedGeneration =
        imported ? 0 : sCatalog[slot].generation;
    return true;
#else
    (void)destination;
    return false;
#endif
}

__attribute__((noinline)) bool requestPersonalCommand(int slot, u16 command,
                                                       const char *status) {
    if (!requestAllowed(sProfile, slot, true)) return false;
#if !IS_EMULATOR
    beginRequest(command, sProfile, static_cast<u16>(slot), 0, 0, status);
    return true;
#else
    (void)command;
    (void)status;
    return false;
#endif
}

__attribute__((noinline)) const SusamuneGhostSlotInfo *catalogEntry(
    bool imported, int slot) {
    if (slot < 0 ||
        slot >= (imported
                     ? static_cast<int>(SUSAMUNE_GHOST_IMPORTED_MAX_ENTRIES)
                     : static_cast<int>(
                           SUSAMUNE_GHOST_PROFILE_WRITABLE_ENTRIES)) ||
        (imported ? !sImportedCatalogReady : !sCatalogReady)) {
        return nullptr;
    }
    return imported ? &sImportedCatalog[slot] : &sCatalog[slot];
}

bool copyLiteral(char *out, u32 size, const char *text, u32 length) {
    if (!out || size == 0) return false;
    u32 count = length;
    if (count >= size) count = size - 1;
    if (count != 0) memcpy(out, text, count);
    out[count] = '\0';
    return true;
}

bool copyVisibleName(char *out, u32 size, const char *text, u32 length) {
    for (u32 i = 0; i < length; i++) {
        if (text[i] != ' ') return copyLiteral(out, size, text, length);
    }
    // Printable catalog validation admits spaces; never show a live row blank.
    return copyLiteral(out, size, kUnnamedName, sizeof(kUnnamedName) - 1);
}

__attribute__((noinline)) bool copyCatalogName(bool imported, int slot,
                                                char *out, u32 size) {
    if (!out || size == 0) return false;
    out[0] = '\0';
    const SusamuneGhostSlotInfo *info = catalogEntry(imported, slot);
    if (!info) return false;
    if ((info->flags & SUSAMUNE_GHOST_SLOT_UNSAFE) != 0)
        return copyLiteral(out, size, kUnsafeName, sizeof(kUnsafeName) - 1);
    if ((info->flags & SUSAMUNE_GHOST_SLOT_PRESENT) == 0)
        return copyLiteral(out, size, kEmptyName, sizeof(kEmptyName) - 1);
    return copyVisibleName(out, size, info->name, info->nameLength);
}

}  // namespace

void init() {
    memset(sCatalog, 0, kCatalogBytes);
    memset(sImportedCatalog, 0, kImportedCatalogBytes);
    sSequence = 0;
    sPendingSequence = 0;
    sPendingRecordToken = 0;
    sPendingDurationQf = 0;
    sPendingExpectedGeneration = 0;
    sWaitFrames = 0;
    sEpoch = 1;
    sPendingEpoch = 0;
    sTotalDuration = 0;
    sImportedTotalDuration = 0;
    sImportedOverflow = 0;
    sPendingCommand = SUSAMUNE_GHOST_CMD_NONE;
    sPendingProfile = 0;
    sPendingSlot = 0;
    sPendingLoadDestination = LOAD_DESTINATION_RACE;
    sProfile = activeProfile();
    sLoadedSlot = -1;
    sLoadedProfile = 0xff;
    sAvailable = false;
    sCatalogReady = false;
    sImportedCatalogReady = false;
    sRefreshQueued = false;
    sImportedRefreshQueued = false;
    sTimedOut = false;
    sStatus = IS_EMULATOR ? kDolphinUnavailable : kUnavailable;

#if !IS_EMULATOR
    volatile SusamuneGhostStorageMailbox *mailbox =
        SUSAMUNE_GHOST_STORAGE_PPC_PTR;
    DCInvalidateRange((void *)&mailbox->response, sizeof(mailbox->response));
    SusamuneGhostStorageResponse response;
    memcpy(&response, (const void *)&mailbox->response, sizeof(response));
    if (response.responseMagic == SUSAMUNE_GHOST_STORAGE_MAGIC &&
        response.protocolVersion == SUSAMUNE_GHOST_STORAGE_VERSION &&
        (response.flags & ~(SUSAMUNE_GHOST_RESPONSE_READY |
                            SUSAMUNE_GHOST_RESPONSE_BUSY)) == 0 &&
        (response.flags & SUSAMUNE_GHOST_RESPONSE_BUSY) == 0) {
        sSequence = response.ackSeq;
        sAvailable =
            (response.flags & SUSAMUNE_GHOST_RESPONSE_READY) != 0;
        sStatus = sAvailable ? kReady : statusForCode(response.status);
        sRefreshQueued = sAvailable;
        sImportedRefreshQueued = sAvailable;
    }
#endif
}

void update() {
    observeProfile();
    observeLoadedPlayback();
    if (sPendingCommand != SUSAMUNE_GHOST_CMD_NONE) {
        if (sPendingCommand == SUSAMUNE_GHOST_CMD_LOAD &&
            observerLoadDestination(sPendingLoadDestination) &&
            !Ghost::observerPreparing() && sPendingEpoch == sEpoch) {
            sEpoch++;
            if (sEpoch == 0) sEpoch++;
        }
#if !IS_EMULATOR
        pollResponse();
#endif
        return;
    }
    if (sRefreshQueued) {
        sRefreshQueued = false;
        beginRefresh();
        return;
    }
    if (sImportedRefreshQueued) {
        sImportedRefreshQueued = false;
        beginImportedRefresh(SUSAMUNE_GHOST_CMD_LIST);
    }
}

void onSavestateLoaded() {
    sEpoch++;
    if (sEpoch == 0) sEpoch++;
    observeLoadedPlayback();
    if (sPendingCommand == SUSAMUNE_GHOST_CMD_LOAD) {
        cancelFailedObserverLoad(sPendingLoadDestination);
        sStatus = kLoadIgnored;
    }
}

bool refresh() {
    observeProfile();
    if (!requestIdle()) return false;
    sRefreshQueued = false;
    return beginRefresh();
}

bool refreshImported() {
    return requestImportedRefresh(SUSAMUNE_GHOST_CMD_LIST);
}

bool scanImports() {
    return requestImportedRefresh(SUSAMUNE_GHOST_CMD_IMPORT_SCAN);
}

bool save(int slot) { return save(slot, 0); }

bool save(int slot, u32 expectedSelectionToken) {
    if (slot < 0 ||
        slot >= static_cast<int>(SUSAMUNE_GHOST_PROFILE_WRITABLE_ENTRIES)) {
        sStatus = kBadSlot;
        return false;
    }
    if (!requestAllowed(sProfile, slot, false)) return false;
    if (!Ghost::hasSaveableTrack()) {
        sStatus = kNoRecording;
        return false;
    }
#if !IS_EMULATOR
    if (expectedSelectionToken != 0) {
        char currentName[SUSAMUNE_GHOST_NAME_SIZE];
        u32 currentSelectionToken = 0;
        if (!Ghost::copySaveableName(currentName, sizeof(currentName),
                                     &currentSelectionToken) ||
            currentSelectionToken != expectedSelectionToken) {
            sStatus = kSaveChanged;
            return false;
        }
    }
    volatile SusamuneGhostStorageMailbox *mailbox =
        SUSAMUNE_GHOST_STORAGE_PPC_PTR;
    u32 size = 0;
    u32 recordToken = 0;
    if (!Ghost::exportLatest((void *)mailbox->payload,
                             SUSAMUNE_GHOST_STORAGE_PAYLOAD_SIZE, sProfile,
                             ILing::pbProfileName(sProfile), &size,
                             &recordToken) ||
        size < SUSAMUNE_GHOST_FILE_HEADER_SIZE ||
        size > SUSAMUNE_GHOST_MAX_FILE_SIZE) {
        sStatus = kExportFailed;
        return false;
    }
    DCFlushRange((void *)mailbox->payload, size);
    const SusamuneGhostFileHeader *header =
        (const SusamuneGhostFileHeader *)(const void *)mailbox->payload;
    beginRequest(SUSAMUNE_GHOST_CMD_SAVE, sProfile,
                 static_cast<u16>(slot), size, recordToken, kSaving,
                 header->durationQf);
    return true;
#else
    (void)slot;
    (void)expectedSelectionToken;
    return false;
#endif
}

bool load(int slot) {
    return loadTrack(false, slot, LOAD_DESTINATION_RACE);
}

bool loadObserver(int slot, bool secondary) {
    return loadTrack(false, slot,
                     secondary ? LOAD_DESTINATION_OBSERVER_SECONDARY
                               : LOAD_DESTINATION_OBSERVER_PRIMARY);
}

bool remove(int slot) {
    return requestPersonalCommand(slot, SUSAMUNE_GHOST_CMD_DELETE, kDeleting);
}

bool exportShare(int slot) {
    return requestPersonalCommand(slot, SUSAMUNE_GHOST_CMD_EXPORT,
                                  kExportingShare);
}

bool importShare(int slot) {
    (void)slot;
    return scanImports();
}

bool loadImported(int slot) {
    return loadTrack(true, slot,
                     LOAD_DESTINATION_RACE);
}

bool loadImportedObserver(int slot, bool secondary) {
    return loadTrack(true, slot,
                     secondary ? LOAD_DESTINATION_OBSERVER_SECONDARY
                               : LOAD_DESTINATION_OBSERVER_PRIMARY);
}

bool removeImported(int slot) {
    if (!requestAllowed(SUSAMUNE_GHOST_IMPORTED_PROFILE, slot, true, true))
        return false;
#if !IS_EMULATOR
    const bool removesLoaded = sLoadedProfile ==
            SUSAMUNE_GHOST_IMPORTED_PROFILE &&
        sLoadedSlot == slot;
    beginRequest(SUSAMUNE_GHOST_CMD_DELETE,
                 SUSAMUNE_GHOST_IMPORTED_PROFILE,
                 static_cast<u16>(slot), 0, 0, kDeleting);
    // Deleting any leaf can renumber every later imported row.
    detachImportedAssociation(removesLoaded);
    return true;
#else
    (void)slot;
    return false;
#endif
}

bool busy() { return sPendingCommand != SUSAMUNE_GHOST_CMD_NONE; }

bool timedOut() {
    return sPendingCommand != SUSAMUNE_GHOST_CMD_NONE && sTimedOut;
}

bool available() { return sAvailable; }

bool catalogReady() { return sCatalogReady; }

bool importedCatalogReady() { return sImportedCatalogReady; }

int profile() { return sProfile; }

int loadedSlot() {
    return sLoadedProfile == sProfile && Ghost::playbackPinned()
        ? static_cast<int>(sLoadedSlot) : -1;
}

bool loadedImported() {
    return sLoadedProfile == SUSAMUNE_GHOST_IMPORTED_PROFILE &&
           sLoadedSlot >= 0 && Ghost::playbackPinned();
}

int loadedImportedSlot() {
    return loadedImported() ? static_cast<int>(sLoadedSlot) : -1;
}

u32 totalDurationQf() { return sTotalDuration; }

u32 importedTotalDurationQf() { return sImportedTotalDuration; }

u32 importedOverflowCount() { return sImportedOverflow; }

const char *statusText() { return sStatus; }

bool copySlotName(int slot, char *out, u32 size) {
    return copyCatalogName(false, slot, out, size);
}

bool copyImportedSlotName(int slot, char *out, u32 size) {
    return copyCatalogName(true, slot, out, size);
}

const SusamuneGhostSlotInfo *slot(int slot) {
    return catalogEntry(false, slot);
}

const SusamuneGhostSlotInfo *importedSlot(int slot) {
    return catalogEntry(true, slot);
}

}  // namespace GhostStorage
