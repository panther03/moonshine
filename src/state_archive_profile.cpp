#include "susamune/state_archive_profile.hxx"
#include "susamune/state_compatibility.h"

#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata" data=".foxtrot.data" bss=".foxtrot.bss"
#include "Dolphin/mem.h"
#ifndef STATE_ARCHIVE_PROFILE_HOST
#include "susamune/addresses.hxx"
#include "SMS/System/Application.hxx"
#include "Dolphin/DVD.h"
#endif

namespace StateArchiveProfile {
namespace {
const unsigned int kMagic = 0x534F574Eu;
const unsigned int kMaxAllocations = 4096;
unsigned int sFailure;

bool memory(unsigned int address, unsigned int size) {
    if (address > 0xffffffffu - size) return false;
    return (address >= 0x80000000u && address + size <= 0x81800000u) ||
           (address >= 0x70000000u && address + size <= 0x72000000u) ||
           (address >= 0x90000000u && address + size <= 0x94000000u);
}
bool mem1(unsigned int address, unsigned int size) {
    return address >= 0x80000000u && size <= 0x1800000u &&
           address <= 0x81800000u - size;
}
unsigned int checksum(const Data &data) {
    const unsigned char *p = reinterpret_cast<const unsigned char *>(&data);
    unsigned int hash = 2166136261u;
    for (unsigned int i = 0; i < sizeof(data); ++i)
        hash = (hash ^ ((i >= 28 && i < 32) ? 0 : p[i])) * 16777619u;
    return hash;
}
struct Reader {
    Data &out;
    const Layout &layout;
    ReadWord callback;
    void *context;
    unsigned int stage, end, heaps[32], heapCount;

    bool word(unsigned int address, unsigned int &value) {
        sFailure = address;
        return !(address & 3u) && memory(address, 4) &&
               callback(context, address, &value);
    }
    bool add(unsigned int address, unsigned int value) {
        if (out.count == kMaxAnchors) return false;
        out.anchors[out.count++] = {address, value};
        return true;
    }
    bool anchor(unsigned int address, unsigned int *value = 0,
                 unsigned int mask = 0xffffffffu) {
        unsigned int v;
        if (!word(address, v) || !add(address, v & mask)) return false;
        if (value) *value = v;
        return true;
    }
    bool keep(unsigned int address, unsigned int size) {
        if (!size || !mem1(address, size)) return false;
        unsigned int first = address, last = address + size, i = 0;
        while (i < out.keepCount && out.keep[i].address + out.keep[i].size < first) ++i;
        while (i < out.keepCount && out.keep[i].address <= last) {
            if (out.keep[i].address < first) first = out.keep[i].address;
            const unsigned int oldEnd = out.keep[i].address + out.keep[i].size;
            if (oldEnd > last) last = oldEnd;
            for (unsigned int j = i + 1; j < out.keepCount; ++j)
                out.keep[j - 1] = out.keep[j];
            --out.keepCount;
        }
        if (out.keepCount == kMaxKeepRanges) return false;
        for (unsigned int j = out.keepCount; j > i; --j) out.keep[j] = out.keep[j - 1];
        out.keep[i] = {first, last - first};
        ++out.keepCount;
        return true;
    }
    bool idleMutex(unsigned int address) {
        for (unsigned int i = 0; i < 16; i += 4) {
            unsigned int value;
            if (!word(address + i, value) || value) return false;
        }
        return true;
    }
    bool backing(unsigned int address, unsigned int size) {
        if (!mem1(address, size)) return false;
        if (address >= stage + 0x78 && address + size <= end) return true;
        for (unsigned int i = 0; i < heapCount; ++i) {
            const unsigned int h = heaps[i];
            unsigned int vt, node;
            if (!mem1(h, 0x84) || (h >= stage && h < end)) continue;
            if (!word(h, vt) || vt != layout.expVtable || !word(h + 0x7c, node)) continue;
            for (unsigned int n = 0; node && n < kMaxAllocations; ++n) {
                unsigned int bytes, next;
                if (!word(node + 4, bytes) || !word(node + 12, next)) return false;
                if (address >= node + 16 && address - node - 16 <= bytes &&
                    size <= bytes - (address - node - 16)) return true;
                node = next;
            }
        }
        return false;
    }
    bool allocations(unsigned int heap, unsigned int start, unsigned int limit) {
        unsigned int node, tail, previous = 0, count = 0;
        unsigned int hash1 = 2166136261u, hash2 = 0x9e3779b9u;
        if (!word(heap + 0x7c, node) || !word(heap + 0x80, tail)) return false;
        while (node) {
            if (++count > kMaxAllocations || node < start || node > limit - 16 || (node & 3)) return false;
            unsigned int fields[4];
            for (unsigned int i = 0; i < 4; ++i) {
                if (!word(node + i * 4, fields[i])) return false;
                hash1 = (hash1 ^ fields[i]) * 16777619u;
                hash2 = ((hash2 << 7) | (hash2 >> 25)) ^ (fields[i] + node + i);
            }
            if ((fields[0] >> 16) != 0x484d || fields[1] > limit - node - 16 ||
                fields[2] != previous) return false;
            previous = node;
            node = fields[3];
        }
        return previous == tail && add(heap + 0x7c, hash1) &&
               add(heap + 0x80, hash2) && add(heap + 0x74, count);
    }
    bool heap(unsigned int address) {
        for (unsigned int i = 0; i < heapCount; ++i) if (heaps[i] == address) return false;
        if (heapCount == 32 || !memory(address, 0x84)) return false;
        heaps[heapCount++] = address;
        unsigned int vt, start, limit, children, tail, count;
        if (!word(address, vt) || (vt != layout.expVtable && vt != layout.solidVtable) ||
            !word(address + 0x30, start) || !word(address + 0x34, limit) ||
            start >= limit || !memory(start, limit - start)) return false;
        // Mod-owned MEM2 heaps are outside every restore destination. Their
        // changing ghosts are independent of the game's persistent owner proof.
        if (!mem1(address, 0x84)) return true;
        if (!idleMutex(address + 0x18)) return false;
        for (unsigned int i = 0; i < 0x18; i += 4)
            if (!anchor(address + i)) return false;
        for (unsigned int i = 0x30; i < 0x58; i += 4)
            if (!anchor(address + i)) return false;
        if (!keep(address, 0x58)) return false;
        if (vt == layout.expVtable && (address < stage || address >= end) &&
            !allocations(address, start, limit)) return false;
        if (!word(address + 0x3c, children) || !word(address + 0x40, tail) ||
            !word(address + 0x44, count) || count > 32) return false;
        unsigned int previous = 0;
        for (unsigned int i = 0; i < count; ++i) {
            unsigned int object, parent, back, next;
            if (!children || !word(children, object) || children != object + 0x48 ||
                !word(children + 4, parent) || parent != address + 0x3c ||
                !word(children + 8, back) || back != previous ||
                !word(children + 12, next) || !heap(object)) return false;
            previous = children;
            children = next;
        }
        return !children && previous == tail;
    }
    bool volumes() {
        unsigned int node, tail, count, previous = 0;
        if (!anchor(layout.volumeList, &node) || !anchor(layout.volumeList + 4, &tail) ||
            !anchor(layout.volumeList + 8, &count) || !count || count > 16) return false;
        for (unsigned int i = 0; i < count; ++i) {
            unsigned int object, parent, back, next, vt;
            if (!node || !word(node, object) || node != object + 0x18 ||
                !word(node + 4, parent) || parent != layout.volumeList ||
                !word(node + 8, back) || back != previous || !word(node + 12, next) ||
                !word(object, vt) || vt != layout.memArchiveVtable ||
                !backing(object, 0x6c)) return false;
            // Only MemArchive has a proved transport-free owner layout here.
            // ARAM/DVD/Comp variants need their own worker admission profile.
            for (unsigned int n = 0; n < 0x6c; n += 4) {
                const unsigned int mask = (n == 0x30 || n == 0x3c || n == 0x68)
                                               ? 0xff000000u : 0xffffffffu;
                if (!anchor(object + n, 0, mask)) return false;
            }
            unsigned int header, magic, size;
            if (!word(object + 0x60, header) || !backing(header, 32) ||
                !word(header, magic) || magic != 0x52415243u ||
                !word(header + 4, size) || size < 32 || !backing(header, size)) return false;
            for (unsigned int n = 0; n < 32; n += 4)
                if (!anchor(header + n)) return false;
            if (!keep(object, 0x6c)) return false;
            previous = node;
            node = next;
        }
        return !node && previous == tail;
    }
    bool application() {
        const unsigned int a = layout.application;
        const unsigned int pointerOffsets[] = {0,4,0x1c,0x20,0x24,0x28,0x2c,0x30,0x34,0x40};
        for (unsigned int i = 0; i < sizeof(pointerOffsets)/sizeof(*pointerOffsets); ++i) {
            unsigned int value;
            if (!anchor(a + pointerOffsets[i], &value) || !mem1(value, 4) ||
                !keep(a + pointerOffsets[i], 4)) return false;
        }
        for (unsigned int i = 0; i < 4; ++i) {
            unsigned int pad;
            if (!word(a + 0x20 + i * 4, pad) || !backing(pad, 0xf0)) return false;
            for (unsigned int n = 0; n < 0x18; n += 4)
                if (!anchor(pad + n)) return false;
            for (unsigned int n = 0x78; n < 0x90; n += 4)
                if (!anchor(pad + n, 0, n == 0x78 ? 0xffff0000u : 0xffffffffu)) return false;
            if (!keep(pad, 0x18) || !keep(pad + 0x68, 0x3c)) return false;
        }
        unsigned int timeRec, rumble;
        if (!anchor(layout.timeRec, &timeRec) || !mem1(timeRec, 0x820) ||
            !anchor(timeRec) || !keep(timeRec, 0x820) ||
            !anchor(layout.rumble, &rumble) || !mem1(rumble, 0x30) ||
            !keep(rumble, 0x30)) return false;
        for (unsigned int i = 0; i < 16; ++i) {
            unsigned int p = layout.globals[i];
            if (p && (!anchor(p) || !keep(p, 4))) return false;
        }
        return keep(layout.setupThread, 0x310) &&
               anchor(layout.setupThreadStack) && keep(layout.setupThreadStack, 4);
    }
};
}

bool captureWithReader(Data &out, const Layout &layout, ReadWord callback,
                       void *context, unsigned int build, unsigned int config) {
    memset(&out, 0, sizeof(out));
    sFailure = 0;
    if (!callback || !build || !layout.game) return false;
    Reader r = {out, layout, callback, context, 0, 0, {}, 0};
    unsigned int root, system;
    if (!r.anchor(layout.application + 0x40, &r.stage) ||
        !r.word(r.stage + 0x34, r.end) || !mem1(r.stage, r.end - r.stage) ||
        !r.anchor(layout.rootHeap, &root) || !r.anchor(layout.systemHeap, &system) ||
        !r.anchor(layout.currentHeap) || !r.heap(root) || !r.application() || !r.volumes()) {
        memset(&out, 0, sizeof(out));
        return false;
    }
    bool foundStage = false, foundSystem = false;
    for (unsigned int i = 0; i < r.heapCount; ++i) {
        foundStage |= r.heaps[i] == r.stage;
        foundSystem |= r.heaps[i] == system;
    }
    if (!foundStage || !foundSystem) { memset(&out, 0, sizeof(out)); return false; }
    out.magic = kMagic;
    out.version = 1;
    out.game = layout.game;
    out.build = build;
    out.config = config;
    out.checksum = checksum(out);
    sFailure = 0;
    return true;
}

bool valid(const Data &data) {
    if (data.magic != kMagic || data.version != 1 || !data.game || !data.build ||
        !data.count || data.count > kMaxAnchors || !data.keepCount ||
        data.keepCount > kMaxKeepRanges || data.checksum != checksum(data)) return false;
    unsigned int end = 0;
    for (unsigned int i = 0; i < data.keepCount; ++i) {
        const Range &range = data.keep[i];
        if (!range.size || !mem1(range.address, range.size) || range.address <= end) return false;
        end = range.address + range.size;
    }
    for (unsigned int i = 0; i < data.count; ++i)
        if ((data.anchors[i].address & 3u) || !memory(data.anchors[i].address, 4)) return false;
    return true;
}
bool matches(const Data &saved, const Data &live) {
    return valid(saved) && valid(live) && saved.game == live.game &&
        SusamuneStateBuildCompatible(saved.game, saved.build) &&
        SusamuneStateBuildCompatible(live.game, live.build) &&
        memcmp(&saved, &live, __builtin_offsetof(Data, build)) == 0 &&
        memcmp(&saved.config, &live.config,
               __builtin_offsetof(Data, checksum) - __builtin_offsetof(Data, config)) == 0 &&
        memcmp(saved.anchors, live.anchors,
               sizeof(Data) - __builtin_offsetof(Data, anchors)) == 0;
}
bool reidentify(Data &data, unsigned int build) {
    if (!valid(data) || !SusamuneStateBuildCompatible(data.game, data.build) ||
        !SusamuneStateBuildCompatible(data.game, build)) return false;
    data.build = build;
    data.checksum = checksum(data);
    return true;
}
unsigned int failureAddress() { return sFailure; }

void copyGameBytes(void *context, void *destination, const void *source, unsigned int size) {
    const Data &live = *static_cast<const Data *>(context);
    unsigned char *dst = static_cast<unsigned char *>(destination);
    const unsigned char *src = static_cast<const unsigned char *>(source);
    typedef __UINTPTR_TYPE__ Address;
    Address address = reinterpret_cast<Address>(destination);
    for (unsigned int i = 0; i < live.keepCount && size; ++i) {
        const Range &range = live.keep[i];
        const Address end = static_cast<Address>(range.address) + range.size;
        if (end <= address) continue;
        if (range.address >= address + size) break;
        if (address < range.address) {
            const unsigned int n = range.address - address;
            memcpy(dst, src, n); dst += n; src += n; size -= n; address += n;
        }
        const unsigned int n = end - address < size ? end - address : size;
        dst += n; src += n; size -= n; address += n;
    }
    if (size) memcpy(dst, src, size);
}

#ifndef STATE_ARCHIVE_PROFILE_HOST
namespace {
const Layout kLayout = {
    SUSAMUNE_GAME_VERSION, SUSAMUNE_ADDR_APPLICATION,
    SUSAMUNE_MEM1_ADDR(0x80409830,0x8040e298,0x80405970),
    SUSAMUNE_MEM1_ADDR(0x80409828,0x8040e290,0x80405968),
    SUSAMUNE_MEM1_ADDR(0x8040982c,0x8040e294,0x8040596c),
    SUSAMUNE_MEM1_ADDR(0x803f2390,0x803fcbe8,0x803f4388),
    SUSAMUNE_MEM1_ADDR(0x8040a2b0,0x8040e180,0x80405848),
    SUSAMUNE_MEM1_ADDR(0x80400bb0,0x804042b4,0x803fba54),
    SUSAMUNE_MEM1_ADDR(0x803a8690,0x803e0070,0x803d7a28),
    SUSAMUNE_MEM1_ADDR(0x803a8548,0x803dff38,0x803d78f0),
    SUSAMUNE_MEM1_ADDR(0x803a8648,0x803e0028,0x803d79e0),
    SUSAMUNE_MEM1_ADDR(0x803a83a0,0x803dfd90,0x803d7748),
    SUSAMUNE_ADDR_TIME_REC_INSTANCE, SUSAMUNE_ADDR_RUMBLE_MANAGER,
    {SUSAMUNE_MEM1_ADDR(0x8040a2b8,0x8040e188,0x80405850),
     SUSAMUNE_MEM1_ADDR(0x8040a2bc,0x8040e18c,0x80405854),
     SUSAMUNE_MEM1_ADDR(0x8040a2ac,0x8040e17c,0x80405844),
     SUSAMUNE_MEM1_ADDR(0x8040a2b4,0x8040e184,0x8040584c),
     SUSAMUNE_MEM1_ADDR(0x8040a300,0x8040e1d0,0x804058a8),
     SUSAMUNE_MEM1_ADDR(0x8040a2c8,0x8040e198,0x80405860),
     SUSAMUNE_MEM1_ADDR(0x8040a2cc,0x8040e19c,0x80405864),
     SUSAMUNE_MEM1_ADDR(0x8040a2c4,0x8040e194,0x8040585c),
     SUSAMUNE_MEM1_ADDR(0x8040a2d0,0x8040e1a0,0x80405868),
     SUSAMUNE_MEM1_ADDR(0x8040a2d4,0x8040e1a4,0x8040586c)}
};
bool readLive(void *, unsigned int address, unsigned int *value) {
    *value = *reinterpret_cast<volatile unsigned int *>(address);
    return true;
}
}
bool capture(Data &out, unsigned int build, unsigned int config) {
    if (!OSIsThreadTerminated(&gSetupThread) || DVDGetDriveStatus() != DVD_STATE_END) {
        memset(&out, 0, sizeof(out));
        return false;
    }
    return captureWithReader(out, kLayout, readLive, 0, build, config);
}
#endif
}
