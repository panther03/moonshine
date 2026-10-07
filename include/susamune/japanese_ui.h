#ifndef SUSAMUNE_JAPANESE_UI_H
#define SUSAMUNE_JAPANESE_UI_H

#include "mem2_map.h"

#define SUSAMUNE_JP_UI_OFFSET 0x00086000u
#define SUSAMUNE_JP_UI_SIZE 0x00019000u
#define SUSAMUNE_JP_UI_PPC_BASE (SUSAMUNE_MEM2_MODBIN_PPC_BASE + SUSAMUNE_JP_UI_OFFSET)
#define SUSAMUNE_JP_UI_DOLPHIN_BASE 0x71C00000u
#define SUSAMUNE_JP_UI_DISC_OFFSET 0x004AA8C0u
#define SUSAMUNE_JP_UI_MAGIC 0x4D4A5549u
#define SUSAMUNE_JP_UI_VERSION 1u
#define SUSAMUNE_JP_UI_FILENAME "ja_ui.bin"

#if SUSAMUNE_JP_UI_OFFSET + SUSAMUNE_JP_UI_SIZE != SUSAMUNE_GHOST_ASSET_VAULT_OFFSET
#error Japanese UI asset must end at the immutable model vault
#endif

static inline unsigned int SusamuneJpUiWord(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | p[3];
}

static inline unsigned int SusamuneJpUiHalf(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | p[1];
}

/* Header and catalogue are immutable after this complete validation. */
static inline int SusamuneJpUiValid(const unsigned char *p, unsigned int bytes)
{
    unsigned int size, count, texts, textBytes, glyphs, glyphCount, pixels;
    unsigned int i, crc = 0xFFFFFFFFu, last = 0;
    if (bytes < 64 || bytes > SUSAMUNE_JP_UI_SIZE ||
        SusamuneJpUiWord(p) != SUSAMUNE_JP_UI_MAGIC ||
        SusamuneJpUiWord(p + 4) != SUSAMUNE_JP_UI_VERSION) return 0;
    size = SusamuneJpUiWord(p + 8);
    count = SusamuneJpUiWord(p + 20);
    texts = SusamuneJpUiWord(p + 24);
    textBytes = SusamuneJpUiWord(p + 28);
    glyphs = SusamuneJpUiWord(p + 32);
    glyphCount = SusamuneJpUiWord(p + 36);
    pixels = SusamuneJpUiWord(p + 40);
    if (size != bytes || SusamuneJpUiWord(p + 16) != 64 ||
        !count || count > 4096 || texts != 64 + count * 8 ||
        !textBytes || textBytes > 65535 || glyphs != texts + textBytes ||
        !glyphCount || glyphCount > 2048 ||
        pixels != ((glyphs + glyphCount * 4 + 31) & ~31u) ||
        pixels > size || size - pixels != glyphCount * 64 ||
        SusamuneJpUiWord(p + 44) != 16 || SusamuneJpUiWord(p + 48) != 64 ||
        SusamuneJpUiWord(p + 52) || SusamuneJpUiWord(p + 56) || SusamuneJpUiWord(p + 60)) return 0;
    for (i = 0; i < size; ++i) {
        unsigned int j;
        crc ^= i >= 12 && i < 16 ? 0 : p[i];
        for (j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    if ((crc ^ 0xFFFFFFFFu) != SusamuneJpUiWord(p + 12) || p[texts + textBytes - 1]) return 0;
    for (i = 0; i < count; ++i) {
        unsigned int key = SusamuneJpUiWord(p + 64 + i * 8);
        unsigned int offset = SusamuneJpUiHalf(p + 70 + i * 8);
        if ((i && key <= last) || offset >= textBytes || (offset && p[texts + offset - 1])) return 0;
        last = key;
    }
    last = 0;
    for (i = 0; i < glyphCount; ++i) {
        const unsigned char *g = p + glyphs + i * 4;
        unsigned int code = SusamuneJpUiHalf(g);
        if (code <= last || g[2] > 24 || g[3]) return 0;
        last = code;
    }
    return 1;
}

#endif
