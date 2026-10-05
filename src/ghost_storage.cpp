#include "susamune/ghost_storage.hxx"

#include "Dolphin/OS.h"
#include "Dolphin/mem.h"
#include "susamune/ghost.hxx"
#include "susamune/crash_report.hxx"
#include "susamune/iling.hxx"
#include "susamune/menu.hxx"
#include "susamune/records.hxx"
#include "susamune/records_persistence.hxx"

namespace GhostStorage {
namespace {

const u32 kTimeoutFrames = 30u * 60u;
const u32 kCatalogBytes = sizeof(SusamuneGhostCatalogPage);

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
const char kQuota[] = "Ghost storage is full";
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

SusamuneGhostCatalogPage *const sPages =
    reinterpret_cast<SusamuneGhostCatalogPage *>(
        SUSAMUNE_GHOST_CATALOG_CACHE_PPC_BASE);
static_assert(2 * kCatalogBytes <= SUSAMUNE_GHOST_CATALOG_CACHE_SIZE,
              "ghost pages exceed the fixed cache");
const char *sStatus = kUnavailable;
u32 sSequence, sPendingSequence, sPendingRecordToken, sPendingDurationQf;
u32 sWaitFrames, sEpoch, sPendingEpoch, sPendingSlot;
u32 sOffsets[2];
u16 sPendingCommand, sPendingProfile;
u8 sPendingLoadDestination, sProfile;
Identity sPendingIdentity, sLoadedIdentity;
bool sLoadedValid, sAvailable, sReady[2], sRefreshQueued[2], sCachedRefresh[2];
bool sImportScanQueued, sTimedOut;

void notify(const char *status) {
    sStatus = status;
    if (gMenu) gMenu->toast(status);
}

void observeLoadedPlayback() {
    if (!Ghost::playbackPinned()) sLoadedValid = false;
}

u8 activeProfile() {
    const int profile = ILing::pbProfile();
    return profile >= 0 && profile < (int)SUSAMUNE_GHOST_PROFILE_COUNT
        ? static_cast<u8>(profile) : 0;
}

void clearCatalog(bool imported) {
    memset(&sPages[imported], 0, kCatalogBytes);
    sReady[imported] = false;
}

void observeProfile() {
    const u8 profile = activeProfile();
    if (profile == sProfile) return;
    if (sPendingCommand == SUSAMUNE_GHOST_CMD_LOAD &&
        sPendingProfile != SUSAMUNE_GHOST_IMPORTED_PROFILE &&
        sPendingLoadDestination != LOAD_DESTINATION_RACE) Ghost::stopObserver();
    sProfile = profile;
    sOffsets[0] = 0;
    clearCatalog(false);
    sRefreshQueued[0] = sAvailable;
    sCachedRefresh[0] = false;
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
    const bool canonicalTeaching =
        ((raw.canonicalVersion == SUSAMUNE_GHOST_FILE_VERSION_V5 &&
          raw.requiredFeatures == SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V5) ||
         (raw.canonicalVersion == SUSAMUNE_GHOST_FILE_VERSION_V6 &&
          raw.requiredFeatures == SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V6)) &&
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
    const bool sane = raw.status == 0 &&
        namespaceSane &&
        raw.discRevision == SUSAMUNE_GHOST_DISC_REVISION &&
        (canonicalV3 || canonicalV4 || canonicalTeaching) &&
        raw.recordingMode == SUSAMUNE_GHOST_RECORDING_POSE_QF &&
        raw.sampleIntervalQf == SUSAMUNE_GHOST_TRANSFORM_INTERVAL_QF &&
        raw.sampleCount >= SUSAMUNE_GHOST_MIN_SAMPLE_COUNT &&
        raw.sampleCount <= SusamuneGhostPoseLimit(raw.canonicalVersion) &&
        raw.durationQf > 0 &&
        raw.durationQf <= SusamuneGhostDurationLimit(raw.canonicalVersion) &&
        (canonicalTeaching ? raw.payloadSize >= expectedCanonicalSize +
                                          SUSAMUNE_GHOST_TEACHING_HEADER_SIZE
                     : raw.payloadSize == expectedCanonicalSize) &&
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

bool validLeaf(const char *leaf) {
    u32 length = 0;
    while (length < SUSAMUNE_GHOST_IMPORT_LEAF_SIZE && leaf[length]) {
        const u8 ch = static_cast<u8>(leaf[length]);
        if (ch < 0x20 || ch > 0x7e || ch == '/' || ch == '\\' ||
            ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
            ch == '<' || ch == '>' || ch == '|') return false;
        length++;
    }
    if (length <= 9 || length >= SUSAMUNE_GHOST_IMPORT_LEAF_SIZE) return false;
    const char suffix[] = ".smsghost";
    for (u32 i = 0; i < 9; i++) {
        u8 ch = static_cast<u8>(leaf[length - 9 + i]);
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        if (ch != suffix[i]) return false;
    }
    for (u32 i = length; i < SUSAMUNE_GHOST_IMPORT_LEAF_SIZE; i++)
        if (leaf[i]) return false;
    return true;
}

bool zeroBytes(const void *data, u32 size) {
    const u8 *bytes = static_cast<const u8 *>(data);
    for (u32 i = 0; i < size; i++) if (bytes[i]) return false;
    return true;
}

bool adoptCatalog(const SusamuneGhostStorageResponse &response) {
#if IS_EMULATOR
    (void)response;
    return false;
#else
    const bool imported = response.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE;
    SusamuneGhostCatalogPage *page = &sPages[imported];
    clearCatalog(imported);
    DCInvalidateRange((void *)SUSAMUNE_GHOST_STORAGE_DATA_PPC_PTR, kCatalogBytes);
    memcpy(page, (const void *)SUSAMUNE_GHOST_STORAGE_DATA_PPC_PTR, kCatalogBytes);
    const u64 totalDuration = (static_cast<u64>(page->totalDurationQfHi) << 32) |
                              page->totalDurationQfLo;
    const u32 remaining = page->first < page->totalCount
        ? page->totalCount - page->first : 0;
    const u32 expectedCount = remaining < SUSAMUNE_GHOST_CATALOG_PAGE_ENTRIES
        ? remaining : SUSAMUNE_GHOST_CATALOG_PAGE_ENTRIES;
    bool valid = response.payloadSize == kCatalogBytes &&
        page->magic == SUSAMUNE_GHOST_CATALOG_PAGE_MAGIC &&
        page->version == SUSAMUNE_GHOST_CATALOG_PAGE_VERSION &&
        page->first == sPendingSlot && page->flags == 0 &&
        page->count == expectedCount && response.slotCount == page->count &&
        totalDuration <= static_cast<u64>(page->totalCount) *
                         SUSAMUNE_GHOST_MAX_DURATION_QF;
    u64 pageDuration = 0;
    for (u32 i = 0; valid && i < SUSAMUNE_GHOST_CATALOG_PAGE_ENTRIES; i++) {
        SusamuneGhostCatalogEntry &entry = page->entries[i];
        if (i >= page->count) {
            valid = zeroBytes(&entry, sizeof(entry));
            continue;
        }
        SusamuneGhostSlotInfo raw = entry.info;
        valid = entry.id != SUSAMUNE_GHOST_SLOT_AUTO &&
            (imported ? validLeaf(entry.leaf) :
                        zeroBytes(entry.leaf, sizeof(entry.leaf))) &&
            sanitizeSlot(raw, &entry.info, imported) &&
            (entry.info.flags & (SUSAMUNE_GHOST_SLOT_PRESENT |
                                 SUSAMUNE_GHOST_SLOT_UNSAFE)) != 0;
        for (u32 previous = 0; valid && previous < i; previous++) {
            valid = imported
                ? memcmp(entry.leaf, page->entries[previous].leaf,
                         sizeof(entry.leaf)) != 0
                : entry.id != page->entries[previous].id;
        }
        pageDuration += entry.info.durationQf;
    }
    if (!valid || pageDuration > totalDuration) {
        clearCatalog(imported);
        return false;
    }
    sReady[imported] = true;
    // Deleting the final row of the final page should return to existing rows.
    if (!page->count && page->first && page->totalCount) {
        sOffsets[imported] = ((page->totalCount - 1) /
            SUSAMUNE_GHOST_CATALOG_PAGE_ENTRIES) * SUSAMUNE_GHOST_CATALOG_PAGE_ENTRIES;
        sRefreshQueued[imported] = sAvailable;
        sCachedRefresh[imported] = false;
    } else if (!page->totalCount && page->first) {
        sOffsets[imported] = 0;
        sRefreshQueued[imported] = sAvailable;
        sCachedRefresh[imported] = false;
    }
    return true;
#endif
}

bool responseShapeIsValid(const SusamuneGhostStorageResponse &response,
                          u16 command) {
    if (response.profile != sPendingProfile ||
        (response.flags & ~(SUSAMUNE_GHOST_RESPONSE_READY |
                            SUSAMUNE_GHOST_RESPONSE_BUSY)) != 0 ||
        (response.flags & SUSAMUNE_GHOST_RESPONSE_BUSY) != 0) return false;
    if (response.status != SUSAMUNE_GHOST_STATUS_OK)
        return response.payloadSize == 0 && response.slotCount == 0;
    if (!(response.flags & SUSAMUNE_GHOST_RESPONSE_READY)) return false;
    if (command == SUSAMUNE_GHOST_CMD_LIST ||
        command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN)
        return response.payloadSize == kCatalogBytes &&
               response.slotCount <= SUSAMUNE_GHOST_CATALOG_PAGE_ENTRIES &&
               response.slot == 0;
    if (response.slotCount != 0 ||
        response.slot == SUSAMUNE_GHOST_SLOT_AUTO) return false;
    if (command != SUSAMUNE_GHOST_CMD_SAVE && response.slot != sPendingSlot)
        return false;
    if (command == SUSAMUNE_GHOST_CMD_LOAD)
        return response.payloadSize >= SUSAMUNE_GHOST_FILE_HEADER_SIZE &&
               response.payloadSize <= SUSAMUNE_GHOST_MAX_FILE_SIZE;
    return response.payloadSize == 0;
}

#if !IS_EMULATOR
void beginRequest(u16 command, u16 profile, u32 slot, u32 payloadSize,
                  u32 recordToken, const char *status, u32 durationQf = 0,
                  u32 expectedGeneration = 0, u32 flags = 0) {
    volatile SusamuneGhostStorageMailbox *mailbox = SUSAMUNE_GHOST_STORAGE_PPC_PTR;
    if (++sSequence == 0) sSequence++;
    CrashReport::note(SUSAMUNE_CRASH_EVENT_STORAGE, command, slot);
    mailbox->request.requestMagic = SUSAMUNE_GHOST_STORAGE_MAGIC;
    mailbox->request.protocolVersion = SUSAMUNE_GHOST_STORAGE_VERSION;
    mailbox->request.command = command;
    mailbox->request.requestSeq = sSequence;
    mailbox->request.profile = profile;
    mailbox->request.reserved = 0;
    mailbox->request.slot = slot;
    mailbox->request.payloadSize = payloadSize;
    mailbox->request.flags = flags;
    mailbox->request.expectedGeneration = expectedGeneration;
    DCFlushRange((void *)&mailbox->request, sizeof(mailbox->request));
    sPendingCommand = command;
    sPendingSequence = sSequence;
    sPendingRecordToken = recordToken;
    sPendingDurationQf = durationQf;
    sPendingProfile = profile;
    sPendingSlot = slot;
    sPendingEpoch = sEpoch;
    sWaitFrames = 0;
    sTimedOut = false;
    sStatus = status;
}
#endif

bool beginRefresh(bool imported, u16 command = SUSAMUNE_GHOST_CMD_LIST,
                  bool cached = false) {
    if (!sAvailable || sPendingCommand != SUSAMUNE_GHOST_CMD_NONE) return false;
    clearCatalog(imported);
#if !IS_EMULATOR
    beginRequest(command, imported ? SUSAMUNE_GHOST_IMPORTED_PROFILE : sProfile,
                 sOffsets[imported], 0, 0,
                 command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN ? kImportingShare : kRefreshing,
                 0, 0, cached ? SUSAMUNE_GHOST_REQUEST_CACHED_LIST : 0);
    return true;
#else
    (void)command;
    (void)cached;
    return false;
#endif
}

__attribute__((noinline)) bool requestImportedRefresh(u16 command) {
    observeProfile();
    if (!sAvailable) {
        sStatus = IS_EMULATOR ? kDolphinUnavailable : kUnavailable;
        return false;
    }
    if (command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN) sOffsets[1] = 0;
    clearCatalog(true);
    sRefreshQueued[1] = true;
    sCachedRefresh[1] = false;
    sImportScanQueued = command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN;
    if (busy()) return true;
    sRefreshQueued[1] = false;
    sImportScanQueued = false;
    return beginRefresh(true, command);
}

void queueNamespaceRefresh(u16 profile) {
    const bool imported = profile == SUSAMUNE_GHOST_IMPORTED_PROFILE;
    if (!imported && profile != sProfile) return;
    clearCatalog(imported);
    sRefreshQueued[imported] = sAvailable;
    sCachedRefresh[imported] = false;
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
    const u32 requestSlot = sPendingSlot;
    const u32 requestEpoch = sPendingEpoch;
    const u32 recordToken = sPendingRecordToken;
    const u32 durationQf = sPendingDurationQf;
    const u8 loadDestination = sPendingLoadDestination;
    sPendingCommand = SUSAMUNE_GHOST_CMD_NONE;
    sPendingRecordToken = 0;
    sPendingDurationQf = 0;
    sPendingLoadDestination = LOAD_DESTINATION_RACE;
    sWaitFrames = 0;
    sTimedOut = false;
    sAvailable = (response.flags & SUSAMUNE_GHOST_RESPONSE_READY) != 0;

    if (!responseShapeIsValid(response, command)) {
        sAvailable = false;
        cancelFailedObserverLoad(loadDestination);
        notify(kProtocol);
        clearCatalog(requestProfile == SUSAMUNE_GHOST_IMPORTED_PROFILE);
        return;
    }
    if (response.status != SUSAMUNE_GHOST_STATUS_OK) {
        cancelFailedObserverLoad(loadDestination);
        const char *status = command == SUSAMUNE_GHOST_CMD_EXPORT &&
            response.status == SUSAMUNE_GHOST_STATUS_SLOT_OCCUPIED
                ? kExportExists : statusForCode(response.status);
        if (command == SUSAMUNE_GHOST_CMD_LIST) sStatus = status;
        else notify(status);
        if (command != SUSAMUNE_GHOST_CMD_LIST)
            queueNamespaceRefresh(requestProfile);
        return;
    }
    if (command == SUSAMUNE_GHOST_CMD_LIST ||
        command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN) {
        const bool imported = requestProfile == SUSAMUNE_GHOST_IMPORTED_PROFILE;
        const bool stale = (!imported && requestProfile != sProfile) ||
                           requestSlot != sOffsets[imported];
        if (stale) {
            sRefreshQueued[imported] = sAvailable;
            return;
        }
        sStatus = adoptCatalog(response)
            ? command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN ? kImportedShare : kReady
            : kCatalogInvalid;
        if (command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN) notify(sStatus);
        return;
    }
    if (command == SUSAMUNE_GHOST_CMD_SAVE) {
        Records::onGhostSaved(durationQf);
        RecordsPersistence::checkpoint();
        Ghost::releaseSavedRecording(recordToken);
        notify(kSaved);
        queueNamespaceRefresh(requestProfile);
        return;
    }
    if (command == SUSAMUNE_GHOST_CMD_DELETE) {
        if (sLoadedValid && sameIdentity(sLoadedIdentity, sPendingIdentity)) {
            if (Ghost::playbackPinned()) Ghost::clearPlayback();
            sLoadedValid = false;
        }
        notify(kDeleted);
        queueNamespaceRefresh(requestProfile);
        return;
    }
    if (command == SUSAMUNE_GHOST_CMD_EXPORT) {
        notify(kExportedShare);
        return;
    }
    if (command == SUSAMUNE_GHOST_CMD_LOAD) {
        if (requestEpoch != sEpoch || !identityValid(sPendingIdentity) ||
            response.generation != sPendingIdentity.generation) {
            cancelFailedObserverLoad(loadDestination);
            notify(kLoadIgnored);
            queueNamespaceRefresh(requestProfile);
            return;
        }
#if !IS_EMULATOR
        DCInvalidateRange((void *)SUSAMUNE_GHOST_STORAGE_DATA_PPC_PTR, response.payloadSize);
        const bool accepted = loadDestination == LOAD_DESTINATION_RACE
            ? Ghost::importPlayback((const void *)SUSAMUNE_GHOST_STORAGE_DATA_PPC_PTR,
                                    response.payloadSize,
                                    requestProfile == SUSAMUNE_GHOST_IMPORTED_PROFILE)
            : Ghost::importObserverTrack((const void *)SUSAMUNE_GHOST_STORAGE_DATA_PPC_PTR,
                  response.payloadSize, loadDestination == LOAD_DESTINATION_OBSERVER_SECONDARY);
        if (!accepted) {
            cancelFailedObserverLoad(loadDestination);
            notify(kImportFailed);
            queueNamespaceRefresh(requestProfile);
            return;
        }
#endif
        if (observerLoadDestination(loadDestination)) {
            notify(kLoaded);
        } else {
            sLoadedIdentity = sPendingIdentity;
            sLoadedValid = true;
            notify(kRaceLoaded);
        }
    }
}

#if !IS_EMULATOR
void pollResponse() {
    volatile SusamuneGhostStorageMailbox *mailbox = SUSAMUNE_GHOST_STORAGE_PPC_PTR;
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

__attribute__((noinline)) const SusamuneGhostSlotInfo *catalogEntry(
    bool imported, int slot) {
    if (!sReady[imported] || slot < 0 ||
        static_cast<u32>(slot) >= sPages[imported].count) return nullptr;
    return &sPages[imported].entries[slot].info;
}

bool copyLiteral(char *out, u32 size, const char *text, u32 length) {
    if (!out || size == 0) return false;
    const u32 count = length < size ? length : size - 1;
    if (count) memcpy(out, text, count);
    out[count] = '\0';
    return true;
}

bool copyVisibleName(char *out, u32 size, const char *text, u32 length) {
    for (u32 i = 0; i < length; i++)
        if (text[i] != ' ') return copyLiteral(out, size, text, length);
    return copyLiteral(out, size, kUnnamedName, sizeof(kUnnamedName) - 1);
}

__attribute__((noinline)) bool copyCatalogName(bool imported, int slot,
                                                char *out, u32 size) {
    if (!out || size == 0) return false;
    out[0] = '\0';
    const SusamuneGhostSlotInfo *info = catalogEntry(imported, slot);
    if (!info) return false;
    if (info->flags & SUSAMUNE_GHOST_SLOT_UNSAFE)
        return copyLiteral(out, size, kUnsafeName, sizeof(kUnsafeName) - 1);
    if (!(info->flags & SUSAMUNE_GHOST_SLOT_PRESENT))
        return copyLiteral(out, size, kEmptyName, sizeof(kEmptyName) - 1);
    return copyVisibleName(out, size, info->name, info->nameLength);
}

__attribute__((noinline)) bool requestIdentity(const Identity &identity,
    u16 command, const char *status, u8 destination = LOAD_DESTINATION_RACE) {
    observeProfile();
    if (!requestIdle()) return false;
    if (!identityValid(identity)) {
        sStatus = kBadSlot;
        return false;
    }
    if ((identity.flags & SUSAMUNE_GHOST_SLOT_UNSAFE) &&
        command != SUSAMUNE_GHOST_CMD_DELETE) {
        sStatus = kUnsafe;
        return false;
    }
    const bool imported = identity.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE;
    if (imported && command == SUSAMUNE_GHOST_CMD_EXPORT) {
        sStatus = kInvalidRequest;
        return false;
    }
#if !IS_EMULATOR
    const u32 payloadSize = imported ? sizeof(identity.leaf) : 0;
    if (imported) {
        memcpy((void *)SUSAMUNE_GHOST_STORAGE_DATA_PPC_PTR, identity.leaf, payloadSize);
        DCFlushRange((void *)SUSAMUNE_GHOST_STORAGE_DATA_PPC_PTR, payloadSize);
    }
    sPendingIdentity = identity;
    sPendingLoadDestination = destination;
    beginRequest(command, identity.profile, identity.id, payloadSize, 0,
                 status, 0, identity.generation);
    return true;
#else
    (void)status;
    (void)destination;
    return false;
#endif
}

__attribute__((noinline)) bool loadTrack(bool imported, int slot, u8 destination) {
    Identity identity;
    if (!copyIdentity(imported, slot, &identity)) {
        sStatus = kCatalogNeeded;
        return false;
    }
    return requestIdentity(identity, SUSAMUNE_GHOST_CMD_LOAD, kLoading, destination);
}

__attribute__((noinline)) bool requestPersonalCommand(int slot, u16 command,
                                                       const char *status) {
    Identity identity;
    if (!copyIdentity(false, slot, &identity)) {
        sStatus = kCatalogNeeded;
        return false;
    }
    return requestIdentity(identity, command, status);
}

}  // namespace

void init() {
    memset(sPages, 0, 2 * kCatalogBytes);
    memset(&sPendingIdentity, 0, sizeof(sPendingIdentity));
    memset(&sLoadedIdentity, 0, sizeof(sLoadedIdentity));
    sSequence = sPendingSequence = sPendingRecordToken = sPendingDurationQf = 0;
    sWaitFrames = sPendingEpoch = sPendingSlot = 0;
    sEpoch = 1;
    sPendingCommand = SUSAMUNE_GHOST_CMD_NONE;
    sPendingProfile = 0;
    sPendingLoadDestination = LOAD_DESTINATION_RACE;
    sProfile = activeProfile();
    sOffsets[0] = sOffsets[1] = 0;
    sReady[0] = sReady[1] = sRefreshQueued[0] = sRefreshQueued[1] = false;
    sCachedRefresh[0] = sCachedRefresh[1] = false;
    sLoadedValid = sAvailable = sImportScanQueued = sTimedOut = false;
    sStatus = IS_EMULATOR ? kDolphinUnavailable : kUnavailable;
#if !IS_EMULATOR
    volatile SusamuneGhostStorageMailbox *mailbox = SUSAMUNE_GHOST_STORAGE_PPC_PTR;
    DCInvalidateRange((void *)&mailbox->response, sizeof(mailbox->response));
    SusamuneGhostStorageResponse response;
    memcpy(&response, (const void *)&mailbox->response, sizeof(response));
    if (response.responseMagic == SUSAMUNE_GHOST_STORAGE_MAGIC &&
        response.protocolVersion == SUSAMUNE_GHOST_STORAGE_VERSION &&
        (response.flags & ~(SUSAMUNE_GHOST_RESPONSE_READY |
                            SUSAMUNE_GHOST_RESPONSE_BUSY)) == 0 &&
        !(response.flags & SUSAMUNE_GHOST_RESPONSE_BUSY)) {
        sSequence = response.ackSeq;
        sAvailable = (response.flags & SUSAMUNE_GHOST_RESPONSE_READY) != 0;
        sStatus = sAvailable ? kReady : statusForCode(response.status);
        sRefreshQueued[0] = sRefreshQueued[1] = sAvailable;
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
            if (++sEpoch == 0) sEpoch++;
        }
#if !IS_EMULATOR
        pollResponse();
#endif
        return;
    }
    for (int imported = 0; imported < 2; imported++) {
        if (!sRefreshQueued[imported]) continue;
        sRefreshQueued[imported] = false;
        const u16 command = imported && sImportScanQueued
            ? SUSAMUNE_GHOST_CMD_IMPORT_SCAN : SUSAMUNE_GHOST_CMD_LIST;
        const bool cached = sCachedRefresh[imported];
        sCachedRefresh[imported] = false;
        if (imported) sImportScanQueued = false;
        beginRefresh(imported != 0, command, cached);
        return;
    }
}

void onSavestateLoaded() {
    if (++sEpoch == 0) sEpoch++;
    observeLoadedPlayback();
    if (sPendingCommand == SUSAMUNE_GHOST_CMD_LOAD) {
        cancelFailedObserverLoad(sPendingLoadDestination);
        sStatus = kLoadIgnored;
    }
}

bool refreshPage(bool imported, u32 offset) {
    observeProfile();
    if (!sAvailable) {
        sStatus = IS_EMULATOR ? kDolphinUnavailable : kUnavailable;
        return false;
    }
    if (offset % SUSAMUNE_GHOST_CATALOG_PAGE_ENTRIES != 0) {
        sStatus = kBadSlot;
        return false;
    }
    const bool cached = offset != sOffsets[imported] &&
        (!sRefreshQueued[imported] || sCachedRefresh[imported]);
    sOffsets[imported] = offset;
    if (imported) sImportScanQueued = false;
    clearCatalog(imported);
    sRefreshQueued[imported] = true;
    sCachedRefresh[imported] = cached;
    if (busy()) return true;
    sRefreshQueued[imported] = false;
    sCachedRefresh[imported] = false;
    return beginRefresh(imported, SUSAMUNE_GHOST_CMD_LIST, cached);
}

bool refresh() { return refreshPage(false, sOffsets[0]); }
bool refreshImported() { return requestImportedRefresh(SUSAMUNE_GHOST_CMD_LIST); }
bool scanImports() { return requestImportedRefresh(SUSAMUNE_GHOST_CMD_IMPORT_SCAN); }

bool identityValid(const Identity &identity) {
    const bool imported = identity.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE;
    const u16 knownFlags = SUSAMUNE_GHOST_SLOT_PRESENT |
        SUSAMUNE_GHOST_SLOT_UNSAFE | SUSAMUNE_GHOST_SLOT_IMPORTED;
    if (identity.id == SUSAMUNE_GHOST_SLOT_AUTO ||
        identity.region >= SUSAMUNE_GHOST_REGION_COUNT ||
        (identity.flags & ~knownFlags) != 0 ||
        !(identity.flags & (SUSAMUNE_GHOST_SLOT_PRESENT | SUSAMUNE_GHOST_SLOT_UNSAFE)) ||
        !zeroBytes(identity.reserved, sizeof(identity.reserved)) ||
        !safeText(identity.name, sizeof(identity.name), identity.nameLength, false))
        return false;
    if (imported)
        return (identity.flags & SUSAMUNE_GHOST_SLOT_IMPORTED) && validLeaf(identity.leaf);
    return identity.profile == activeProfile() && identity.region == kRegion &&
        !(identity.flags & SUSAMUNE_GHOST_SLOT_IMPORTED) &&
        zeroBytes(identity.leaf, sizeof(identity.leaf));
}

bool sameIdentity(const Identity &a, const Identity &b) {
    if (a.profile != b.profile || a.region != b.region) return false;
    return a.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE
        ? memcmp(a.leaf, b.leaf, sizeof(a.leaf)) == 0 : a.id == b.id;
}

bool copyIdentity(bool imported, int row, Identity *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->id = SUSAMUNE_GHOST_SLOT_AUTO;
    observeProfile();
    const SusamuneGhostSlotInfo *info = catalogEntry(imported, row);
    if (!info) return false;
    const SusamuneGhostCatalogEntry &entry = sPages[imported].entries[row];
    out->id = entry.id;
    out->generation = info->generation;
    out->flags = info->flags;
    out->profile = imported ? SUSAMUNE_GHOST_IMPORTED_PROFILE : sProfile;
    out->region = imported && !(info->flags & SUSAMUNE_GHOST_SLOT_UNSAFE)
        ? info->region : kRegion;
    out->nameLength = info->nameLength;
    memcpy(out->name, info->name, sizeof(out->name));
    memcpy(out->leaf, entry.leaf, sizeof(out->leaf));
    return identityValid(*out);
}

bool copyIdentityName(const Identity &identity, char *out, u32 size) {
    if (!out || !size) return false;
    out[0] = '\0';
    if (!identityValid(identity)) return false;
    if (identity.flags & SUSAMUNE_GHOST_SLOT_UNSAFE)
        return copyLiteral(out, size, kUnsafeName, sizeof(kUnsafeName) - 1);
    return copyVisibleName(out, size, identity.name, identity.nameLength);
}

bool isLoaded(const Identity &identity) {
    return sLoadedValid && Ghost::playbackPinned() &&
           sameIdentity(identity, sLoadedIdentity) &&
           identity.generation == sLoadedIdentity.generation;
}

bool saveNew(u32 expectedSelectionToken) {
    observeProfile();
    if (!requestIdle()) return false;
    if (!Ghost::hasSaveableTrack()) { sStatus = kNoRecording; return false; }
#if !IS_EMULATOR
    if (expectedSelectionToken) {
        char currentName[SUSAMUNE_GHOST_NAME_SIZE];
        u32 currentToken = 0;
        if (!Ghost::copySaveableName(currentName, sizeof(currentName), &currentToken) ||
            currentToken != expectedSelectionToken) {
            sStatus = kSaveChanged;
            return false;
        }
    }
    u32 size = 0, recordToken = 0;
    if (!Ghost::exportLatest((void *)SUSAMUNE_GHOST_STORAGE_DATA_PPC_PTR,
            SUSAMUNE_GHOST_STORAGE_PAYLOAD_SIZE, sProfile, ILing::pbProfileName(sProfile),
            &size, &recordToken) || size < SUSAMUNE_GHOST_FILE_HEADER_SIZE ||
            size > SUSAMUNE_GHOST_MAX_FILE_SIZE) {
        sStatus = kExportFailed;
        return false;
    }
    DCFlushRange((void *)SUSAMUNE_GHOST_STORAGE_DATA_PPC_PTR, size);
    const SusamuneGhostFileHeader *header = reinterpret_cast<const SusamuneGhostFileHeader *>(
        (const void *)SUSAMUNE_GHOST_STORAGE_DATA_PPC_PTR);
    beginRequest(SUSAMUNE_GHOST_CMD_SAVE, sProfile, SUSAMUNE_GHOST_SLOT_AUTO,
                 size, recordToken, kSaving, header->durationQf);
    return true;
#else
    (void)expectedSelectionToken;
    return false;
#endif
}

// Occupied page rows must never become overwrite requests.
bool save(int slot) { return save(slot, 0); }
bool save(int slot, u32 token) {
    if (slot != -1) { sStatus = kOccupied; return false; }
    return saveNew(token);
}
bool load(const Identity &identity) {
    return requestIdentity(identity, SUSAMUNE_GHOST_CMD_LOAD, kLoading);
}
bool loadObserver(const Identity &identity, bool secondary) {
    return requestIdentity(identity, SUSAMUNE_GHOST_CMD_LOAD, kLoading,
        secondary ? LOAD_DESTINATION_OBSERVER_SECONDARY : LOAD_DESTINATION_OBSERVER_PRIMARY);
}
bool remove(const Identity &identity) {
    return requestIdentity(identity, SUSAMUNE_GHOST_CMD_DELETE, kDeleting);
}
bool exportShare(const Identity &identity) {
    return requestIdentity(identity, SUSAMUNE_GHOST_CMD_EXPORT, kExportingShare);
}
bool load(int slot) { return loadTrack(false, slot, LOAD_DESTINATION_RACE); }
bool loadObserver(int slot, bool secondary) {
    return loadTrack(false, slot,
        secondary ? LOAD_DESTINATION_OBSERVER_SECONDARY : LOAD_DESTINATION_OBSERVER_PRIMARY);
}
bool remove(int slot) {
    return requestPersonalCommand(slot, SUSAMUNE_GHOST_CMD_DELETE, kDeleting);
}
bool exportShare(int slot) {
    return requestPersonalCommand(slot, SUSAMUNE_GHOST_CMD_EXPORT, kExportingShare);
}
bool importShare(int slot) { (void)slot; return scanImports(); }
bool loadImported(int slot) { return loadTrack(true, slot, LOAD_DESTINATION_RACE); }
bool loadImportedObserver(int slot, bool secondary) {
    return loadTrack(true, slot,
        secondary ? LOAD_DESTINATION_OBSERVER_SECONDARY : LOAD_DESTINATION_OBSERVER_PRIMARY);
}
bool removeImported(int slot) {
    Identity identity;
    if (!copyIdentity(true, slot, &identity)) { sStatus = kCatalogNeeded; return false; }
    return remove(identity);
}
bool busy() { return sPendingCommand != SUSAMUNE_GHOST_CMD_NONE; }
bool timedOut() { return busy() && sTimedOut; }
bool available() { return sAvailable; }
bool catalogReady() { return sReady[0]; }
bool importedCatalogReady() { return sReady[1]; }
int profile() { return sProfile; }
u32 pageOffset(bool imported) { return sOffsets[imported]; }
u32 pageCount(bool imported) { return sReady[imported] ? sPages[imported].count : 0; }
u32 totalCount(bool imported) { return sReady[imported] ? sPages[imported].totalCount : 0; }
int loadedSlot() {
    return sLoadedValid && sLoadedIdentity.profile == sProfile && Ghost::playbackPinned() &&
        sLoadedIdentity.id <= 0x7fffffffu ? static_cast<int>(sLoadedIdentity.id) : -1;
}
bool loadedImported() {
    return sLoadedValid && sLoadedIdentity.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE &&
        Ghost::playbackPinned();
}
int loadedImportedSlot() {
    return loadedImported() && sLoadedIdentity.id <= 0x7fffffffu
        ? static_cast<int>(sLoadedIdentity.id) : -1;
}
u64 totalDurationQf() {
    return sReady[0] ? (static_cast<u64>(sPages[0].totalDurationQfHi) << 32) |
        sPages[0].totalDurationQfLo : 0;
}
u64 importedTotalDurationQf() {
    return sReady[1] ? (static_cast<u64>(sPages[1].totalDurationQfHi) << 32) |
        sPages[1].totalDurationQfLo : 0;
}
u32 importedOverflowCount() { return 0; }
const char *statusText() { return sStatus; }
bool copySlotName(int slot, char *out, u32 size) {
    return copyCatalogName(false, slot, out, size);
}
bool copyImportedSlotName(int slot, char *out, u32 size) {
    return copyCatalogName(true, slot, out, size);
}
const SusamuneGhostSlotInfo *slot(int slot) { return catalogEntry(false, slot); }
const SusamuneGhostSlotInfo *importedSlot(int slot) { return catalogEntry(true, slot); }

}  // namespace GhostStorage
