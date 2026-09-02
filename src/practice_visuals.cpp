#include "susamune/practice_visuals.hxx"

#include "Dolphin/MTX.h"
#include "Dolphin/math.h"
#include "Dolphin/string.h"
#include "JSystem/J2D/J2DScreen.hxx"
#include "SMS/Camera/CubeManagerBase.hxx"
#include "SMS/Camera/CubeMapTool.hxx"
#include "SMS/Camera/PolarSubCamera.hxx"
#include "SMS/GC2D/GCConsole2.hxx"
#include "SMS/MapObj/MapObjHide.hxx"
#include "SMS/Strategic/Strategy.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/addresses.hxx"
#include "susamune/iling.hxx"
#include "susamune/menu.hxx"
#include "susamune/settings.hxx"

namespace PracticeVisuals {
namespace {

enum HiddenItemMode {
    HIDDEN_ITEMS_OFF,
    HIDDEN_ITEMS_BOTH,
    HIDDEN_ITEMS_FRUIT,
    HIDDEN_ITEMS_COINS,
};

enum HurtboxMode {
    HURTBOX_OFF,
    HURTBOX_WIREFRAME,
    HURTBOX_TRANSPARENT,
    HURTBOX_SOLID,
};

enum HurtboxTarget {
    HURTBOX_ALL_ENEMIES,
    HURTBOX_EELY_TEETH,
};

const u32 kBossActor = 0x08000000u;
const u32 kEnemyActor = 0x10000000u;
const u32 kNoCollision = 0x00000001u;
const u32 kCannotReceive = 0x00000004u;
const u32 kHideObject = 0x20000011u;
const u32 kMapSmoke = 0x4000001fu;
const u32 kCoinFirst = 0x2000000eu;
const u32 kCoinLast = 0x20000010u;
const u32 kFruitFirst = 0x40000390u;
const u32 kFruitLast = 0x40000395u;
const u32 kEelTooth = 0x08000022u;
const u32 kYoshiTongue = 0x08000083u;
const int kBalloonPanelShift = 60;

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

bool isEelBody(const THitActor *actor) {
    const u32 vtable = *reinterpret_cast<const u32 *>(actor);
    return vtable == SUSAMUNE_VT_BOSS_EEL ||
           vtable == SUSAMUNE_VT_BOSS_EEL_BODY_COLLISION;
}

bool validVolume(const THitActor *actor, u8 target) {
    if (!actor || actor->mObjectID == kYoshiTongue || isEelBody(actor) ||
        (target == HURTBOX_EELY_TEETH && actor->mObjectID != kEelTooth) ||
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

void fillCylinder(Menu *menu, const s16 *lower, const s16 *upper,
                  const JUtility::TColor &color) {
    menu->fillPoly(lower, 8, color);
    menu->fillPoly(upper, 8, color);
    for (int i = 0; i < 8; ++i) {
        const int next = (i + 1) & 7;
        const s16 side[8] = {
            lower[i * 2], lower[i * 2 + 1],
            lower[next * 2], lower[next * 2 + 1],
            upper[next * 2], upper[next * 2 + 1],
            upper[i * 2], upper[i * 2 + 1],
        };
        menu->fillPoly(side, 4, color);
    }
}

void drawCylinder(Menu *menu, const Projection &projection,
                  const THitActor *actor, u8 mode, u8 target) {
    if (!validVolume(actor, target)) return;

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

    const JUtility::TColor line(35, 255, 120, 235);
    if (allValid) {
        if (mode == HURTBOX_TRANSPARENT)
            fillCylinder(menu, lower, upper,
                         JUtility::TColor(35, 255, 120, 64));
        else if (mode == HURTBOX_SOLID)
            fillCylinder(menu, lower, upper,
                         JUtility::TColor(35, 255, 120, 255));
        menu->strokePoly(lower, 8, line);
        menu->strokePoly(upper, 8, line);
        for (int i = 0; i < 8; i += 2)
            drawSegment(menu, &lower[i * 2], &upper[i * 2], line);
        return;
    }

    for (int i = 0; i < 8; ++i) {
        const int next = (i + 1) & 7;
        if (lowerValid[i] && lowerValid[next])
            drawSegment(menu, &lower[i * 2], &lower[next * 2], line);
        if (upperValid[i] && upperValid[next])
            drawSegment(menu, &upper[i * 2], &upper[next * 2], line);
        if ((i & 1) == 0 && lowerValid[i] && upperValid[i])
            drawSegment(menu, &lower[i * 2], &upper[i * 2], line);
    }
}

void drawGroupHurtboxes(Menu *menu, const Projection &projection,
                        TIdxGroupObj *group, u8 mode, u8 target) {
    if (!group) return;
    for (JGadget::TList_pointer_void::iterator it =
             group->mViewObjList.begin();
         it != group->mViewObjList.end(); ++it) {
        drawCylinder(menu, projection, static_cast<THitActor *>(*it), mode,
                     target);
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

bool hiddenItemEnabled(u32 objectId, u8 mode) {
    const bool coin = objectId >= kCoinFirst && objectId <= kCoinLast;
    const bool fruit = objectId >= kFruitFirst && objectId <= kFruitLast;
    return (mode == HIDDEN_ITEMS_BOTH && (coin || fruit)) ||
           (mode == HIDDEN_ITEMS_FRUIT && fruit) ||
           (mode == HIDDEN_ITEMS_COINS && coin);
}

bool isSprayHideObject(const TMapObjBase *actor) {
    if (!actor || !actor->mRegisterName) return false;
    if (actor->mObjectID == kHideObject)
        return strcmp(actor->mRegisterName, "WaterHitHideObj") == 0;
    return actor->mObjectID == kMapSmoke &&
           strcmp(actor->mRegisterName, "MapSmoke") == 0;
}

void drawHiddenMarker(Menu *menu, const Projection &projection,
                      const THideObjBase *hide, u8 mode, bool labels) {
    if (!hide || !hide->mAllowReveal || !hide->mHiddenObj ||
        (hide->mObjectType & kNoCollision)) {
        return;
    }
    const u32 objectId = hide->mHiddenObj->mObjectID;
    const char *label = hiddenLabel(objectId);
    if (!label || !hiddenItemEnabled(objectId, mode)) return;

    TVec3f world = hide->mTranslation;
    world.y += 55.0f;
    s16 x;
    s16 y;
    if (!projection.point(world, &x, &y)) return;

    JUtility::TColor line(255, 135, 35, 255);
    JUtility::TColor fill(125, 45, 0, 190);
    if (objectId == 0x2000000eu) {
        line = JUtility::TColor(255, 230, 30, 255);
        fill = JUtility::TColor(130, 105, 0, 190);
    } else if (objectId == 0x2000000fu) {
        line = JUtility::TColor(255, 70, 70, 255);
        fill = JUtility::TColor(125, 0, 0, 190);
    } else if (objectId == 0x20000010u) {
        line = JUtility::TColor(70, 175, 255, 255);
        fill = JUtility::TColor(0, 55, 135, 190);
    }
    const s16 diamond[8] = {
        x, static_cast<s16>(y - 7), static_cast<s16>(x + 7), y,
        x, static_cast<s16>(y + 7), static_cast<s16>(x - 7), y,
    };
    menu->fillPoly(diamond, 4, fill);
    menu->strokePoly(diamond, 4, line);
    if (labels)
        menu->drawText(label, x + 10, y - 6, 10, 10, line);
}

void drawHiddenItems(Menu *menu, const Projection &projection, u8 mode,
                     bool labels) {
    if (!gpStrategy->mObjectGroup) return;
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
                             static_cast<THideObjBase *>(actor), mode, labels);
        }
    }
}

bool validCube(const TCubeGeneralInfo *cube) {
    return cube && cube->mScale.x > 0.0f && cube->mScale.x < 1000000.0f &&
           cube->mScale.y > 0.0f && cube->mScale.y < 1000000.0f &&
           cube->mScale.z > 0.0f && cube->mScale.z < 1000000.0f;
}

TVec3f cubePoint(const TCubeGeneralInfo *cube, f32 x, f32 y, f32 z) {
    const f32 radians = 0.01745329252f;
    const f32 sx = sinf(cube->mRotation.x * radians);
    const f32 cx = cosf(cube->mRotation.x * radians);
    const f32 sy = sinf(cube->mRotation.y * radians);
    const f32 cy = cosf(cube->mRotation.y * radians);
    const f32 sz = sinf(cube->mRotation.z * radians);
    const f32 cz = cosf(cube->mRotation.z * radians);

    const f32 x1 = x;
    const f32 y1 = y * cx - z * sx;
    const f32 z1 = y * sx + z * cx;
    const f32 x2 = x1 * cy + z1 * sy;
    const f32 y2 = y1;
    const f32 z2 = -x1 * sy + z1 * cy;
    return TVec3f(cube->mTranslation.x + x2 * cz - y2 * sz,
                  cube->mTranslation.y + x2 * sz + y2 * cz,
                  cube->mTranslation.z + z2);
}

void drawRaceCube(Menu *menu, const Projection &projection,
                  const TCubeGeneralInfo *cube, char letter,
                  const JUtility::TColor &color) {
    if (!validCube(cube)) return;

    static const s8 kCorners[24] = {
        -1, 0, -1, 1, 0, -1, 1, 0, 1, -1, 0, 1,
        -1, 1, -1, 1, 1, -1, 1, 1, 1, -1, 1, 1,
    };
    static const u8 kEdges[24] = {
        0, 1, 1, 2, 2, 3, 3, 0,
        4, 5, 5, 6, 6, 7, 7, 4,
        0, 4, 1, 5, 2, 6, 3, 7,
    };
    s16 screen[16];
    bool visible[8];
    for (int i = 0; i < 8; ++i) {
        const TVec3f world = cubePoint(
            cube, kCorners[i * 3] * cube->mScale.x * 0.5f,
            kCorners[i * 3 + 1] * cube->mScale.y,
            kCorners[i * 3 + 2] * cube->mScale.z * 0.5f);
        visible[i] = projection.point(world, &screen[i * 2],
                                      &screen[i * 2 + 1]);
    }
    for (int i = 0; i < 12; ++i) {
        const u8 a = kEdges[i * 2];
        const u8 b = kEdges[i * 2 + 1];
        if (visible[a] && visible[b])
            drawSegment(menu, &screen[a * 2], &screen[b * 2], color);
    }

    const TVec3f labelPoint =
        cubePoint(cube, 0.0f, cube->mScale.y * 0.5f, 0.0f);
    s16 x;
    s16 y;
    if (projection.point(labelPoint, &x, &y)) {
        char label[] = "CP A";
        label[3] = letter;
        menu->drawText(label, x + 5, y - 6, 11, 11, color);
    }
}

void drawRaceManager(Menu *menu, const Projection &projection,
                     TCubeManagerBase *manager, char letter,
                     const JUtility::TColor &color) {
    if (!manager) return;
    TNameRefPtrAryT<TCubeGeneralInfo> *info =
        manager->getCubeInfo<TCubeGeneralInfo>();
    if (!info) return;
    for (JGadget::TVector_pointer_void::iterator it = info->mChildren.begin();
         it != info->mChildren.end(); ++it) {
        drawRaceCube(menu, projection,
                     static_cast<const TCubeGeneralInfo *>(*it), letter, color);
    }
}

void drawRiccoCheckpoints(Menu *menu, const Projection &projection) {
    if (gpApplication.mCurrentScene.mAreaID != TGameSequence::AREA_RICOEX0 ||
        gpApplication.mCurrentScene.mEpisodeID != 0) {
        return;
    }
    drawRaceManager(menu, projection, gpCubeFastA, 'A',
                    JUtility::TColor(75, 190, 255, 245));
    drawRaceManager(menu, projection, gpCubeFastB, 'B',
                    JUtility::TColor(255, 185, 45, 245));
    drawRaceManager(menu, projection, gpCubeFastC, 'C',
                    JUtility::TColor(95, 255, 130, 245));
}

bool paneOnScreen(const J2DPane *pane) {
    return pane && pane->mIsVisible && pane->mRect.mY1 < 448 &&
           pane->mRect.mY2 > 0;
}

J2DPane *shiftPinnaBalloonPanel(J2DScreen *screen) {
    if (!screen ||
        gpApplication.mContext != TApplication::CONTEXT_DIRECT_STAGE ||
        gpApplication.mCurrentScene.mAreaID != TGameSequence::AREA_PINNABOSS ||
        gpApplication.mCurrentScene.mEpisodeID != 0 || !gpMarDirector ||
        !gpMarDirector->mGCConsole ||
        gpMarDirector->mGCConsole->mMainScreen != screen) {
        return nullptr;
    }

    J2DPane *balloon = screen->search('\0b_0');
    J2DPane *timer = screen->search('\0t_0');
    if (!paneOnScreen(balloon) || !paneOnScreen(timer)) return nullptr;

    // This hook runs after retail's TExPane updates and immediately before the
    // screen draws, so neither pane's animation state learns about the shift.
    balloon->add(0, kBalloonPanelShift);
    return balloon;
}

}  // namespace

void update() {
    if (gSettings.get(SETTING_PINNA_HIDDEN_ITEMS) != HIDDEN_ITEMS_OFF ||
        gSettings.get(SETTING_ENEMY_HURTBOXES) != HURTBOX_OFF ||
        gSettings.getBool(SETTING_RICCO_RACE_CHECKPOINTS)) {
        ILing::invalidateForAssist();
    }
}

void drawHudScreen(J2DScreen *screen, int x, int y,
                   const J2DGrafContext *context) {
    J2DPane *shifted = shiftPinnaBalloonPanel(screen);
    screen->draw(x, y, context);
    if (shifted) shifted->add(0, -kBalloonPanelShift);
}

void draw(Menu *menu) {
    if (!menu || menu->shown() ||
        gpApplication.mContext != TApplication::CONTEXT_DIRECT_STAGE ||
        !gpStrategy || !gpMarDirector || gpMarDirector->_260 == 0 ||
        gpMarDirector->mCurState < TMarDirector::STATE_GAME_STARTING) {
        return;
    }
    const u8 hurtboxMode = gSettings.get(SETTING_ENEMY_HURTBOXES);
    const u8 hiddenMode = gSettings.get(SETTING_PINNA_HIDDEN_ITEMS);
    const bool checkpoints =
        gSettings.getBool(SETTING_RICCO_RACE_CHECKPOINTS);
    if (hurtboxMode == HURTBOX_OFF && hiddenMode == HIDDEN_ITEMS_OFF &&
        !checkpoints) {
        return;
    }

    const Projection projection;
    if (!projection.valid) return;
    if (hurtboxMode != HURTBOX_OFF) {
        const u8 target = gSettings.get(SETTING_HURTBOX_TARGET);
        // Teeth and other boss sub-parts can live in the object group.
        drawGroupHurtboxes(menu, projection, gpStrategy->mObjectGroup,
                           hurtboxMode, target);
        drawGroupHurtboxes(menu, projection, gpStrategy->mEnemyGroup,
                           hurtboxMode, target);
        drawGroupHurtboxes(menu, projection, gpStrategy->mBossGroup,
                           hurtboxMode, target);
    }
    if (hiddenMode != HIDDEN_ITEMS_OFF)
        drawHiddenItems(menu, projection, hiddenMode,
                        gSettings.getBool(SETTING_HIDDEN_ITEM_LABELS));
    if (checkpoints) drawRiccoCheckpoints(menu, projection);
}

}  // namespace PracticeVisuals

extern "C" void susamuneDrawHudScreen(J2DScreen *screen, int x, int y,
                                       const J2DGrafContext *context) {
    PracticeVisuals::drawHudScreen(screen, x, y, context);
}
