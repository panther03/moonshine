#ifndef SUSAMUNE_GHOST_FLUDD_H
#define SUSAMUNE_GHOST_FLUDD_H

/* V6 keeps the old 16-byte controller sample and appends this eight-byte
 * visual observation. Legacy 15-minute streams retain their original stride.
 * 36,000 * 24 == 54,000 * 16, so neither input bank nor state metadata moves. */
#define SUSAMUNE_GHOST_V6_MAX_DURATION_QF 71928u
#define SUSAMUNE_GHOST_V6_MAX_SAMPLE_COUNT 17983u
#define SUSAMUNE_GHOST_V6_INPUT_MAX_COUNT 36000u
#define SUSAMUNE_GHOST_V6_INPUT_SAMPLE_SIZE 24u
#define SUSAMUNE_GHOST_FLUDD_PRESENT 0x08u
#define SUSAMUNE_GHOST_FLUDD_SPRAYING 0x10u

struct SusamuneGhostFluddSample {
    unsigned char mode; /* retail nozzle 0..5, PRESENT, SPRAYING */
    signed char offset[3]; /* emitter relative to Mario, two units per step */
    unsigned char aim[3]; /* unsigned yaw 12 / signed pitch 12, turn / 4096 */
    unsigned char power; /* emission speed, one game unit per step */
};

static int SusamuneGhostFluddValid(const unsigned char *p) {
    unsigned int pitch;
    if (!p[0])
        return !(p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7]);
    if ((p[0] & ~0x1fu) || !(p[0] & SUSAMUNE_GHOST_FLUDD_PRESENT) ||
        (p[0] & 7u) > 5u ||
        (!(p[0] & SUSAMUNE_GHOST_FLUDD_SPRAYING) && p[7])) return 0;
    pitch = ((unsigned int)(p[5] & 15u) << 8) | p[6];
    return pitch <= 1024u || pitch >= 3072u;
}

typedef char SusamuneGhostFluddSampleSize[
    sizeof(struct SusamuneGhostFluddSample) == 8 ? 1 : -1];

#endif
