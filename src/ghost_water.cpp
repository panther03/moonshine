#include "susamune/ghost_fludd.hxx"
#include "susamune/ghost.hxx"
#include "susamune/ghost_model.hxx"
#include "susamune/mem2_map.h"
#include "susamune/settings.hxx"
#include "susamune/menu.hxx"
#include "susamune/retail_input.hxx"
#include "SMS/Map/Map.hxx"
#include "SMS/Camera/PolarSubCamera.hxx"
#include "SMS/Player/Mario.hxx"
#include "SMS/Player/Watergun.hxx"
#include "Dolphin/GX.h"
#include "Dolphin/MTX.h"
#include "Dolphin/mem.h"
#include "JSystem/JUtility/JUTTexture.hxx"

extern "C" void *gpModelWaterManager;
extern "C" void GXGetProjectionv(f32 *projection);
extern "C" GXColor gModelWaterManagerWaterColor[4];
extern "C" f32 ghostSquareRoot(f32) asm("sqrtf__3stdFf");

#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata" data=".foxtrot.data" bss=".foxtrot.bss"

namespace GhostFludd {
namespace {
const unsigned kDrops = 64;
struct Drop { TVec3f position, velocity; f32 life; bool splash; u8 type, size; };
struct Water { Drop drops[kDrops]; u32 qf, token, next; bool ready; TVec3f previous; };
struct Storage { Water runner[2]; };
static_assert(sizeof(Storage) <= SUSAMUNE_GHOST_WATER_SIZE, "ghost water exceeds its scalar tail");
static_assert(0x2000u + SUSAMUNE_GHOST_WATER_SIZE <= SUSAMUNE_PRESENTATION_SIZE, "ghost water tail overlap");
#define sWater (*reinterpret_cast<Storage *>(SUSAMUNE_GHOST_WATER_PPC_BASE))
TMarDirector *sDirector;

void vertex(f32 x, f32 y, f32 z, u32 color, f32 s, f32 t) {
    volatile f32 *fifo = reinterpret_cast<volatile f32 *>(0xCC008000);
    *fifo = x; *fifo = y; *fifo = z;
    *reinterpret_cast<volatile u32 *>(0xCC008000) = color;
    *fifo = s; *fifo = t;
}

f32 parameter(u8 type, unsigned offset, f32 fallback) {
    const u8 *manager = static_cast<const u8 *>(gpModelWaterManager);
    const u8 *params = type < 17 ? *reinterpret_cast<u8 *const *>(manager + 0x5dbc + type * 4) : nullptr;
    return params ? *reinterpret_cast<const f32 *>(params + offset) : fallback;
}

void advance(Water &water, const Ghost::VisualState &state, unsigned runner) {
    if (!water.ready || water.token != state.recordingToken ||
        state.visualQf < water.qf || state.visualQf - water.qf > 24) {
        memset(&water, 0, sizeof(water));
        water.qf = state.visualQf;
        water.token = state.recordingToken;
        water.ready = true;
        water.previous.set(state.x, state.y, state.z);
    }
    // Retail water move runs once per quarter-frame, not once per drawn frame.
    const f32 dt = state.visualQf - water.qf;
    if (dt <= 0) return;
    water.qf = state.visualQf;
    for (unsigned i = 0; i < kDrops; ++i) {
        Drop &drop = water.drops[i];
        if (drop.life <= 0) continue;
        drop.life -= dt;
        if (drop.splash) continue;
        const f32 oldY = drop.position.y;
        drop.position.x += drop.velocity.x * dt;
        const f32 gravity = parameter(drop.type, 0x54, -0.4f);
        drop.position.y += drop.velocity.y * dt + gravity * dt * (dt + 1) * 0.5f;
        drop.position.z += drop.velocity.z * dt;
        drop.velocity.y += gravity * dt;
        if (gpMap) {
            const TBGCheckData *ground;
            const f32 floor = gpMap->checkGround(drop.position.x, oldY + 30,
                                                drop.position.z, &ground);
            if (ground && floor > -30000 && drop.position.y <= floor) {
                drop.position.y = floor + 3;
                drop.splash = true;
                drop.life = 24;
            }
        }
    }
    const f32 inheritedX = (state.x - water.previous.x) / dt * 0.125f;
    const f32 inheritedZ = (state.z - water.previous.z) / dt * 0.125f;
    water.previous.set(state.x, state.y, state.z);
    if (!(state.fludd.mode & SUSAMUNE_GHOST_FLUDD_SPRAYING)) return;
    TVec3f aim;
    direction(state.fludd, aim);
    const f32 power = state.fludd.power;
    const unsigned nozzle = state.fludd.mode & 7u;
    const TNozzleBase *source = nozzle < 6 && gpMarioOriginal && gpMarioOriginal->mFludd
        ? gpMarioOriginal->mFludd->mNozzleList[nozzle] : nullptr;
    for (unsigned i = 0; i < 4; ++i) {
        Drop &drop = water.drops[water.next++ % kDrops];
        // Local deterministic scatter only: never consume Sunshine's RNG or
        // call its water emitter, collision responses, sound, or goop logic.
        u32 seed = state.visualQf * 1664525u + i * 1013904223u;
        drop.position.set(state.x + state.fludd.offset[0] * 2,
                          state.y + state.fludd.offset[1] * 2,
                          state.z + state.fludd.offset[2] * 2);
        // Use the rendered muzzle. Both hover outlets share the recorded aim;
        // no additional emission or gameplay actor is created.
        GhostModel::emissionPoint(runner, nozzle, i, drop.position);
        drop.velocity.set(aim.x * power + inheritedX + ((s32)(seed & 255) - 127) * 0.016f,
                          aim.y * power + ((s32)((seed >> 8) & 255) - 127) * 0.016f,
                          aim.z * power + inheritedZ + ((s32)((seed >> 16) & 255) - 127) * 0.016f);
        drop.type = source ? source->mEmitParams.mType.get() : 0;
        const f32 size = source ? source->mEmitParams.mSize.get() : 50;
        drop.size = size > 255 ? 255 : size > 0 ? (u8)size : 50;
        const f32 age = dt * i * 0.25f;
        const f32 gravity = parameter(drop.type, 0x54, -0.4f);
        drop.position.x += drop.velocity.x * age;
        drop.position.y += drop.velocity.y * age + gravity * age * (age + 1) * 0.5f;
        drop.position.z += drop.velocity.z * age;
        drop.velocity.y += gravity * age;
        drop.life = parameter(drop.type, 0x68, 255) - age;
        drop.splash = false;
    }
}

void setup(bool highlight) {
    Mtx identity;
    MTXIdentity(identity);
    // The late water pass can receive a reused TGraphics. Use the same
    // projection cache as retail camera perform, including free-camera edits.
    GXSetProjection(*reinterpret_cast<Mtx44 *>(reinterpret_cast<u8 *>(gpCamera) + 0x16c), GX_PERSPECTIVE);
    GXLoadPosMtxImm(identity, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0,
                     GX_IDENTITY, GX_FALSE, 0x7d);
    GXSetNumIndStages(0);
    GXSetNumTevStages(2);
    GXSetTevDirect(GX_TEVSTAGE0);
    GXSetTevDirect(GX_TEVSTAGE1);
    // Borrow retail's immutable water mask and highlight; never add particles
    // to its gameplay pool or mutate its shared quad buffer.
    u8 *manager = static_cast<u8 *>(gpModelWaterManager);
    (*reinterpret_cast<JUTTexture **>(manager + 0x5d3c))->load(GX_TEXMAP0);
    (*reinterpret_cast<JUTTexture **>(manager + 0x5d40))->load(GX_TEXMAP1);
    GXSetTevColor(GX_TEVREG0, *reinterpret_cast<GXColor *>(manager + 0x5d20));
    GXSetTevColor(GX_TEVREG1, highlight
        ? *reinterpret_cast<GXColor *>(manager + 0x5d24)
        : gModelWaterManagerWaterColor[0]);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP1, GX_COLOR0A0);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO,
        highlight ? GX_CC_C0 : GX_CC_ZERO, GX_CC_TEXC, GX_CC_C1);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    // Retail draws a faint blue base, then adds the specular highlight at its
    // own coverage. Multiplying both by the base alpha makes the spray dark.
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_RASA,
        highlight ? GX_CA_TEXA : GX_CA_A1, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_APREV, GX_CA_TEXA, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA,
        highlight ? GX_BL_ONE : GX_BL_INVSRCALPHA, GX_LO_COPY);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);
}
}

void draw(JDrama::TGraphics *graphics) {
    TMarDirector *director = RetailInput::stageDirector();
    if (sDirector != director) {
        memset(&sWater, 0, sizeof(sWater));
        sDirector = director;
    }
    if (!graphics || !director || !gpCamera || !gpModelWaterManager ||
        (gMenu && gMenu->shown())) return;
    const u8 *manager = static_cast<const u8 *>(gpModelWaterManager);
    if (!*reinterpret_cast<JUTTexture *const *>(manager + 0x5d3c) ||
        !*reinterpret_cast<JUTTexture *const *>(manager + 0x5d40)) return;
    Ghost::prepareVisual();
    bool anyDraw = false;
    f32 savedProjection[7];
    for (unsigned pass = 0; pass < 2; ++pass) {
        bool configured = false;
        for (unsigned runner = 0; runner < 2; ++runner) {
            Ghost::VisualState state;
            const bool visible = runner ? Ghost::secondaryVisualState(&state) : Ghost::visualState(&state);
            Water &water = sWater.runner[runner];
            // The water pass can precede the player's model-entry cue. Availability
            // must not depend on a submitted latch that is cleared each frame.
            if (!visible || !state.visible || !GhostModel::available() ||
                !gSettings.getBool(SETTING_GHOST_DISPLAY) ||
                !(state.fludd.mode & SUSAMUNE_GHOST_FLUDD_PRESENT)) {
                water.ready = false;
                continue;
            }
            advance(water, state, runner);
            for (unsigned i = 0; i < kDrops; ++i) {
                const Drop &drop = water.drops[i];
                if (drop.life <= 0) continue;
                if (!configured) {
                    if (!anyDraw) GXGetProjectionv(savedProjection);
                    setup(pass != 0);
                    configured = true;
                    anyDraw = true;
                }
                TVec3f camera;
                // Retail preserves the active water view here during cue 8.
                const Mtx &view = *reinterpret_cast<Mtx *>(reinterpret_cast<u8 *>(gpModelWaterManager) + 0x5e10);
                // Scalar MEM2 data never enters retail paired-single helpers.
                const TVec3f &p = drop.position;
                camera.set(view[0][0] * p.x + view[0][1] * p.y + view[0][2] * p.z + view[0][3],
                           view[1][0] * p.x + view[1][1] * p.y + view[1][2] * p.z + view[1][3],
                           view[2][0] * p.x + view[2][1] * p.y + view[2][2] * p.z + view[2][3]);
                f32 x = drop.splash ? (32 - drop.life) * 1.25f : drop.size * 0.707f;
                f32 y = drop.splash ? x * 0.3f : x;
                f32 stretchX = 0, stretchY = 0;
                f32 crossX = -x, crossY = y;
                if (!drop.splash) {
                    const TVec3f &v = drop.velocity;
                    const f32 extension = parameter(drop.type, 0x18, 0.7f);
                    const f32 vx = (view[0][0] * v.x + view[0][1] * v.y + view[0][2] * v.z) * extension;
                    const f32 vy = (view[1][0] * v.x + view[1][1] * v.y + view[1][2] * v.z) * extension;
                    const f32 length = ghostSquareRoot(vx * vx + vy * vy);
                    if (length > 1) {
                        // Same velocity-oriented diamond as retail calcDrawVtx.
                        y = vy * x / length;
                        x = vx * x / length;
                        crossX = y; crossY = -x;
                        const f32 stretch = *reinterpret_cast<const f32 *>(manager + 0x5d18);
                        stretchX = vx * stretch; stretchY = vy * stretch;
                    }
                }
                const u8 opacity[] = {64, 128, 192, 255};
                const u8 choice = gSettings.get(SETTING_GHOST_OPACITY);
                const u32 alpha = (u32)((drop.life < 4 ? drop.life * 0.25f : 1) *
                    opacity[choice < 4 ? choice : 1]);
                GXBegin(GX_QUADS, GX_VTXFMT0, 4);
                const u32 color = 0xffffff00u | alpha;
                vertex(camera.x + x + stretchX, camera.y + y + stretchY, camera.z, color, 0, 0);
                vertex(camera.x + crossX, camera.y + crossY, camera.z, color, 1, 0);
                vertex(camera.x - x - stretchX, camera.y - y - stretchY, camera.z, color, 1, 1);
                vertex(camera.x - crossX, camera.y - crossY, camera.z, color, 0, 1);
            }
        }
    }
    // Later HUD consumers can reuse the active projection without setting it.
    if (anyDraw) {
        Mtx44 projection = {};
        const u8 type = savedProjection[0] == 0 ? GX_PERSPECTIVE : GX_ORTHOGRAPHIC;
        projection[0][0] = savedProjection[1];
        projection[0][type == GX_PERSPECTIVE ? 2 : 3] = savedProjection[2];
        projection[1][1] = savedProjection[3];
        projection[1][type == GX_PERSPECTIVE ? 2 : 3] = savedProjection[4];
        projection[2][2] = savedProjection[5];
        projection[2][3] = savedProjection[6];
        GXSetProjection(projection, type);
    }
}
}
