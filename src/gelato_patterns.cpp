// Deterministic first-direction choices for Gelato 6's two red-coin fish.
// The two authored rails are loops, so every later node is unambiguous.

#include "Dolphin/types.h"
#include "SMS/Graph/GraphWeb.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/settings.hxx"

namespace {

const u8 kGelatoArea = 4;
const u8 kGelatoSixEpisode = 5;

bool sameText(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int fishIndex(const TGraphWeb *graph) {
    if (!graph) return -1;
    if (sameText(graph->mRailName, "kaiyu16")) return 0;
    if (sameText(graph->mRailName, "kaiyu17")) return 1;
    return -1;
}

u8 patternSlot(u8 pattern, int actorIndex) {
    return (u8)(((pattern - 1) >> (1 - actorIndex)) & 1);
}

}  // namespace

extern "C" int susamuneGelatoFishRandomNext(
    const TGraphWeb *graph, int current, int previous, u32 filter) {
    const int retail = graph->getRandomNextIndex(current, previous, filter);
    const u8 pattern =
        gSettings.get(SETTING_GELATO_RED_COIN_FISH_PATTERN);
    if (!gpMarDirector || gpMarDirector->mAreaID != kGelatoArea ||
        gpMarDirector->mEpisodeID != kGelatoSixEpisode ||
        pattern < 1 || pattern > 4 || previous != -1 || filter != (u32)-1)
        return retail;

    const int actorIndex = fishIndex(graph);
    if (actorIndex < 0 || current < 0 || current >= graph->mNodeCount)
        return retail;

    const TRailNode *node = graph->mNodes[current].mRailNode;
    if (!node || node->mNeighborCount != 2) return retail;
    return node->mNeighborIDs[patternSlot(pattern, actorIndex)];
}
