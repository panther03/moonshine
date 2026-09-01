#include "susamune/practice_visuals.hxx"

#include "Dolphin/MTX.h"
#include "Dolphin/math.h"
#include "Dolphin/string.h"
#include "SMS/Camera/PolarSubCamera.hxx"
#include "SMS/MapObj/MapObjHide.hxx"
#include "SMS/Strategic/Strategy.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/addresses.hxx"
#include "susamune/menu.hxx"
#include "susamune/settings.hxx"

namespace PracticeVisuals {
namespace {

const u32 kBossActor = 0x08000000u;
const u32 kEnemyActor = 0x10000000u;
const u32 kNoCollision = 0x00000001u;
const u32 kCannotReceive = 0x00000004u;
const u32 kHideObject = 0x20000011u;
const u32 kMapSmoke = 0x4000001fu;
const u32 kCoinFirst = 0x2000000eu;
const u32 kCoinLast = 0x20000010u;
const u32 kEelTooth = 0x08000022u;
const u32 kYoshiTongue = 0x08000083u;

struct Projection {
    Mtx view;
    f32 cot;
    f32 aspect;
    f32 nearPlane;
    f32 farPlane;
    bool valid;

    Projection() : valid(false) {
        if (!gpCamera) return;
        const f32 fovy = gpCamera->mProjectionFovy;
        aspect = gpCamera->mProjectionAspect;
        nearPlane = gpCamera->mProjectionNear;
        farPlane = gpCamera->mProjectionFar;
        if (!(fovy > 1.0f && fovy < 170.0f && aspect > 0.5f &&
              aspect < 2.0f && nearPlane > 0.0f && farPlane > nearPlane)) {
            return;
        }

        Vec cameraPos = {gpCamera->mTranslation.x, gpCamera->mTranslation.y,
                         gpCamera->mTranslation.z};
        Vec cameraUp = {gpCamera->mUpVector.x, gpCamera->mUpVector.y,
                        gpCamera->mUpVector.z};
        Vec cameraTarget = {gpCamera->mTargetPos.x, gpCamera->mTargetPos.y,
                            gpCamera->mTargetPos.z};
        C_MTXLookAt(view, &cameraPos, &cameraUp, &cameraTarget);
        cot = 1.0f / tanf(fovy * 0.00872664626f);
        valid = cot > 0.0f && cot < 100.0f;
    }

    bool point(const TVec3f &world, s16 *x, s16 *y) const {
        if (!valid || !x || !y || !(world.x > -1000000.0f) ||
            !(world.x < 1000000.0f) || !(world.y > -1000000.0f) ||
            !(world.y < 1000000.0f) || !(world.z > -1000000.0f) ||
            !(world.z < 1000000.0f)) {
            return false;
        }

        Vec source = {world.x, world.y, world.z};
        Vec viewPosition;
        PSMTXMultVec(view, &source, &viewPosition);
        const f32 depth = -viewPosition.z;
        if (!(depth >= nearPlane && depth <= farPlane)) return false;

        const f32 ndcX = viewPosition.x * cot / (aspect * depth);
        const f32 ndcY = viewPosition.y * cot / depth;
        if (!(ndcX > -4.0f && ndcX < 4.0f && ndcY > -4.0f &&
              ndcY < 4.0f)) {
            return false;
        }
        *x = static_cast<s16>((ndcX * 0.5f + 0.5f) * 640.0f);
        *y = static_cast<s16>((0.5f - ndcY * 0.5f) * 480.0f);
        return true;
    }
};

bool validVolume(const THitActor *actor) {
    if (!actor || actor->mObjectID == kYoshiTongue ||
        (actor->mObjectType & (kNoCollision | kCannotReceive))) {
        return false;
    }
    const u32 vtable = *reinterpret_cast<const u32 *>(actor);
    if (!(actor->mObjectID & (kBossActor | kEnemyActor)) &&
        vtable != SUSAMUNE_VT_POIHANA_COLLISION) {
        return false;
    }
    if (actor->mObjectID == kEelTooth &&
        *reinterpret_cast<const s32 *>(
            reinterpret_cast<const u8 *>(actor) + 0x70) <= 1) {
        return false;
    }
    return actor->mReceiveRadius > 0.0f && actor->mReceiveRadius < 50000.0f &&
           actor->mReceiveHeight > 0.0f && actor->mReceiveHeight < 50000.0f;
}

void drawSegment(Menu *menu, const s16 *a, const s16 *b,
                 const JUtility::TColor &color) {
    const s16 points[4] = {a[0], a[1], b[0], b[1]};
    menu->strokePoly(points, 2, color);
}

void drawCylinder(Menu *menu, const Projection &projection,
                  const THitActor *actor) {
    if (!validVolume(actor)) return;

    static const s8 kCircle[16] = {
        100, 0, 71, 71, 0, 100, -71, 71,
        -100, 0, -71, -71, 0, -100, 71, -71,
    };
    s16 lower[16];
    s16 upper[16];
    bool lowerValid[8];
    bool upperValid[8];
    bool allValid = true;
    for (int i = 0; i < 8; ++i) {
        TVec3f p(actor->mTranslation.x +
                     actor->mReceiveRadius * kCircle[i * 2] * 0.01f,
                 actor->mTranslation.y,
                 actor->mTranslation.z +
                     actor->mReceiveRadius * kCircle[i * 2 + 1] * 0.01f);
        lowerValid[i] =
            projection.point(p, &lower[i * 2], &lower[i * 2 + 1]);
        p.y += actor->mReceiveHeight;
        upperValid[i] =
            projection.point(p, &upper[i * 2], &upper[i * 2 + 1]);
        allValid = allValid && lowerValid[i] && upperValid[i];
    }

    const JUtility::TColor color(35, 255, 120, 220);
    if (allValid) {
        menu->strokePoly(lower, 8, color);
        menu->strokePoly(upper, 8, color);
        for (int i = 0; i < 8; i += 2)
            drawSegment(menu, &lower[i * 2], &upper[i * 2], color);
        return;
    }

    for (int i = 0; i < 8; ++i) {
        const int next = (i + 1) & 7;
        if (lowerValid[i] && lowerValid[next])
            drawSegment(menu, &lower[i * 2], &lower[next * 2], color);
        if (upperValid[i] && upperValid[next])
            drawSegment(menu, &upper[i * 2], &upper[next * 2], color);
        if ((i & 1) == 0 && lowerValid[i] && upperValid[i])
            drawSegment(menu, &lower[i * 2], &upper[i * 2], color);
    }
}

void drawGroupHurtboxes(Menu *menu, const Projection &projection,
                        TIdxGroupObj *group) {
    if (!group) return;
    for (JGadget::TList_pointer_void::iterator it =
             group->mViewObjList.begin();
         it != group->mViewObjList.end(); ++it) {
        drawCylinder(menu, projection, static_cast<THitActor *>(*it));
    }
}

const char *hiddenLabel(u32 objectId) {
    if (objectId >= kCoinFirst && objectId <= kCoinLast) return "COIN";
    switch (objectId) {
    case 0x40000390u: return "COCONUT";
    case 0x40000391u: return "PAPAYA";
    case 0x40000392u: return "PINEAPPLE";
    case 0x40000393u: return "DURIAN";
    case 0x40000394u: return "BANANA";
    case 0x40000395u: return "PEPPER";
    default: return nullptr;
    }
}

bool isSprayHideObject(const TMapObjBase *actor) {
    if (!actor || !actor->mRegisterName) return false;
    if (actor->mObjectID == kHideObject)
        return strcmp(actor->mRegisterName, "WaterHitHideObj") == 0;
    return actor->mObjectID == kMapSmoke &&
           strcmp(actor->mRegisterName, "MapSmoke") == 0;
}

void drawHiddenMarker(Menu *menu, const Projection &projection,
                      const THideObjBase *hide) {
    if (!hide || !hide->mAllowReveal || !hide->mHiddenObj ||
        (hide->mObjectType & kNoCollision)) {
        return;
    }
    const char *label = hiddenLabel(hide->mHiddenObj->mObjectID);
    if (!label) return;

    TVec3f world = hide->mTranslation;
    world.y += 55.0f;
    s16 x;
    s16 y;
    if (!projection.point(world, &x, &y)) return;

    JUtility::TColor line(255, 135, 35, 255);
    JUtility::TColor fill(125, 45, 0, 190);
    if (hide->mHiddenObj->mObjectID == 0x2000000eu) {
        line = JUtility::TColor(255, 230, 30, 255);
        fill = JUtility::TColor(130, 105, 0, 190);
    } else if (hide->mHiddenObj->mObjectID == 0x2000000fu) {
        line = JUtility::TColor(255, 70, 70, 255);
        fill = JUtility::TColor(125, 0, 0, 190);
    } else if (hide->mHiddenObj->mObjectID == 0x20000010u) {
        line = JUtility::TColor(70, 175, 255, 255);
        fill = JUtility::TColor(0, 55, 135, 190);
    }
    const s16 diamond[8] = {
        x, static_cast<s16>(y - 7), static_cast<s16>(x + 7), y,
        x, static_cast<s16>(y + 7), static_cast<s16>(x - 7), y,
    };
    menu->fillPoly(diamond, 4, fill);
    menu->strokePoly(diamond, 4, line);
    menu->drawText(label, x + 10, y - 6, 10, 10, line);
}

void drawPinnaItems(Menu *menu, const Projection &projection) {
    if (gpApplication.mCurrentScene.mAreaID !=
            TGameSequence::AREA_PINNABEACH ||
        !gpStrategy->mObjectGroup) {
        return;
    }
    TIdxGroupObj *group = gpStrategy->mObjectGroup;
    for (JGadget::TList_pointer_void::iterator it =
             group->mViewObjList.begin();
         it != group->mViewObjList.end(); ++it) {
        THitActor *hit = static_cast<THitActor *>(*it);
        if (!hit || (hit->mObjectID != kHideObject &&
                     hit->mObjectID != kMapSmoke)) {
            continue;
        }
        TMapObjBase *actor = static_cast<TMapObjBase *>(hit);
        if (isSprayHideObject(actor)) {
            drawHiddenMarker(menu, projection,
                             static_cast<THideObjBase *>(actor));
        }
    }
}

}  // namespace

void draw(Menu *menu) {
    if (!menu || menu->shown() ||
        gpApplication.mContext != TApplication::CONTEXT_DIRECT_STAGE ||
        !gpStrategy || !gpMarDirector ||
        gpMarDirector->_260 == 0 ||
        gpMarDirector->mCurState < TMarDirector::STATE_GAME_STARTING) {
        return;
    }
    const bool hurtboxes = gSettings.getBool(SETTING_ENEMY_HURTBOXES);
    const bool pinnaItems = gSettings.getBool(SETTING_PINNA_HIDDEN_ITEMS);
    if (!hurtboxes && !pinnaItems) return;

    const Projection projection;
    if (!projection.valid) return;
    if (hurtboxes) {
        // Teeth and other boss sub-parts can live in the object group.
        drawGroupHurtboxes(menu, projection, gpStrategy->mObjectGroup);
        drawGroupHurtboxes(menu, projection, gpStrategy->mEnemyGroup);
        drawGroupHurtboxes(menu, projection, gpStrategy->mBossGroup);
    }
    if (pinnaItems) drawPinnaItems(menu, projection);
}

}  // namespace PracticeVisuals
