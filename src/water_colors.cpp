#include "susamune/water_colors.hxx"
#include "susamune/fludd_colors.hxx"
#include "susamune/ghost_fludd.hxx"
#include "susamune/retail_input.hxx"

#include "Dolphin/GX.h"
#include "Dolphin/mem.h"
#include "SMS/Player/Mario.hxx"
#include "SMS/Player/Watergun.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"

#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata" data=".foxtrot.data" bss=".foxtrot.bss"

extern "C" {
extern void *gpModelWaterManager;
extern void *gpSplashManager;
extern void *gpMarioParticleManager;
extern GXColor gModelWaterManagerWaterColor[4];
extern void *waterVtable[] asm("__vt__18TModelWaterManager");
extern void *splashVtable[] asm("__vt__14TSplashManager");
extern void *particleVtable[] asm("__vt__21TMarioParticleManager");
void retailWater(void *, u32, JDrama::TGraphics *)
    asm("perform__18TModelWaterManagerFUlPQ26JDrama9TGraphics");
void retailSplash(void *, u32, JDrama::TGraphics *)
    asm("perform__14TSplashManagerFUlPQ26JDrama9TGraphics");
void retailParticles(void *, u32, JDrama::TGraphics *)
    asm("perform__21TMarioParticleManagerFUlPQ26JDrama9TGraphics");
}

namespace WaterColors {
namespace {
typedef void (*Perform)(void *, u32, JDrama::TGraphics *);
TMarDirector *sDirector;
const u32 kDraw = 8;
const u32 kWaterDraw = kDraw | 0x80;
const u32 kMistCount = 32;

bool mem1(const void *pointer, u32 size) {
    const u32 address = reinterpret_cast<u32>(pointer);
    return address >= 0x80000000u && address < 0x81800000u &&
           size <= 0x81800000u - address;
}
bool live() {
    return gpMarDirector && RetailInput::stageDirector() == gpMarDirector &&
        gpMarDirector == sDirector && gpMarDirector->_260 &&
        gpMarDirector->mCurState >= TMarDirector::STATE_GAME_STARTING;
}
GXColor &color(void *owner, u32 offset) {
    return *reinterpret_cast<GXColor *>(static_cast<u8 *>(owner) + offset);
}
void *pointerAt(const void *owner, u32 offset) {
    return reinterpret_cast<void *>(
        *reinterpret_cast<const u32 *>(static_cast<const u8 *>(owner) + offset));
}
void replace(GXColor &destination, unsigned part, bool keepBrightness = false) {
    if (!FluddColors::enabled(part)) return;
    const u8 *rgb = FluddColors::rgb(part);
    u32 brightness = 255;
    if (keepBrightness) {
        brightness = destination.r > destination.g ? destination.r : destination.g;
        if (destination.b > brightness) brightness = destination.b;
    }
    destination.r = (rgb[0] * brightness + 127) / 255;
    destination.g = (rgb[1] * brightness + 127) / 255;
    destination.b = (rgb[2] * brightness + 127) / 255;
}
bool custom() {
    return FluddColors::enabled(FluddColors::WATER) ||
           FluddColors::enabled(FluddColors::WATER_HIGHLIGHT);
}
bool normalWater() {
    return mem1(gpModelWaterManager, 0x5d60) &&
        static_cast<const u8 *>(gpModelWaterManager)[0x5d5f] == 0;
}

void drawWater(void *self, u32 cue, JDrama::TGraphics *graphics) {
    if (!live() || self != gpModelWaterManager || !normalWater() ||
        !(cue & kWaterDraw) || !custom()) {
        retailWater(self, cue, graphics);
    } else {
        // Mixed cues finish gameplay before lending the renderer its palette.
        if (cue & ~kWaterDraw) retailWater(self, cue & ~kWaterDraw, graphics);
        const GXColor base = gModelWaterManagerWaterColor[0];
        const GXColor shine = color(self, 0x5d20), shade = color(self, 0x5d24);
        replace(gModelWaterManagerWaterColor[0], FluddColors::WATER);
        replace(color(self, 0x5d20), FluddColors::WATER_HIGHLIGHT, true);
        replace(color(self, 0x5d24), FluddColors::WATER_HIGHLIGHT, true);
        retailWater(self, cue & kWaterDraw, graphics);
        gModelWaterManagerWaterColor[0] = base;
        color(self, 0x5d20) = shine;
        color(self, 0x5d24) = shade;
    }
    // Draw after the water-composite pass, not into its silhouette mask.
    if (live() && self == gpModelWaterManager && (cue & 0x80u)) GhostFludd::draw(graphics);
}

void drawSplash(void *self, u32 cue, JDrama::TGraphics *graphics) {
    if (!live() || self != gpSplashManager || !mem1(self, 0x640) ||
        !normalWater() || !(cue & kDraw) || !FluddColors::enabled(FluddColors::WATER)) {
        retailSplash(self, cue, graphics);
        return;
    }
    if (cue & ~kDraw) retailSplash(self, cue & ~kDraw, graphics);
    const GXColor original = color(self, 0x63c);
    replace(color(self, 0x63c), FluddColors::WATER);
    retailSplash(self, cue & kDraw, graphics);
    color(self, 0x63c) = original;
}

struct MistColor { void *emitter; GXColor primary, environment; };
void drawParticles(void *self, u32 cue, JDrama::TGraphics *graphics) {
    if (!live() || self != gpMarioParticleManager || !mem1(self, 0x3bc) ||
        !normalWater() || !(cue & kDraw) || !custom() ||
        !mem1(gpMarioAddress, sizeof(TMario)) ||
        !mem1(gpMarioAddress->mFludd, sizeof(TWaterGun))) {
        retailParticles(self, cue, graphics);
        return;
    }
    // Particle calculation can delete or replace emitters; collect after it.
    if (cue & 2) retailParticles(self, cue & ~kDraw, graphics);
    const u8 *manager = static_cast<const u8 *>(self);
    const u32 count = *reinterpret_cast<const u32 *>(manager + 0x3b4);
    const u8 *info = static_cast<const u8 *>(pointerAt(manager, 0x50));
    const void *engine = pointerAt(manager, 0x3b8);
    if (!count || count > kMistCount || !mem1(info, count * 16)) {
        retailParticles(self, cue & ~2u, graphics);
        return;
    }
    MistColor saved[kMistCount];
    u32 used = 0;
    const u32 fludd = reinterpret_cast<__UINTPTR_TYPE__>(gpMarioAddress->mFludd);
    for (u32 i = 0; i < count; ++i) {
        const u32 owner = *reinterpret_cast<const u32 *>(info + i * 16);
        void *emitter = pointerAt(info + i * 16, 12);
        // Effect 0x10D is also reusable; require one of this FLUDD's nozzle owners.
        if (owner < fludd || owner - fludd >= sizeof(TWaterGun) ||
            !mem1(emitter, 0x188) ||
            pointerAt(emitter, 0x10c) != engine)
            continue;
        bool duplicate = false;
        for (u32 j = 0; j < used; ++j) duplicate |= saved[j].emitter == emitter;
        if (duplicate) continue;
        saved[used++] = {emitter, color(emitter, 0x180), color(emitter, 0x184)};
        replace(color(emitter, 0x180), FluddColors::WATER);
        replace(color(emitter, 0x184), FluddColors::WATER_HIGHLIGHT);
    }
    retailParticles(self, cue & ~2u, graphics);
    while (used) {
        const MistColor &original = saved[--used];
        color(original.emitter, 0x180) = original.primary;
        color(original.emitter, 0x184) = original.environment;
    }
}

void install(void **vtable, Perform original, Perform replacement) {
    if (vtable[8] != reinterpret_cast<void *>(original)) return;
    vtable[8] = reinterpret_cast<void *>(replacement);
    DCStoreRange(vtable + 8, sizeof(void *));
}
}

void onStageSetup() {
    sDirector = gpMarDirector;
    // Retail .data is not snapshotted; no heap callback registry can become stale.
    install(waterVtable, retailWater, drawWater);
    install(splashVtable, retailSplash, drawSplash);
    install(particleVtable, retailParticles, drawParticles);
}
}
