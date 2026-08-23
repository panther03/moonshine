// =====================================================================
// pattern_selector.cpp
//
// Native port of sup39's Pianta Village Pattern Selector. The seven
// Chomplet graph states and the Chain Chomp's three branches are the compact
// tables from the original code; everything else is ordinary C++ and uses
// Susamune's existing menu renderer.
// =====================================================================

#include "susamune/pattern_selector.hxx"

#include "JSystem/JUtility/JUTGamePad.hxx"
#include "SMS/Enemy/SpineEnemy.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/addresses.hxx"
#include "susamune/glyphs.hxx"
#include "susamune/menu.hxx"
#include "susamune/settings.hxx"

namespace {

typedef JUtility::TColor Color;

u8  sPattern[3];
u8  sCursor;
u16 sPreviousButtons;

struct ChompletPath {
    u16 key;       // current graph node in the high byte, actor-name digit low
    u8  next[4];   // selected patterns 1..4; 0xff falls back to random
};

// Byte-for-byte equivalent of the seven records at 0x817f0440 in the Gecko
// version, written out as data with names instead of an opaque memory block.
const ChompletPath kChompletPaths[] = {
    { 0x2132, { 0x45, 0x20, 0x20, 0xff } },
    { 0x1d32, { 0xff, 0x62, 0x1c, 0xff } },
    { 0x2630, { 0x50, 0x25, 0x25, 0x25 } },
    { 0x2130, { 0xff, 0x45, 0x20, 0x20 } },
    { 0x1d30, { 0xff, 0xff, 0x62, 0x1c } },
    { 0x0831, { 0x51, 0x07, 0x07, 0xff } },
    { 0x0231, { 0xff, 0x36, 0x01, 0xff } },
};

const u8 kBossPaths[3] = { 0x1e, 0x6e, 0x20 };  // left, up, right

// One authored outgoing edge for every Bianco Petey graph node. The stop
// sequence is 3 (N1) -> 10 (S1) -> 8 (S2) -> 12 (S3); 16 and 9 are the
// retail non-stop transit back to S1.
const u8 kPeteyPath[] = {
    1, 2, 3, 10, 3, 4, 7, 8, 12, 10, 8, 4, 16, 3, 11, 12, 9, 3,
};

bool inPlayableStage() {
    return gpMarDirector && gpMarDirector->mCurState == TMarDirector::STATE_NORMAL;
}

int selectedNode(TSpineEnemy *enemy, int current, int previous) {
    if (!gSettings.getBool(SETTING_PATTERN_SELECTOR)) {
        return -1;
    }

    const u32 vtable = *(const u32 *)enemy;
    if (vtable == SUSAMUNE_VT_BOSS_WANWAN) {
        // PV4 only selects the first fork. Later calls must remain random or
        // the Chain Chomp repeatedly targets an initial graph node.
        if (previous != -1) {
            return -1;
        }
        const u8 pattern = sPattern[0];
        return pattern >= 1 && pattern <= 3 ? kBossPaths[pattern - 1] : -1;
    }

    if (vtable != SUSAMUNE_VT_FIRE_WANWAN || !enemy->mKeyName) {
        return -1;
    }

    // The three Chomplets are named with a stable trailing 0/1/2 byte. This
    // is the same mKeyName[19] discriminator the original hook uses.
    const u8 nameDigit = (u8)enemy->mKeyName[19];
    if (nameDigit < (u8)'0' || nameDigit > (u8)'2') {
        return -1;
    }
    // Spawn/name order is 2, 0, 1 in the game's actor list. The original
    // table stores exactly this remap in the byte following each path record;
    // the on-screen digits remain left-to-right spawn order.
    const u8 patternIndex = nameDigit == (u8)'2' ? 0 :
                            nameDigit == (u8)'0' ? 1 : 2;
    const u8 pattern = sPattern[patternIndex];
    if (pattern < 1 || pattern > 4) {
        return -1;
    }

    const u16 key = (u16)(((current & 0xff) << 8) | nameDigit);
    for (u32 i = 0; i < sizeof(kChompletPaths) / sizeof(kChompletPaths[0]); i++) {
        if (kChompletPaths[i].key == key) {
            const u8 node = kChompletPaths[i].next[pattern - 1];
            return node == 0xff ? -1 : node;
        }
    }
    return -1;
}

bool forcesPeteyRoute(TSpineEnemy *enemy) {
    return gSettings.getBool(SETTING_PETEY_ROUTE) && gpMarDirector &&
           gpMarDirector->mAreaID == 2 && gpMarDirector->mEpisodeID == 4 &&
           *(const u32 *)enemy == SUSAMUNE_VT_BOSS_PAKKUN;
}

}  // namespace

namespace PatternSelector {

void update(bool allowInput) {
    const u16 held = JUTGamePad::mPadStatus[0].mButton;
    const u16 pressed = (u16)(held & ~sPreviousButtons);
    sPreviousButtons = held;

    if (!allowInput || !gSettings.getBool(SETTING_PATTERN_SELECTOR) ||
        !inPlayableStage() ||
        !(held & JUTGamePad::L)) {
        return;
    }

    if (pressed & JUTGamePad::DPAD_LEFT) {
        sCursor = (u8)((sCursor + 3) & 3);
    } else if (pressed & JUTGamePad::DPAD_RIGHT) {
        sCursor = (u8)((sCursor + 1) & 3);
    }

    if (pressed & (JUTGamePad::DPAD_UP | JUTGamePad::DPAD_DOWN)) {
        // Cursor 3 is the original code's hidden-cursor state. Editing from
        // there returns to the first digit.
        if (sCursor >= 3) {
            sCursor = 0;
        }
        const u8 delta = (pressed & JUTGamePad::DPAD_UP) ? 1 : 4;
        sPattern[sCursor] = (u8)((sPattern[sCursor] + delta) % 5);
    }
}

void draw(Menu *menu) {
    if (!menu || !gSettings.getBool(SETTING_PATTERN_SELECTOR) || !inPlayableStage()) {
        return;
    }

    const Color normal(255, 255, 255, 230);
    const Color cursor(255, 194, 61, 255);
    const int y = 426;
    menu->fillBox(454, 421, 178, 29, Color(0, 0, 0, 150));
    menu->drawText("Pattern", 462, y, 14, 14, normal);

    char digit[2] = { '0', '\0' };
    for (int i = 0; i < 3; i++) {
        const int x = 536 + i * 30;
        menu->drawText(i == sCursor ? SUSAMUNE_GLYPH_B : " ", x, y,
                       14, 14, cursor);
        digit[0] = (char)('0' + sPattern[i]);
        menu->drawText(digit, x + 14, y, 16, 16, normal);
    }
}

}  // namespace PatternSelector

extern "C" void susamuneGoToRandomNextGraphNode(TSpineEnemy *enemy) {
    TGraphTracer *tracer = enemy->mGraphTracer;
    TGraphWeb *graph = tracer->mGraph;

    if (tracer->getCurGraphIndex() < 0) {
        typedef int (*FindNearestNodeFn)(const TGraphWeb *, const TVec3f &, u32);
        FindNearestNodeFn findNearest =
            (FindNearestNodeFn)SUSAMUNE_ADDR_GRAPH_FIND_NEAREST_NODE;
        tracer->setTo(findNearest(graph, enemy->mTranslation, (u32)-1));
    } else {
        const int current = tracer->getCurGraphIndex();
        int next;
        if (forcesPeteyRoute(enemy)) {
            // Preserve the conditional retail RNG advance even though its edge
            // choice is replaced. The graph node itself is the route phase.
            next = graph->getRandomNextIndex(current, tracer->mPreviousNode, (u32)-1);
            if ((u32)current < sizeof(kPeteyPath)) next = kPeteyPath[current];
        } else {
            next = selectedNode(enemy, current, tracer->mPreviousNode);
            if (next < 0) {
                next = graph->getRandomNextIndex(current, tracer->mPreviousNode,
                                                 (u32)-1);
            }
        }
        tracer->moveTo(next);
    }

    enemy->setGoalPathFromGraph();
    enemy->_128 = 0;
    enemy->_12C = 0.0f;
}
