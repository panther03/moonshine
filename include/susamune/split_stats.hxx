#ifndef _SUSAMUNE_SPLIT_STATS_HXX
#define _SUSAMUNE_SPLIT_STATS_HXX

#include <Dolphin/types.h>

class Menu;

namespace SplitStats {

// Persistent schema IDs. Append only; never derive these from IL catalog rows.
enum RouteId : u16 {
    ROUTE_BIANCO_4 = 0,
    ROUTE_GELATO_8_GBS = 1,
    ROUTE_PIANTA_6 = 2,
    ROUTE_RICCO_1 = 3,
    ROUTE_RICCO_2 = 4,
    ROUTE_RICCO_3 = 5,
    ROUTE_RICCO_4 = 6,
    ROUTE_RICCO_5 = 7,
    ROUTE_RICCO_6 = 8,
    ROUTE_RICCO_7 = 9,
    ROUTE_AIRSTRIP_1 = 10,
    ROUTE_BIANCO_PLANT = 11,
    ROUTE_DELFINO_SHADOW_MARIO = 12,
    ROUTE_BIANCO_2 = 13,
    ROUTE_BIANCO_3_FULL = 14,
    ROUTE_BIANCO_3_SECRET = 15,
    ROUTE_TRAVEL_SKIP = 16,
    ROUTE_BIANCO_5 = 17,
    ROUTE_BIANCO_6_FULL = 18,
    ROUTE_BIANCO_6_SECRET = 19,
    ROUTE_BIANCO_7 = 20,
    ROUTE_GELATO_PLANT = 21,
    ROUTE_GELATO_7 = 22,
    ROUTE_PIANTA_1 = 23,
    ROUTE_PIANTA_2 = 24,
    ROUTE_PIANTA_3 = 25,
    ROUTE_PIANTA_4 = 26,
    ROUTE_PIANTA_5_FULL = 27,
    ROUTE_PIANTA_5_SECRET = 28,
    ROUTE_PIANTA_7 = 29,
    ROUTE_HONEY_SKIP = 30,
    ROUTE_PINNA_1 = 31,
    ROUTE_PINNA_2_FULL = 32,
    ROUTE_PINNA_2_SECRET = 33,
    ROUTE_PINNA_3 = 34,
    ROUTE_PINNA_4 = 35,
    ROUTE_PINNA_EYG = 36,
    ROUTE_PINNA_6_SECRET = 37,
    ROUTE_PINNA_7 = 38,
    ROUTE_SIRENA_1 = 39,
    ROUTE_SIRENA_2_FULL = 40,
    ROUTE_SIRENA_2_SECRET = 41,
    ROUTE_SIRENA_3 = 42,
    ROUTE_SIRENA_4_FULL = 43,
    ROUTE_SIRENA_4_SECRET = 44,
    ROUTE_SIRENA_5 = 45,
    ROUTE_SIRENA_6 = 46,
    ROUTE_SIRENA_7 = 47,
    ROUTE_NOKI_1 = 48,
    ROUTE_NOKI_2 = 49,
    ROUTE_NOKI_3 = 50,
    ROUTE_NOKI_4_FULL = 51,
    ROUTE_NOKI_4_EEL = 52,
    ROUTE_NOKI_5 = 53,
    ROUTE_NOKI_6_FULL = 54,
    ROUTE_NOKI_6_SECRET = 55,
    ROUTE_NOKI_7 = 56,
    ROUTE_CORONA = 57,
    ROUTE_BOWSER = 58,
    ROUTE_RICCO_2_RACE = 59,
    ROUTE_RICCO_4_SECRET = 60,
    ROUTE_BIANCO_1 = 61,
    ROUTE_BIANCO_3_REDS = 62,
    ROUTE_BIANCO_6_REDS = 63,
    ROUTE_BIANCO_8 = 64,
    ROUTE_BIANCO_100 = 65,
    ROUTE_RICCO_4_REDS = 66,
    ROUTE_RICCO_8 = 67,
    ROUTE_RICCO_100 = 68,
    ROUTE_GELATO_1_FULL = 69,
    ROUTE_GELATO_1_SECRET = 70,
    ROUTE_GELATO_1_REDS = 71,
    ROUTE_GELATO_2 = 72,
    ROUTE_GELATO_3 = 73,
    ROUTE_GELATO_4 = 74,
    ROUTE_GELATO_4_INSIDE = 75,
    ROUTE_GELATO_5 = 76,
    ROUTE_GELATO_6 = 77,
    ROUTE_GELATO_8 = 78,
    ROUTE_GELATO_HIDDEN = 79,
    ROUTE_GELATO_100 = 80,
    ROUTE_PINNA_2_REDS = 81,
    ROUTE_PINNA_5 = 82,
    ROUTE_PINNA_6_FULL = 83,
    ROUTE_PINNA_6_REDS = 84,
    ROUTE_PINNA_8 = 85,
    ROUTE_PINNA_100 = 86,
    ROUTE_SIRENA_2_REDS = 87,
    ROUTE_SIRENA_4_REDS = 88,
    ROUTE_SIRENA_8 = 89,
    ROUTE_SIRENA_100 = 90,
    ROUTE_NOKI_6_REDS = 91,
    ROUTE_NOKI_8 = 92,
    ROUTE_NOKI_HIDDEN = 93,
    ROUTE_NOKI_100 = 94,
    ROUTE_PIANTA_5_REDS = 95,
    ROUTE_PIANTA_8 = 96,
    ROUTE_PIANTA_HIDDEN = 97,
    ROUTE_PIANTA_100 = 98,
    ROUTE_AIRSTRIP_REDS = 99,
    ROUTE_DELFINO_COP_SECRET = 100,
    ROUTE_PACHINKO = 101,
    ROUTE_DELFINO_SLIDE = 102,
    ROUTE_LILY_PAD = 103,
    ROUTE_GRASS_SECRET = 104,
    ROUTE_LIGHTHOUSE = 105,
    ROUTE_BOX_GAME_1 = 106,
    ROUTE_BOX_GAME_2 = 107,
    ROUTE_LEFT_BELL = 108,
    ROUTE_RIGHT_BELL = 109,
    ROUTE_CHUCKSTER = 110,
    ROUTE_SHINE_GATE = 111,
    ROUTE_DELFINO_100 = 112,
    ROUTE_UNDERBELL = 113,
    ROUTE_BEACH_SHINE = 114,
    ROUTE_GOLD_BIRD = 115,
    ROUTE_PIANTA_ENTER = 116,
    ROUTE_RICCO_ENTER = 117,
    ROUTE_BIANCO_II_ENTER = 118,
    ROUTE_SIRENA_ENTER = 119,
    ROUTE_NOKI_ENTER = 120,
    ROUTE_CORONA_ENTER = 121,
    ROUTE_BIANCO_3_FULL_REDS = 122,
    ROUTE_BIANCO_6_FULL_REDS = 123,
    ROUTE_RICCO_4_FULL_REDS = 124,
    ROUTE_GELATO_1_FULL_REDS = 125,
    ROUTE_PINNA_2_FULL_REDS = 126,
    ROUTE_PINNA_6_FULL_REDS = 127,
    ROUTE_SIRENA_2_FULL_REDS = 128,
    ROUTE_SIRENA_4_FULL_REDS = 129,
    ROUTE_NOKI_6_FULL_REDS = 130,
    ROUTE_PIANTA_5_FULL_REDS = 131,
    ROUTE_COUNT = 132,
    ROUTE_INVALID = 0xffff,
};

struct Summary {
    struct Segment {
        s32 pbSplitQf;
        s32 pbSegmentQf;
        s32 goldQf;
    };

    enum { MAX_SEGMENTS = 6 };

    u32 attempts;
    u32 finishes;
    u32 golds;
    u32 playedQf;
    s32 sumBestQf;
    const char *routeName;
    Segment segments[MAX_SEGMENTS];
    u8 segmentCount;
};

enum StorageState : u8 {
    STORAGE_SESSION,
    STORAGE_READ_ONLY,
    STORAGE_SAVING,
    STORAGE_FAILED,
    STORAGE_SD,
};

enum DeleteGoldResult : u8 {
    DELETE_GOLD_OK,
    DELETE_GOLD_NONE,
    DELETE_GOLD_READ_ONLY,
    DELETE_GOLD_INVALID,
};

void init();
void beginFrame();
void update();
void onStageSetup();

// ILing owns attempt identity; the detector owns only retail event edges.
void onILAttemptStarted(int entry, bool eligible);
void onILAttemptEnded();
void invalidateAttempt();
void onILResult(int entry, s32 qf);
void onPBDeleted(int entry, int profile);
void onSavestateLoaded();

// `eventId` is the route-local, zero-based endpoint ordinal. Equal absolute
// QFs are valid when one retail update crosses multiple thresholds.
bool onRouteEvent(u16 routeId, u8 eventId, s32 absoluteQf);
bool routeActive(u16 routeId);

bool supportsEntry(int entry);
bool summary(int entry, Summary *out);
DeleteGoldResult deleteGold(int entry, u8 localSegment);

StorageState storageState();

// Presentation only. QftDisplay refuses adjacency unless the compact QFT
// actually drew earlier in this same closed-menu render pass.
void draw(Menu *menu);

}  // namespace SplitStats

#endif  // _SUSAMUNE_SPLIT_STATS_HXX
