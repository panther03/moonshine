#ifndef MOONSHINE_FAILURE_BANNER_STYLE_H
#define MOONSHINE_FAILURE_BANNER_STYLE_H
#include "susamune/susamune_cfg.h"

#define MOONSHINE_FAILURE_STYLE_MAGIC 0x4D464231u
#define MOONSHINE_FAILURE_STYLE_TAIL 0xA5u
struct MoonshineFailureStyle {
    unsigned int magic;
    unsigned short x, y;
    unsigned char scale, textA, bgR, bgG, bgB, bgA, textBrightness, padding;
    unsigned char rgb[1][3], reserved;
};
typedef char moonshine_failure_style_size[(sizeof(struct MoonshineFailureStyle) == 20) ? 1 : -1];

static inline int MoonshineFailureStyleValid(const struct MoonshineFailureStyle *style) {
    return style->magic == MOONSHINE_FAILURE_STYLE_MAGIC &&
           style->reserved == MOONSHINE_FAILURE_STYLE_TAIL;
}

// Both slices already travel together in settings journals and layout profiles.
// Existing offsets, file versions and record lengths remain unchanged.
static inline void MoonshineFailureStyleRead(struct MoonshineFailureStyle *out,
    const volatile struct SusamuneWallkickStyleCfg *wallkick,
    const volatile struct SusamunePracticeDisplayStyleCfg *practice) {
    unsigned int i;
    unsigned char *bytes = (unsigned char *)out;
    for (i = 0; i < 8; ++i) bytes[i] = wallkick->reserved1[i];
    for (i = 0; i < 12; ++i) bytes[8 + i] = practice->reserved[i];
}
static inline void MoonshineFailureStyleWrite(const struct MoonshineFailureStyle *src,
    volatile struct SusamuneWallkickStyleCfg *wallkick,
    volatile struct SusamunePracticeDisplayStyleCfg *practice) {
    unsigned int i;
    const unsigned char *bytes = (const unsigned char *)src;
    for (i = 0; i < 8; ++i) wallkick->reserved1[i] = bytes[i];
    for (i = 0; i < 12; ++i) practice->reserved[i] = bytes[8 + i];
}
static inline void MoonshineFailureStyleInit(struct MoonshineFailureStyle *out) {
    struct MoonshineFailureStyle defaults = {MOONSHINE_FAILURE_STYLE_MAGIC,
        162, 357, 100, 255, 8, 12, 20, 210, 100, 10, {{255,255,255}}, MOONSHINE_FAILURE_STYLE_TAIL};
    *out = defaults;
}
#endif
