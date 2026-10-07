#ifndef SUSAMUNE_STATE_ARCHIVE_PROFILE_HXX
#define SUSAMUNE_STATE_ARCHIVE_PROFILE_HXX

namespace StateArchiveProfile {
const unsigned int kMaxAnchors = 640;
const unsigned int kMaxKeepRanges = 96;
struct Word { unsigned int address, value; };
struct Range { unsigned int address, size; };
struct Data {
    unsigned int magic, version, game, build, config, count, keepCount, checksum;
    Word anchors[kMaxAnchors];
    Range keep[kMaxKeepRanges];
};
static_assert(sizeof(Data) == 5920, "archive owner profile wire size changed");

typedef bool (*ReadWord)(void *, unsigned int, unsigned int *);
struct Layout {
    unsigned int game, application, rootHeap, systemHeap, currentHeap;
    unsigned int setupThread, setupThreadStack, volumeList;
    unsigned int solidVtable, expVtable, memArchiveVtable, aramArchiveVtable;
    unsigned int timeRec, rumble, globals[16];
};

bool capture(Data &, unsigned int build, unsigned int config);
bool captureWithReader(Data &, const Layout &, ReadWord, void *,
                       unsigned int build, unsigned int config);
bool valid(const Data &);
bool matches(const Data &saved, const Data &live);
bool reidentify(Data &, unsigned int build);
unsigned int failureAddress();

// Use only the locally captured, matched live profile, after validating all
// destination spans. Preserved bytes are never written, even temporarily.
void copyGameBytes(void *liveProfile, void *destination,
                   const void *source, unsigned int size);
}
#endif
