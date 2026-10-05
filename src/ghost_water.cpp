#include "susamune/ghost_fludd.hxx"
#include "susamune/ghost.hxx"
#include "susamune/ghost_model.hxx"
#include "susamune/mem2_map.h"
#include "susamune/settings.hxx"
#include "susamune/menu.hxx"
#include "susamune/retail_input.hxx"
#include "SMS/Map/Map.hxx"
#include "SMS/Camera/PolarSubCamera.hxx"
#include "Dolphin/GX.h"
#include "Dolphin/MTX.h"
#include "Dolphin/mem.h"

extern "C" void *gpModelWaterManager;
extern "C" void GXGetProjectionv(f32 *projection);

#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata" data=".foxtrot.data" bss=".foxtrot.bss"

namespace GhostFludd {
namespace {
const unsigned kDrops = 64;
struct Drop { TVec3f position, velocity; f32 life; bool splash; };
struct Water { Drop drops[kDrops]; u32 qf, token, next; bool ready; };
struct Storage { Water runner[2]; };
static_assert(sizeof(Storage) <= SUSAMUNE_GHOST_WATER_SIZE, "ghost water exceeds its scalar tail");
static_assert(0x2000u + SUSAMUNE_GHOST_WATER_SIZE <= SUSAMUNE_PRESENTATION_SIZE, "ghost water tail overlap");
#define sWater (*reinterpret_cast<Storage *>(SUSAMUNE_GHOST_WATER_PPC_BASE))
TMarDirector *sDirector;

void vertex(f32 x, f32 y, f32 z, u32 color) {
    volatile f32 *fifo = reinterpret_cast<volatile f32 *>(0xCC008000);
    *fifo = x; *fifo = y; *fifo = z;
    *reinterpret_cast<volatile u32 *>(0xCC008000) = color;
}

void advance(Water &water, const Ghost::VisualState &state) {
    if (!water.ready || water.token != state.recordingToken ||
        state.visualQf < water.qf || state.visualQf - water.qf > 24) {
        memset(&water, 0, sizeof(water));
        water.qf = state.visualQf;
        water.token = state.recordingToken;
        water.ready = true;
    }
    const f32 dt = (state.visualQf - water.qf) * 0.25f;
    if (dt <= 0) return;
    water.qf = state.visualQf;
    for (unsigned i = 0; i < kDrops; ++i) {
        Drop &drop = water.drops[i];
        if (drop.life <= 0) continue;
        drop.life -= dt;
        if (drop.splash) continue;
        const f32 oldY = drop.position.y;
        drop.position.x += drop.velocity.x * dt;
        drop.position.y += drop.velocity.y * dt;
        drop.position.z += drop.velocity.z * dt;
        drop.velocity.y -= 0.65f * dt;
        if (gpMap) {
            const TBGCheckData *ground;
            const f32 floor = gpMap->checkGround(drop.position.x, oldY + 30,
                                                drop.position.z, &ground);
            if (ground && floor > -30000 && drop.position.y <= floor) {
                drop.position.y = floor + 3;
                drop.splash = true;
                drop.life = 6;
            }
        }
    }
    if (!(state.fludd.mode & SUSAMUNE_GHOST_FLUDD_SPRAYING)) return;
    TVec3f aim;
    direction(state.fludd, aim);
    const f32 power = state.fludd.power;
    for (unsigned i = 0; i < 4; ++i) {
        Drop &drop = water.drops[water.next++ % kDrops];
        // Local deterministic scatter only: never consume Sunshine's RNG or
        // call its water emitter, collision responses, sound, or goop logic.
        u32 seed = state.visualQf * 1664525u + i * 1013904223u;
        drop.position.set(state.x + state.fludd.offset[0] * 2,
                          state.y + state.fludd.offset[1] * 2,
                          state.z + state.fludd.offset[2] * 2);
        drop.velocity.set(aim.x * power + ((s32)(seed & 255) - 127) * 0.016f,
                          aim.y * power + ((s32)((seed >> 8) & 255) - 127) * 0.016f,
                          aim.z * power + ((s32)((seed >> 16) & 255) - 127) * 0.016f);
        const f32 age = dt * i * 0.25f;
        drop.position.x += drop.velocity.x * age;
        drop.position.y += drop.velocity.y * age;
        drop.position.z += drop.velocity.z * age;
        drop.life = 14 - age;
        drop.splash = false;
    }
}

void setup() {
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
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(0);
    GXSetNumIndStages(0);
    GXSetNumTevStages(1);
    GXSetTevDirect(GX_TEVSTAGE0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_COPY);
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
    Ghost::prepareVisual();
    bool configured = false;
    f32 savedProjection[7];
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
        advance(water, state);
        for (unsigned i = 0; i < kDrops; ++i) {
            const Drop &drop = water.drops[i];
            if (drop.life <= 0) continue;
            if (!configured) {
                GXGetProjectionv(savedProjection);
                setup();
                configured = true;
            }
            TVec3f camera;
            // Retail preserves the active water view here during cue 8.
            const Mtx &view = *reinterpret_cast<Mtx *>(reinterpret_cast<u8 *>(gpModelWaterManager) + 0x5e10);
            // Scalar MEM2 data never enters retail paired-single helpers.
            const TVec3f &p = drop.position;
            camera.set(view[0][0] * p.x + view[0][1] * p.y + view[0][2] * p.z + view[0][3],
                       view[1][0] * p.x + view[1][1] * p.y + view[1][2] * p.z + view[1][3],
                       view[2][0] * p.x + view[2][1] * p.y + view[2][2] * p.z + view[2][3]);
            const f32 radius = drop.splash ? (8 - drop.life) * 3 : 5;
            const f32 height = drop.splash ? radius * 0.3f : radius;
            const u8 opacity[] = {64, 128, 192, 255};
            const u8 choice = gSettings.get(SETTING_GHOST_OPACITY);
            const u32 alpha = (u32)((drop.life < 4 ? drop.life * 0.25f : 1) *
                opacity[choice < 4 ? choice : 1]);
            GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, 6);
            vertex(camera.x, camera.y, camera.z, 0xc8eeff00u | alpha);
            vertex(camera.x - radius, camera.y, camera.z, 0x409fff00u);
            vertex(camera.x, camera.y + height, camera.z, 0x409fff00u);
            vertex(camera.x + radius, camera.y, camera.z, 0x409fff00u);
            vertex(camera.x, camera.y - height, camera.z, 0x409fff00u);
            vertex(camera.x - radius, camera.y, camera.z, 0x409fff00u);
        }
    }
    // Later HUD consumers can reuse the active projection without setting it.
    if (configured) {
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
