#pragma once

#include "susamune/state_live_video.hxx"

namespace StateRestoreBindings {
struct Words {
    unsigned int addresses[32], count, first, last;
    void reset(unsigned int begin, unsigned int end) {
        count = 0; first = begin; last = end;
    }
    bool add(const void *field) {
        const StateLiveVideo::Address address = reinterpret_cast<StateLiveVideo::Address>(field);
        if ((address & 3u) || address < first || address >= last || last - address < 4) return false;
        unsigned int index = 0;
        while (index < count && addresses[index] < address) ++index;
        if (index < count && addresses[index] == address) return true;
        if (count == 32) return false;
        for (unsigned int i = count; i > index; --i) addresses[i] = addresses[i - 1];
        addresses[index] = address; ++count;
        return true;
    }
};

inline void copyExcept(const Words &words, void *context, void *destination,
                       const void *source, unsigned int size, StateLiveVideo::Copy copy) {
    unsigned char *dst = static_cast<unsigned char *>(destination);
    const unsigned char *src = static_cast<const unsigned char *>(source);
    StateLiveVideo::Address address = reinterpret_cast<StateLiveVideo::Address>(destination);
    for (unsigned int i = 0; i < words.count && size; ++i) {
        const unsigned int first = words.addresses[i], last = first + 4;
        if (last <= address) continue;
        if (first >= address + size) break;
        if (address < first) {
            const unsigned int n = first - address;
            copy(context, dst, src, n); dst += n; src += n; size -= n; address += n;
        }
        const unsigned int n = last - address < size ? last - address : size;
        dst += n; src += n; size -= n; address += n;
    }
    if (size) copy(context, dst, src, size);
}
}
