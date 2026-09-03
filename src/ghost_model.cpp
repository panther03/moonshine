#include "susamune/ghost_model.hxx"

#include "Dolphin/GX.h"
#include "Dolphin/MTX.h"
#include "Dolphin/OS.h"
#include "Dolphin/mem.h"
#include "JSystem/J3D/J3DDrawBuffer.hxx"
#include "JSystem/J3D/J3DModel.hxx"
#include "JSystem/J3D/J3DModelLoaderDataBase.hxx"
#include "JSystem/J3D/J3DShape.hxx"
#include "JSystem/JDrama/JDRViewObjPtrListT.hxx"
#include "JSystem/JKernel/JKRFileLoader.hxx"
#include "JSystem/JKernel/JKRHeap.hxx"
#include "SMS/Player/Mario.hxx"
#include "SMS/Player/MarioDraw.hxx"
#include "SMS/Player/Yoshi.hxx"
#include "SMS/MoveBG/ResetFruit.hxx"
#include "SMS/Strategic/LiveActor.hxx"
#include "SMS/Strategic/Strategy.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/addresses.hxx"
#include "susamune/checksum.hxx"
#include "susamune/ghost.hxx"
#include "susamune/ghost_model_asset.h"
#include "susamune/ghost_storage.h"
#include "susamune/menu.hxx"
#include "susamune/settings.hxx"

class TScreenTexture {
public:
    bool replace(J3DModelData *, const char *);
};

extern TScreenTexture *gpScreenTexture;

void SMS_InitPacket_MatColor(J3DModel *, u16, GXChannelID,
                             const GXColor *);
J3DMtxCalc *J3DNewMtxCalcAnm(u32, J3DAnmTransform *);

namespace GhostModel {
namespace {

enum Appearance {
    APPEARANCE_SHADOW = 0,
    APPEARANCE_PIANTA = 1,
    APPEARANCE_COUNT = 2,
};

struct AssetHeader {
    u32 magic;
    u16 version;
    u16 headerSize;
    s32 status;
    u32 totalSize;
    u32 bmdOffset;
    u32 bmdSize;
    u32 payloadChecksum;
    u32 reserved;
};

struct ModelSlot {
    JKRExpHeap *heap;
    J3DModelData *data;
    J3DModel *model;
    J3DMtxCalc *mtxCalc;
    bool opaque;
    bool opacityConfigured;
    bool colorCallbacksInstalled;
};

enum AttachmentKind {
    ATTACHMENT_NONE,
    ATTACHMENT_YOSHI,
    ATTACHMENT_HELD,
};

struct AttachmentPacketState {
    GXColor channelColor;
    GXColorS10 tevColor;
    bool tintTevColor;
};

struct AttachmentModel {
    J3DModelData *data;
    J3DModel *model;
    int runner;
    AttachmentKind kind;
    AttachmentPacketState packetState;
    u32 estimatedBytes;
    u32 usedBytes;
};

struct AttachmentRequest {
    AttachmentKind kind;
    J3DModel *source;
};

const u32 kCueCalcView = 0x00000004u;
const u32 kCueEntry = 0x00000200u;
const u32 kRegistrationMinFree = 64u;
const u32 kShadowLoadFlags = 0x10210000u;
const u32 kPiantaLoadFlags = 0x10040000u;
const u32 kShadowWorstCaseUsed = 0x118CDu;
const u32 kPiantaWorstCaseUsed = 0x11B4Du;
const u32 kModelAllocationPreflight = 0x12000u;
const u32 kFixedExpHeapOverhead = 0x130u;
const u32 kAttachmentInstanceMax = 0xFF00u;
const u32 kAttachmentAllocOverhead = 0x40u;
const u32 kShapePacketStride = 0x34u;
const u32 kMaterialPacketStride = 0x48u;
const u32 kDisplayListObjectSize = 0x10u;
const u32 kVertexBufferSize = 0x3Cu;
const u32 kShapePacketUserAreaOffset = 0x0Cu;
const u32 kShapePacketCallbackOffset = 0x10u;
const u32 kShapePacketShapeOffset = 0x14u;
const u32 kShapePacketDrawMtxOffset = 0x18u;
const u32 kShapePacketNrmMtxOffset = 0x1Cu;
const u32 kShapePacketViewNoOffset = 0x20u;
const u32 kExpectedJointCount = 29u;
const u16 kYoshiMaxJointCount = 38u;
const u16 kYoshiMaxEnvelopeCount = 52u;
const u16 kYoshiMaxDrawMtxCount = 126u;
const u16 kYoshiMaxShapeCount = 11u;
const u16 kYoshiMaxMaterialCount = 11u;
const f32 kAngleToRadians = 0.00009587379924285257f;
// Mario's BCK table is 0..198; retail indexes rider poses 0xB6..0xC6 directly.
const u16 kMarioBckCount = 199u;
const u32 kJumpBaseObjectId = 0x40000017u;
const char kShadowModelPath[] = "/scene/kagemario/default.bmd";
const char kPiantaModelPath[] = "/scene/map/map/pad/monteman_model.bmd";
const char kShadowTextureName[] = "H_kagemario_dummy";

static_assert(sizeof(AssetHeader) == SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE,
              "ghost model asset header ABI changed");
static_assert(SUSAMUNE_GHOST_MODEL_HEAP_OFFSET ==
                  SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE,
              "primary model heap must follow the sample payload");
static_assert(SUSAMUNE_GHOST_MODEL_HEAP_OFFSET +
                      SUSAMUNE_GHOST_MODEL_HEAP_SIZE <=
                  SUSAMUNE_GHOST_SEGMENT_TABLE_OFFSET,
              "primary model heap overlaps playback data");
static_assert(SUSAMUNE_GHOST_STORAGE_HEADER_SIZE +
                      SUSAMUNE_GHOST_MAX_FILE_SIZE <=
                  SUSAMUNE_GHOST_SECONDARY_HEAP_OFFSET,
              "secondary model heap overlaps transfer payload");
static_assert(SUSAMUNE_GHOST_SECONDARY_HEAP_OFFSET +
                      SUSAMUNE_GHOST_SECONDARY_HEAP_SIZE ==
                  SUSAMUNE_GHOST_SLOT_SIZE,
              "secondary model heap must consume the transfer tail");
static_assert(((SUSAMUNE_GHOST_STORAGE_HEADER_SIZE +
                SUSAMUNE_GHOST_MAX_FILE_SIZE) &
               31u) == 0,
              "transfer payload end must be cache-line aligned");
static_assert(kShadowWorstCaseUsed <= kModelAllocationPreflight,
              "Shadow allocation proof exceeds the preflight bound");
static_assert(kPiantaWorstCaseUsed <= kModelAllocationPreflight,
              "Piantissimo allocation proof exceeds the preflight bound");
static_assert(SUSAMUNE_GHOST_MODEL_HEAP_SIZE >=
                  kModelAllocationPreflight + kFixedExpHeapOverhead,
              "primary model heap cannot satisfy its preflight");
static_assert(SUSAMUNE_GHOST_SECONDARY_HEAP_SIZE >=
                  kModelAllocationPreflight + kFixedExpHeapOverhead,
              "secondary model heap cannot satisfy its preflight");
static_assert(SUSAMUNE_MOD_ATTACHMENT_HEAP_SIZE >=
                  kAttachmentInstanceMax * 2u + kFixedExpHeapOverhead,
              "attachment heap cannot hold two worst-case instances");
static_assert(SUSAMUNE_MOD_ATTACHMENT_HEAP_OFFSET ==
                  SUSAMUNE_MOD_BLOB_MAX_SIZE,
              "attachment heap must follow the mod working cap");
static_assert(SUSAMUNE_MOD_ATTACHMENT_HEAP_OFFSET +
                      SUSAMUNE_MOD_ATTACHMENT_HEAP_SIZE <=
                  SUSAMUNE_MOD_SCRATCH_OFFSET,
              "attachment heap overlaps mod scratch");
static_assert(SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE +
                      SUSAMUNE_GHOST_SHADOW_MASTER_SIZE <=
                  SUSAMUNE_GHOST_SEGMENT_TABLE_OFFSET,
              "Shadow work copy overlaps the record segment table");

ModelSlot sSlots[APPEARANCE_COUNT];
JKRExpHeap *sAttachmentHeap;
AttachmentModel sAttachmentModels[2];
bool sRegistered;
bool sPrepared[2];
AttachmentModel *sPreparedAttachments[2];
bool sSubmitted[2];
Ghost::VisualState sVisualStates[2];
bool sHaveVisualState[2];
GXColor sGhostColor = {255, 255, 255, 160};
TMarDirector *volatile sPendingDirector;
volatile u32 sPendingGeneration;
u32 sQuiescedGeneration;
u32 sLoadedGeneration;

u32 readBig32(const u8 *bytes) {
    return (static_cast<u32>(bytes[0]) << 24) |
           (static_cast<u32>(bytes[1]) << 16) |
           (static_cast<u32>(bytes[2]) << 8) |
           static_cast<u32>(bytes[3]);
}

bool validBmdHeader(const void *resource, u32 size) {
    const u8 *bytes = static_cast<const u8 *>(resource);
    return bytes && memcmp(bytes, "J3D2bmd3", 8) == 0 &&
           readBig32(bytes + 8) == size;
}

bool validBmd(const void *resource, u32 size, u32 checksum) {
    return validBmdHeader(resource, size) &&
           Checksum::crc32(resource, size) == checksum;
}

#if !defined(IS_EMULATOR) || !IS_EMULATOR
const u8 *validateMaster(void *raw, u32 bufferSize, u32 magic,
                         u32 totalSize, u32 bmdOffset, u32 bmdSize,
                         u32 payloadChecksum) {
    DCInvalidateRange(raw, bufferSize);
    const AssetHeader *header = static_cast<const AssetHeader *>(raw);
    if (header->magic != magic ||
        header->version != SUSAMUNE_GHOST_MODEL_ASSET_VERSION ||
        header->headerSize != SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE ||
        header->status != SUSAMUNE_GHOST_MODEL_STATUS_READY ||
        header->totalSize != totalSize || header->bmdOffset != bmdOffset ||
        header->bmdSize != bmdSize ||
        header->payloadChecksum != payloadChecksum || header->reserved != 0 ||
        totalSize < bmdOffset || totalSize > bufferSize) {
        return nullptr;
    }
    const u8 *payload = static_cast<const u8 *>(raw) + bmdOffset;
    return Checksum::crc32(payload, totalSize - bmdOffset) == payloadChecksum
        ? payload
        : nullptr;
}

const u8 *shadowMaster() {
    void *asset = const_cast<SusamuneGhostShadowAsset *>(
        SUSAMUNE_GHOST_SHADOW_MASTER_PPC_PTR);
    return validateMaster(asset, SUSAMUNE_GHOST_SHADOW_ASSET_BUFFER_SIZE,
                          SUSAMUNE_GHOST_SHADOW_ASSET_MAGIC,
                          SUSAMUNE_GHOST_SHADOW_ASSET_SIZE,
                          SUSAMUNE_GHOST_SHADOW_BMD_OFFSET,
                          SUSAMUNE_GHOST_SHADOW_BMD_SIZE,
                          SUSAMUNE_GHOST_SHADOW_PAYLOAD_CRC32);
}

const u8 *piantaMaster() {
    void *asset = const_cast<SusamuneGhostPiantaAsset *>(
        SUSAMUNE_GHOST_PIANTA_MASTER_PPC_PTR);
    return validateMaster(asset, SUSAMUNE_GHOST_PIANTA_ASSET_BUFFER_SIZE,
                          SUSAMUNE_GHOST_PIANTA_ASSET_MAGIC,
                          SUSAMUNE_GHOST_PIANTA_ASSET_SIZE,
                          SUSAMUNE_GHOST_PIANTA_BMD_OFFSET,
                          SUSAMUNE_GHOST_PIANTA_BMD_SIZE,
                          SUSAMUNE_GHOST_PIANTA_PAYLOAD_CRC32);
}
#endif

const void *stageLocalBmd(const char *path, u32 size, u32 checksum,
                          bool allowTextureMutation) {
    const void *resource = JKRFileLoader::getGlbResource(path);
    if (!validBmdHeader(resource, size)) return nullptr;
    return allowTextureMutation || Checksum::crc32(resource, size) == checksum
        ? resource
        : nullptr;
}

const void *shadowResource() {
#if !defined(IS_EMULATOR) || !IS_EMULATOR
    const u8 *master = shadowMaster();
    if (master) {
        SusamuneGhostShadowAsset *workingAsset =
            const_cast<SusamuneGhostShadowAsset *>(
                SUSAMUNE_GHOST_SHADOW_STAGING_PPC_PTR);
        u8 *working = reinterpret_cast<u8 *>(workingAsset) +
                      SUSAMUNE_GHOST_SHADOW_BMD_OFFSET;
        memcpy(working, master, SUSAMUNE_GHOST_SHADOW_BMD_SIZE);
        DCFlushRange(working, SUSAMUNE_GHOST_SHADOW_BMD_SIZE);
        if (!validBmd(working, SUSAMUNE_GHOST_SHADOW_BMD_SIZE,
                      SUSAMUNE_GHOST_SHADOW_BMD_CRC32)) {
            return nullptr;
        }
        return working;
    }
#endif
    const void *local = stageLocalBmd(
        kShadowModelPath, SUSAMUNE_GHOST_SHADOW_BMD_SIZE,
        SUSAMUNE_GHOST_SHADOW_BMD_CRC32, true);
    return local;
}

const void *piantaResource() {
#if !defined(IS_EMULATOR) || !IS_EMULATOR
    const u8 *master = piantaMaster();
    if (master && validBmd(master, SUSAMUNE_GHOST_PIANTA_BMD_SIZE,
                           SUSAMUNE_GHOST_PIANTA_BMD_CRC32)) {
        return master;
    }
#endif
    const void *local = stageLocalBmd(
        kPiantaModelPath, SUSAMUNE_GHOST_PIANTA_BMD_SIZE,
        SUSAMUNE_GHOST_PIANTA_BMD_CRC32, false);
    return local;
}

u8 ghostAlpha() {
    static const u8 kOpacity[] = {64, 128, 192, 255};
    u8 choice = gSettings.get(SETTING_GHOST_OPACITY);
    if (choice >= sizeof(kOpacity)) choice = 1;
    return kOpacity[choice];
}

J3DAnmTransform *marioAnimation(u16 logicalId, bool ridingYoshi) {
    if (!gpMarioOriginal || !gpMarioOriginal->mModelData ||
        !gpMarioOriginal->mModelData->_04 ||
        logicalId > SUSAMUNE_GHOST_ANIMATION_ID_MAX) {
        return nullptr;
    }
    // Retail bypasses gMarioAnimeData for 0xB6..0xC6 rider poses.
    const u16 bckId = ridingYoshi
        ? logicalId : gMarioAnimeData[logicalId].mAnimID;
    if (bckId >= kMarioBckCount) return nullptr;
    u8 *common = static_cast<u8 *>(gpMarioOriginal->mModelData->_04);
    J3DAnmTransform **animations =
        *reinterpret_cast<J3DAnmTransform ***>(common + 4);
    return animations ? animations[bckId] : nullptr;
}

J3DAnmTransform **animationSlot(ModelSlot &slot) {
    return slot.mtxCalc
        ? reinterpret_cast<J3DAnmTransform **>(
              reinterpret_cast<u8 *>(slot.mtxCalc) - 0x10)
        : nullptr;
}

s16 animationFrameMax(const J3DAnmTransform *animation) {
    return animation
        ? *reinterpret_cast<const s16 *>(
              reinterpret_cast<const u8 *>(animation) + 2)
        : 0;
}

f32 *animationFrame(J3DAnmTransform *animation) {
    return animation
        ? reinterpret_cast<f32 *>(reinterpret_cast<u8 *>(animation) + 4)
        : nullptr;
}

J3DMtxCalc **rootMtxCalcSlot(ModelSlot &slot) {
    return slot.data && slot.data->mRootNode
        ? reinterpret_cast<J3DMtxCalc **>(
              reinterpret_cast<u8 *>(slot.data->mRootNode) + 0x58)
        : nullptr;
}

bool configureOpacity(ModelSlot &slot, bool opaque) {
    if (!slot.data || !slot.model) return false;
    if (slot.opacityConfigured && slot.opaque == opaque) return true;
    const u16 materialCount = slot.data->getMaterialNum();
    for (u16 i = 0; i < materialCount; ++i) {
        u8 *material = reinterpret_cast<u8 *>(slot.data->mMaterials[i]);
        if (!material) return false;
        u32 *mode = reinterpret_cast<u32 *>(material + 0x08);
        u8 *pe = *reinterpret_cast<u8 **>(material + 0x30);
        if (!pe) return false;

        *mode = (*mode & ~7u) | (opaque ? 1u : 4u);
        *reinterpret_cast<u16 *>(pe + 0x08) = 0x00E7u;
        pe[0x0a] = 0;
        pe[0x0b] = 0;
        pe[0x0c] = opaque ? GX_BM_NONE : GX_BM_BLEND;
        pe[0x0d] = opaque ? GX_BL_ONE : GX_BL_SRCALPHA;
        pe[0x0e] = opaque ? GX_BL_ZERO : GX_BL_INVSRCALPHA;
        pe[0x0f] = GX_LO_COPY;
        *reinterpret_cast<u16 *>(pe + 0x10) = opaque ? 0x0017u : 0x0016u;
    }
    slot.model->makeDL();
    slot.opaque = opaque;
    slot.opacityConfigured = true;
    return true;
}

bool installColorCallbacks(ModelSlot &slot) {
    if (!slot.data || !slot.model) return false;
    if (slot.colorCallbacksInstalled) return true;
    for (u16 i = 0; i < slot.data->getMaterialNum(); ++i)
        SMS_InitPacket_MatColor(slot.model, i, GX_COLOR0A0, &sGhostColor);
    slot.colorCallbacksInstalled = true;
    return true;
}

u64 alignTo(u64 value, u32 alignment) {
    return (value + alignment - 1u) &
           ~static_cast<u64>(alignment - 1u);
}

bool validAttachmentSource(const J3DModel *source, u32 *estimateOut) {
    if (!source || !source->mModelData || !estimateOut ||
        source->mBumpMtxBuf[0] || source->mBumpMtxBuf[1]) {
        return false;
    }
    J3DModelData *data = source->mModelData;
    const u16 joints = data->getJointNum();
    const u16 materials = data->getMaterialNum();
    const u16 shapes = data->mShapeNum;
    if (joints == 0 || joints > kYoshiMaxJointCount ||
        data->_84 > kYoshiMaxEnvelopeCount ||
        data->mDrawMtxData.mEntryNum > kYoshiMaxDrawMtxCount ||
        shapes == 0 || shapes > kYoshiMaxShapeCount ||
        materials == 0 || materials > kYoshiMaxMaterialCount ||
        !data->mMaterials || !data->mShapes) {
        return false;
    }

    u64 payload = alignTo(sizeof(J3DModel), 32u);
    payload += alignTo(joints, 4u);
    if (data->_84) payload += alignTo(data->_84, 4u);
    payload += alignTo(static_cast<u64>(joints) * sizeof(Mtx), 4u);
    if (data->_84) {
        payload += alignTo(static_cast<u64>(data->_84) * sizeof(Mtx), 4u);
    }
    // mtxNum is one: four pointer arrays and four matrix arrays.
    payload += 4u * sizeof(void *);
    payload += 2u * alignTo(static_cast<u64>(
        data->mDrawMtxData.mEntryNum) * sizeof(Mtx), 32u);
    payload += 2u * alignTo(static_cast<u64>(
        data->mDrawMtxData.mEntryNum) * sizeof(Mtx33), 32u);
    payload += static_cast<u64>(shapes) * kShapePacketStride;
    payload += static_cast<u64>(materials) * kMaterialPacketStride;
    payload += static_cast<u64>(materials) * kDisplayListObjectSize;
    payload += kVertexBufferSize;
    for (u16 i = 0; i < materials; ++i) {
        if (!data->mMaterials[i]) return false;
        payload += alignTo(data->mMaterials[i]->countDLSize(), 32u) * 2u;
    }
    const u64 allocationCount = 14u + (data->_84 ? 2u : 0u) +
                                static_cast<u64>(materials) * 3u;
    const u64 estimate = payload +
                         allocationCount * kAttachmentAllocOverhead;
    if (estimate > kAttachmentInstanceMax) return false;
    *estimateOut = static_cast<u32>(estimate);
    return true;
}

void attachmentPacketCallback(J3DShapePacket *packet, int phase) {
    if (!packet || phase != 0) return;
    u8 *raw = reinterpret_cast<u8 *>(packet);
    AttachmentPacketState *state =
        *reinterpret_cast<AttachmentPacketState **>(
            raw + kShapePacketUserAreaOffset);
    if (!state) return;
    GXSetChanMatColor(GX_COLOR0A0, state->channelColor);
    if (state->tintTevColor)
        GXSetTevColorS10(GX_TEVREG2, state->tevColor);
    const bool opaque = state->channelColor.a == 255;
    if (!opaque) {
        GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    }
    GXSetBlendMode(opaque ? GX_BM_NONE : GX_BM_BLEND,
                   opaque ? GX_BL_ONE : GX_BL_SRCALPHA,
                   opaque ? GX_BL_ZERO : GX_BL_INVSRCALPHA,
                   GX_LO_COPY);
    GXSetZMode(GX_TRUE, GX_LEQUAL, opaque ? GX_TRUE : GX_FALSE);
}

void installAttachmentCallbacks(AttachmentModel &attachment) {
    if (!attachment.model || !attachment.data) return;
    for (u16 i = 0; i < attachment.data->mShapeNum; ++i) {
        u8 *packet = reinterpret_cast<u8 *>(attachment.model->mShapePackets) +
                     static_cast<u32>(i) * kShapePacketStride;
        *reinterpret_cast<AttachmentPacketState **>(
            packet + kShapePacketUserAreaOffset) = &attachment.packetState;
        *reinterpret_cast<void (**)(J3DShapePacket *, int)>(
            packet + kShapePacketCallbackOffset) = attachmentPacketCallback;
    }
}

bool isSupportedFruit(u32 objectId) {
    static_assert(TResetFruit::COCONUT == 0x40000390u,
                  "retail fruit id changed");
    static_assert(TResetFruit::BANANA == 0x40000394u,
                  "retail fruit id changed");
    return objectId >= TResetFruit::COCONUT &&
           objectId <= TResetFruit::BANANA;
}

J3DModel *heldFruitSource(u32 objectId, u16 nameKey) {
    if (!gpStrategy || !isSupportedFruit(objectId)) return nullptr;
    // Retail TResetFruit rows register in the enemy group.
    TIdxGroupObj *group = gpStrategy->mEnemyGroup;
    if (!group) return nullptr;
    for (JGadget::TList_pointer_void::iterator it =
             group->mViewObjList.begin();
         it != group->mViewObjList.end(); ++it) {
        THitActor *actor = static_cast<THitActor *>(*it);
        if (!actor || actor->mObjectID != objectId ||
            actor->mKeyCode != nameKey) {
            continue;
        }
        TResetFruit *fruit = static_cast<TResetFruit *>(actor);
        return fruit->mActorData ? fruit->mActorData->mModel : nullptr;
    }
    return nullptr;
}

J3DModel *heldJumpBaseSource(u32 objectId, u16 nameKey) {
    if (!gpStrategy || objectId != kJumpBaseObjectId) return nullptr;
    // Retail jumpbase_data registers TJumpBase in the object group.
    TIdxGroupObj *group = gpStrategy->mObjectGroup;
    if (!group) return nullptr;
    for (JGadget::TList_pointer_void::iterator it =
             group->mViewObjList.begin();
         it != group->mViewObjList.end(); ++it) {
        THitActor *actor = static_cast<THitActor *>(*it);
        if (!actor || actor->mObjectID != objectId ||
            actor->mKeyCode != nameKey) {
            continue;
        }
        TLiveActor *jumpBase = static_cast<TLiveActor *>(actor);
        return jumpBase->mActorData
            ? jumpBase->mActorData->mModel : nullptr;
    }
    return nullptr;
}

J3DModel *yoshiSource() {
    return gpMarioOriginal && gpMarioOriginal->mYoshi &&
                   gpMarioOriginal->mYoshi->mActor
        ? gpMarioOriginal->mYoshi->mActor->mModel
        : nullptr;
}

void restoreHeap(JKRHeap *heap);

AttachmentModel *createAttachment(AttachmentModel &attachment, int runner,
                                  AttachmentKind kind, J3DModel *source) {
    memset(&attachment, 0, sizeof(attachment));
    attachment.data = source ? source->mModelData : nullptr;
    attachment.runner = runner;
    attachment.kind = kind;
    if (!sAttachmentHeap || !source || !source->mModelData) return nullptr;
    u32 estimate = 0;
    if (!validAttachmentSource(source, &estimate) ||
        sAttachmentHeap->getTotalFreeSize() < estimate) {
        return nullptr;
    }

    attachment.estimatedBytes = estimate;
    attachment.packetState.channelColor = sGhostColor;
    attachment.packetState.tintTevColor = kind == ATTACHMENT_YOSHI;

    JKRHeap *oldHeap = JKRHeap::sCurrentHeap;
    sAttachmentHeap->becomeCurrentHeap();
    const u32 before = sAttachmentHeap->getTotalFreeSize();
    void *storage = sAttachmentHeap->alloc(sizeof(J3DModel), 32);
    if (storage) {
        attachment.model =
            new (storage) J3DModel(attachment.data, 0, 1);
        attachment.model->makeDL();
        installAttachmentCallbacks(attachment);
    }
    const u32 after = sAttachmentHeap->getTotalFreeSize();
    attachment.usedBytes = before >= after ? before - after : 0;
    restoreHeap(oldHeap);

    if (!attachment.model || !attachment.model->mJointArray ||
        !attachment.model->mMatPackets || !attachment.model->mShapePackets ||
        !attachment.model->mVtxBuffer ||
        attachment.usedBytes > attachment.estimatedBytes ||
        attachment.usedBytes > kAttachmentInstanceMax) {
        attachment.model = nullptr;
        return nullptr;
    }
    return &attachment;
}

AttachmentRequest desiredAttachment(const Ghost::VisualState &state) {
    AttachmentRequest request = {ATTACHMENT_NONE, nullptr};
    if (state.yoshi) {
        request.kind = ATTACHMENT_YOSHI;
        request.source = yoshiSource();
    } else if (state.heldObjectId) {
        request.kind = ATTACHMENT_HELD;
        request.source = heldFruitSource(state.heldObjectId,
                                         state.heldNameKey);
        if (!request.source) {
            request.source = heldJumpBaseSource(state.heldObjectId,
                                                state.heldNameKey);
        }
    }
    if (!request.source) request.kind = ATTACHMENT_NONE;
    return request;
}

bool attachmentSetMatches(const AttachmentRequest requests[2]) {
    for (int runner = 0; runner < 2; ++runner) {
        const J3DModelData *desired = requests[runner].source
            ? requests[runner].source->mModelData : nullptr;
        const AttachmentModel &current = sAttachmentModels[runner];
        if (current.kind != requests[runner].kind ||
            current.data != desired) {
            return false;
        }
    }
    return true;
}

void rebuildAttachments(const AttachmentRequest requests[2]) {
    if (!sAttachmentHeap || attachmentSetMatches(requests)) return;
    // The previous frame may still reference these private display lists.
    GXDrawDone();
    sAttachmentHeap->freeAll();
    memset(sAttachmentModels, 0, sizeof(sAttachmentModels));
    for (int runner = 0; runner < 2; ++runner) {
        if (requests[runner].kind != ATTACHMENT_NONE)
            createAttachment(sAttachmentModels[runner], runner,
                             requests[runner].kind,
                             requests[runner].source);
    }
}

void restoreHeap(JKRHeap *heap) {
    if (heap) heap->becomeCurrentHeap();
    else JKRHeap::sCurrentHeap = nullptr;
}

void clearLiveModel(ModelSlot &slot) {
    slot.data = nullptr;
    slot.model = nullptr;
    slot.mtxCalc = nullptr;
    slot.opaque = false;
    slot.opacityConfigured = false;
    slot.colorCallbacksInstalled = false;
}

bool modelStorageReady(const ModelSlot &slot) {
    return slot.data && slot.model &&
           slot.model->mModelData == slot.data &&
           slot.data->mMaterials && slot.data->mShapes &&
           slot.data->getMaterialNum() != 0 && slot.data->mShapeNum != 0 &&
           slot.model->mJointArray &&
           slot.model->mDrawMtxBuf[0] && slot.model->mDrawMtxBuf[1] &&
           slot.model->mNrmMtxBuf[0] && slot.model->mNrmMtxBuf[1] &&
           slot.model->mMatPackets && slot.model->mShapePackets &&
           slot.model->mVtxBuffer;
}

bool loadModel(Appearance appearance) {
    ModelSlot &slot = sSlots[appearance];
    clearLiveModel(slot);
    if (!slot.heap) return false;
    slot.heap->freeAll();
    const u32 emptyFree = slot.heap->getTotalFreeSize();
    if (emptyFree < kModelAllocationPreflight) return false;

    const void *resource = appearance == APPEARANCE_SHADOW
        ? shadowResource()
        : piantaResource();
    if (!resource ||
        (appearance == APPEARANCE_SHADOW && !gpScreenTexture)) return false;

    JKRHeap *oldHeap = JKRHeap::sCurrentHeap;
    slot.heap->becomeCurrentHeap();
    const u32 loadFlags = appearance == APPEARANCE_SHADOW
        ? kShadowLoadFlags
        : kPiantaLoadFlags;
    slot.data = J3DModelLoaderDataBase::load(resource, loadFlags);
    if (slot.data && slot.data->getJointNum() == kExpectedJointCount) {
        void *storage = slot.heap->alloc(sizeof(J3DModel), 32);
        if (storage) slot.model = new (storage) J3DModel(slot.data, 0, 1);
    }
    if (slot.model && !modelStorageReady(slot)) slot.model = nullptr;
    if (slot.model) {
        const u16 initialId = gpMarioOriginal &&
                                      gpMarioOriginal->mAnimationID <=
                                          SUSAMUNE_GHOST_ANIMATION_ID_MAX
            ? gpMarioOriginal->mAnimationID
            : 0;
        J3DAnmTransform *initial = marioAnimation(initialId, false);
        if (!initial && initialId != 0) initial = marioAnimation(0, false);
        if (initial) {
            slot.mtxCalc = J3DNewMtxCalcAnm(slot.data->_C & 0xfu, initial);
        }
        if (!slot.mtxCalc) slot.model = nullptr;
    }
    if (slot.model && appearance == APPEARANCE_SHADOW &&
        !gpScreenTexture->replace(slot.data, kShadowTextureName)) {
        slot.model = nullptr;
    }
    if (slot.model && !configureOpacity(slot, ghostAlpha() == 255))
        slot.model = nullptr;
    if (slot.model && !installColorCallbacks(slot))
        slot.model = nullptr;
    if (slot.model && appearance == APPEARANCE_PIANTA &&
        !validBmd(resource, SUSAMUNE_GHOST_PIANTA_BMD_SIZE,
                  SUSAMUNE_GHOST_PIANTA_BMD_CRC32)) {
        slot.model = nullptr;
    }

    const u32 free = slot.heap->getTotalFreeSize();
    const u32 used = emptyFree >= free ? emptyFree - free : 0;
    restoreHeap(oldHeap);

    if (!slot.model || used > kModelAllocationPreflight) {
        clearLiveModel(slot);
        return false;
    }
    return true;
}

Appearance selectedAppearance() {
    return gSettings.get(SETTING_GHOST_APPEARANCE) == 1
        ? APPEARANCE_PIANTA : APPEARANCE_SHADOW;
}

ModelSlot &runnerSlot(int runner) {
    const int selected = static_cast<int>(selectedAppearance());
    return sSlots[selected ^ (runner != 0 ? 1 : 0)];
}

bool prepareRunner(int runner, const Ghost::VisualState &state) {
    ModelSlot &slot = runnerSlot(runner);
    if (!slot.model || !slot.mtxCalc || !state.visible ||
        !gSettings.getBool(SETTING_GHOST_DISPLAY) ||
        (gMenu && gMenu->shown())) return false;

    J3DAnmTransform *animation =
        marioAnimation(state.animationId, state.yoshi != 0);
    J3DAnmTransform **animationPtr = animationSlot(slot);
    J3DMtxCalc **rootCalc = rootMtxCalcSlot(slot);
    const s16 frameMax = animationFrameMax(animation);
    f32 *frame = animationFrame(animation);
    if (!animation || !animationPtr || !rootCalc || frameMax <= 0 || !frame)
        return false;

    Mtx yaw;
    MTXRotRad(yaw, 'y', static_cast<f32>(state.yaw) * kAngleToRadians);
    yaw[0][3] = state.x;
    yaw[1][3] = state.y;
    yaw[2][3] = state.z;
    MTXCopy(yaw, *slot.model->getBaseTRMtx());

    const f32 savedFrame = *frame;
    J3DMtxCalc *savedCalc = *rootCalc;
    *animationPtr = animation;
    *frame = static_cast<f32>(state.animationPhase) * frameMax /
             static_cast<f32>(SUSAMUNE_GHOST_ANIMATION_PHASE_MAX + 1u);
    *rootCalc = slot.mtxCalc;
    slot.model->J3DModel::calc();
    *rootCalc = savedCalc;
    *frame = savedFrame;
    return true;
}

bool modelPacketsReady(const ModelSlot &slot) {
    if (!modelStorageReady(slot)) return false;
    for (u16 i = 0; i < slot.data->mShapeNum; ++i) {
        const u8 *packet =
            reinterpret_cast<const u8 *>(slot.model->mShapePackets) +
            static_cast<u32>(i) * kShapePacketStride;
        if (*reinterpret_cast<J3DShape *const *>(
                packet + kShapePacketShapeOffset) != slot.data->mShapes[i] ||
            *reinterpret_cast<Mtx *const *const *>(
                packet + kShapePacketDrawMtxOffset) !=
                slot.model->mDrawMtxBuf[1] ||
            *reinterpret_cast<Mtx33 *const *const *>(
                packet + kShapePacketNrmMtxOffset) !=
                slot.model->mNrmMtxBuf[1] ||
            *reinterpret_cast<u32 *const *>(
                packet + kShapePacketViewNoOffset) !=
                &slot.model->mCurrentViewNo) {
            return false;
        }
    }
    return true;
}

void setAttachmentColor(AttachmentModel &attachment,
                        const Ghost::VisualState &state) {
    attachment.packetState.channelColor.r = 255;
    attachment.packetState.channelColor.g = 255;
    attachment.packetState.channelColor.b = 255;
    attachment.packetState.channelColor.a = sGhostColor.a;
    attachment.packetState.tintTevColor =
        attachment.kind == ATTACHMENT_YOSHI;
    if (!attachment.packetState.tintTevColor) return;
    u8 color = state.yoshi;
    if (color < SUSAMUNE_GHOST_V4_YOSHI_GREEN ||
        color > SUSAMUNE_GHOST_V4_YOSHI_PINK) {
        color = SUSAMUNE_GHOST_V4_YOSHI_GREEN;
    }
    const JUtility::TColor &body = bodyColor[color - 1u];
    attachment.packetState.tevColor.r = body.r;
    attachment.packetState.tevColor.g = body.g;
    attachment.packetState.tevColor.b = body.b;
    attachment.packetState.tevColor.a = sGhostColor.a;
}

AttachmentModel *prepareAttachment(int runner,
                                   const AttachmentRequest &request,
                                   const Ghost::VisualState &state) {
    AttachmentModel *attachment = &sAttachmentModels[runner];
    J3DModel *source = request.source;
    if (!source || request.kind == ATTACHMENT_NONE ||
        attachment->kind != request.kind ||
        attachment->data != source->mModelData || !attachment->model) {
        return nullptr;
    }
    setAttachmentColor(*attachment, state);
    attachment->model->mBaseScale = source->mBaseScale;

    if (request.kind == ATTACHMENT_YOSHI) {
        Mtx yaw;
        MTXRotRad(yaw, 'y', static_cast<f32>(state.yaw) * kAngleToRadians);
        yaw[0][3] = state.x;
        yaw[1][3] = state.y;
        yaw[2][3] = state.z;
        MTXCopy(yaw, *attachment->model->getBaseTRMtx());
    } else {
        ModelSlot &mario = runnerSlot(runner);
        if (!mario.data || !mario.model || !mario.data->mJointNames)
            return nullptr;
        const s32 hand = mario.data->mJointNames->getIndex("jnt_hand_R");
        if (hand < 0 || hand >= mario.data->getJointNum()) return nullptr;
        MTXCopy(*mario.model->getAnmMtx(hand),
                *attachment->model->getBaseTRMtx());
    }
    attachment->model->J3DModel::calc();
    attachment->model->J3DModel::viewCalc();
    return attachment;
}

void entryAttachment(AttachmentModel &attachment) {
    if (!attachment.model || !attachment.data) return;
    u32 savedShapeFlags[kYoshiMaxShapeCount];
    const u16 shapeCount = attachment.kind == ATTACHMENT_YOSHI
        ? attachment.data->mShapeNum : 0;
    for (u16 i = 0; i < shapeCount; ++i) {
        J3DShape *shape = attachment.data->mShapes[i];
        savedShapeFlags[i] = shape ? shape->_8 : 0;
        // An unhatched live Yoshi hides the shared model-data shapes.
        if (shape) shape->_8 &= ~1u;
    }

    u32 savedModes[kYoshiMaxMaterialCount];
    const u16 count = attachment.data->getMaterialNum();
    if (sGhostColor.a != 255) {
        for (u16 i = 0; i < count; ++i) {
            u32 *mode = reinterpret_cast<u32 *>(
                reinterpret_cast<u8 *>(attachment.data->mMaterials[i]) +
                0x08);
            savedModes[i] = *mode;
            *mode = (*mode & ~7u) | 4u;
        }
    }

    // recursiveEntry consumes both shared routes synchronously.
    attachment.model->J3DModel::entry();
    if (sGhostColor.a != 255) {
        for (u16 i = 0; i < count; ++i) {
            u32 *mode = reinterpret_cast<u32 *>(
                reinterpret_cast<u8 *>(attachment.data->mMaterials[i]) +
                0x08);
            *mode = savedModes[i];
        }
    }
    for (u16 i = 0; i < shapeCount; ++i) {
        J3DShape *shape = attachment.data->mShapes[i];
        if (shape) shape->_8 = savedShapeFlags[i];
    }
}

class GhostView : public JDrama::TViewObj {
public:
    GhostView() : JDrama::TViewObj("Moonshine Ghost Models") {}

    virtual void perform(u32 cue, JDrama::TGraphics *) override {
        if (!sRegistered) return;
        if ((cue & (kCueCalcView | kCueEntry)) == 0) return;
        if (cue & kCueCalcView) {
            Ghost::prepareVisual();
            sGhostColor.a = ghostAlpha();
            AttachmentRequest requests[2] = {
                {ATTACHMENT_NONE, nullptr},
                {ATTACHMENT_NONE, nullptr},
            };
            for (int appearance = 0; appearance < APPEARANCE_COUNT;
                 ++appearance) {
                ModelSlot &slot = sSlots[appearance];
                if (slot.model &&
                    !configureOpacity(slot, sGhostColor.a == 255)) {
                    clearLiveModel(slot);
                }
            }
            for (int runner = 0; runner < 2; ++runner) {
                sHaveVisualState[runner] = runner == 0
                    ? Ghost::visualState(&sVisualStates[runner])
                    : Ghost::secondaryVisualState(&sVisualStates[runner]);
                sPrepared[runner] = sHaveVisualState[runner] &&
                    prepareRunner(runner, sVisualStates[runner]);
                if (!sPrepared[runner]) continue;
                ModelSlot &slot = runnerSlot(runner);
                slot.model->J3DModel::viewCalc();
                sPrepared[runner] = modelPacketsReady(slot);
                if (!sPrepared[runner]) continue;
                requests[runner] = desiredAttachment(sVisualStates[runner]);
            }
            rebuildAttachments(requests);
            for (int runner = 0; runner < 2; ++runner) {
                sPreparedAttachments[runner] = sPrepared[runner]
                    ? prepareAttachment(runner, requests[runner],
                                        sVisualStates[runner])
                    : nullptr;
            }
        }
        if (cue & kCueEntry) {
            for (int runner = 0; runner < 2; ++runner) {
                ModelSlot &slot = runnerSlot(runner);
                if (!sPrepared[runner] || !modelPacketsReady(slot)) continue;
                slot.model->J3DModel::entry();
                AttachmentModel *attachment = sPreparedAttachments[runner];
                if (attachment && attachment->model)
                    entryAttachment(*attachment);
                sSubmitted[runner] = true;
            }
        }
    }
};

alignas(32) u8 sViewStorage[sizeof(GhostView)];
GhostView *sView;

bool registerView(TMarDirector *director) {
    static const char kPlayerGroup[] =
        "\x83\x76\x83\x8C\x81\x5B\x83\x84\x81\x5B"
        "\x83\x4F\x83\x8B\x81\x5B\x83\x76";
    JDrama::TNameRef *ref = director->mViewObjRoot->search(kPlayerGroup);
    if (!ref) return false;
    JDrama::TViewObjPtrListT<JDrama::TViewObj> *group =
        reinterpret_cast<JDrama::TViewObjPtrListT<JDrama::TViewObj> *>(ref);
    for (JGadget::TList_pointer_void::iterator it = group->mViewObjList.begin();
         it != group->mViewObjList.end(); ++it) {
        if (*it == sView) return true;
    }
    JKRHeap *nodeHeap = JKRHeap::sCurrentHeap;
    if (!nodeHeap || nodeHeap->getFreeSize() < kRegistrationMinFree)
        return false;
    const u32 oldSize = group->mViewObjList.size();
    group->mViewObjList.push_back(sView);
    return group->mViewObjList.size() == oldSize + 1;
}

bool retirePlayerDrawBuffers() {
    if (!gpMarioOriginal || !gpMarioOriginal->mDrawBufferA ||
        !gpMarioOriginal->mDrawBufferB) {
        return false;
    }
    gpMarioOriginal->mDrawBufferA->frameInit();
    gpMarioOriginal->mDrawBufferB->frameInit();
    return true;
}

void loadPendingStage() {
    const u32 generation = sPendingGeneration;
    if (generation == sLoadedGeneration) return;
    if (generation != sQuiescedGeneration) {
        // Let Mario retire the previous stage's queued packets first.
        sRegistered = false;
        sQuiescedGeneration = generation;
        return;
    }

    TMarDirector *director = sPendingDirector;
    if (!director || director != gpMarDirector || !director->mViewObjRoot ||
        director->_260 == 0 || generation != sPendingGeneration) {
        return;
    }

    if (!gpMarioOriginal || !gpMarioOriginal->mDrawBufferA ||
        !gpMarioOriginal->mDrawBufferB) {
        return;
    }
    GXDrawDone();
    if (!retirePlayerDrawBuffers()) return;
    sSubmitted[0] = sSubmitted[1] = false;
    sPrepared[0] = sPrepared[1] = false;
    memset(sPreparedAttachments, 0, sizeof(sPreparedAttachments));
    memset(sAttachmentModels, 0, sizeof(sAttachmentModels));
    if (sAttachmentHeap) sAttachmentHeap->freeAll();
    clearLiveModel(sSlots[APPEARANCE_SHADOW]);
    clearLiveModel(sSlots[APPEARANCE_PIANTA]);
    const bool shadow = loadModel(APPEARANCE_SHADOW);
    const bool pianta = loadModel(APPEARANCE_PIANTA);
    const bool registered =
        (shadow || pianta) && registerView(director) &&
        generation == sPendingGeneration;
    if (!registered) {
        clearLiveModel(sSlots[APPEARANCE_SHADOW]);
        clearLiveModel(sSlots[APPEARANCE_PIANTA]);
    }
    if (generation == sPendingGeneration) {
        sRegistered = registered;
        sLoadedGeneration = generation;
    }
}

}  // namespace

void init() {
    memset(sSlots, 0, sizeof(sSlots));
    memset(sAttachmentModels, 0, sizeof(sAttachmentModels));
    memset(sPreparedAttachments, 0, sizeof(sPreparedAttachments));
    sRegistered = false;
    sPrepared[0] = sPrepared[1] = false;
    sSubmitted[0] = sSubmitted[1] = false;
    sPendingDirector = nullptr;
    sPendingGeneration = 0;
    sQuiescedGeneration = 0;
    sLoadedGeneration = 0;
    sView = new (sViewStorage) GhostView();
    JKRHeap *oldHeap = JKRHeap::sCurrentHeap;
    sSlots[APPEARANCE_SHADOW].heap = JKRExpHeap::create(
        reinterpret_cast<void *>(SUSAMUNE_GHOST_MODEL_HEAP_PPC_BASE),
        SUSAMUNE_GHOST_MODEL_HEAP_SIZE, JKRHeap::sRootHeap, false);
    sSlots[APPEARANCE_PIANTA].heap = JKRExpHeap::create(
        reinterpret_cast<void *>(SUSAMUNE_GHOST_SECONDARY_HEAP_PPC_BASE),
        SUSAMUNE_GHOST_SECONDARY_HEAP_SIZE, JKRHeap::sRootHeap, false);
    sAttachmentHeap = JKRExpHeap::create(
        reinterpret_cast<void *>(SUSAMUNE_ADDR_MOD_ATTACHMENT_HEAP),
        SUSAMUNE_MOD_ATTACHMENT_HEAP_SIZE, JKRHeap::sRootHeap, false);
    restoreHeap(oldHeap);
}

void beginFrame() {
    sSubmitted[0] = sSubmitted[1] = false;
    sPrepared[0] = sPrepared[1] = false;
    memset(sPreparedAttachments, 0, sizeof(sPreparedAttachments));
    loadPendingStage();
}

void beforeStageSetup() {
    // Retail never clears this stage-local singleton.
    gpScreenTexture = nullptr;
}

void onStageSetup(TMarDirector *director) {
    // setupObjects runs off-thread. The render thread retires old packets.
    sPendingDirector = director;
    u32 generation = sPendingGeneration + 1u;
    if (generation == 0) generation = 1;
    sPendingGeneration = generation;
}

bool available() {
    return sRegistered && (sSlots[APPEARANCE_SHADOW].model ||
                           sSlots[APPEARANCE_PIANTA].model);
}

bool submitted(bool secondary) {
    return sSubmitted[secondary ? 1 : 0];
}

}  // namespace GhostModel
