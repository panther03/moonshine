#ifndef SUSAMUNE_GHOST_TEACHING_H
#define SUSAMUNE_GHOST_TEACHING_H

#include "susamune/practice_input.h"
#include "susamune/ghost_fludd.h"

/* V5 appends this bounded section after the unchanged V4 pose payload. */
#define SUSAMUNE_GHOST_TEACHING_MAGIC 0x53475449u
#define SUSAMUNE_GHOST_TEACHING_VERSION 1u
#define SUSAMUNE_GHOST_TEACHING_FLUDD_VERSION 2u
#define SUSAMUNE_GHOST_TEACHING_HEADER_SIZE 32u
#define SUSAMUNE_GHOST_INPUT_MAX_COUNT 54000u
#define SUSAMUNE_GHOST_INPUT_SAMPLE_SIZE 16u
#define SUSAMUNE_GHOST_SPLIT_MAX_COUNT 8u
#define SUSAMUNE_GHOST_SPLIT_SAMPLE_SIZE 12u
#define SUSAMUNE_GHOST_TEACHING_INPUT_TRUNCATED 1u
#define SUSAMUNE_GHOST_TEACHING_MAX_SIZE \
    (SUSAMUNE_GHOST_TEACHING_HEADER_SIZE + \
     SUSAMUNE_GHOST_INPUT_MAX_COUNT * SUSAMUNE_GHOST_INPUT_SAMPLE_SIZE + \
     SUSAMUNE_GHOST_SPLIT_MAX_COUNT * SUSAMUNE_GHOST_SPLIT_SAMPLE_SIZE)

struct SusamuneGhostInputSample {
    unsigned int qf;
    struct SusamunePracticeInput input;
};

struct SusamuneGhostSplitSample {
    unsigned int qf;
    unsigned int schema;
    unsigned short route;
    unsigned char endpoint;
    unsigned char reserved;
};

struct SusamuneGhostTeachingHeader {
    unsigned int magic;
    unsigned short version;
    unsigned short headerSize;
    unsigned int inputCount;
    unsigned int splitCount;
    unsigned int flags;
    unsigned int dataChecksum;
    unsigned int reserved[2];
};

static unsigned int SusamuneGhostReadBe32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | p[3];
}

static int SusamuneGhostTeachingHeaderValid(const unsigned char *p,
                                           unsigned int size) {
    unsigned int inputs, splits, stride, maximum;
    if (size < SUSAMUNE_GHOST_TEACHING_HEADER_SIZE ||
        SusamuneGhostReadBe32(p) != SUSAMUNE_GHOST_TEACHING_MAGIC ||
        p[4] != 0 || (p[5] != SUSAMUNE_GHOST_TEACHING_VERSION &&
                     p[5] != SUSAMUNE_GHOST_TEACHING_FLUDD_VERSION) ||
        p[6] != 0 || p[7] != SUSAMUNE_GHOST_TEACHING_HEADER_SIZE)
        return 0;
    inputs = SusamuneGhostReadBe32(p + 8);
    splits = SusamuneGhostReadBe32(p + 12);
    stride = p[5] == SUSAMUNE_GHOST_TEACHING_FLUDD_VERSION
        ? SUSAMUNE_GHOST_V6_INPUT_SAMPLE_SIZE : SUSAMUNE_GHOST_INPUT_SAMPLE_SIZE;
    maximum = p[5] == SUSAMUNE_GHOST_TEACHING_FLUDD_VERSION
        ? SUSAMUNE_GHOST_V6_INPUT_MAX_COUNT : SUSAMUNE_GHOST_INPUT_MAX_COUNT;
    return inputs <= maximum &&
           splits <= SUSAMUNE_GHOST_SPLIT_MAX_COUNT &&
           !(SusamuneGhostReadBe32(p + 16) &
             ~SUSAMUNE_GHOST_TEACHING_INPUT_TRUNCATED) &&
           !SusamuneGhostReadBe32(p + 24) &&
           !SusamuneGhostReadBe32(p + 28) &&
           size == SUSAMUNE_GHOST_TEACHING_HEADER_SIZE +
               inputs * stride +
               splits * SUSAMUNE_GHOST_SPLIT_SAMPLE_SIZE;
}

static int SusamuneGhostTeachingInputValid(const unsigned char *p,
        unsigned int startQf, unsigned int endQf,
        unsigned int previousQf, int first) {
    unsigned int qf = SusamuneGhostReadBe32(p);
    return qf >= startQf && qf <= endQf &&
           (first || qf > previousQf) && p[15] == 0 &&
           !(p[4] & 0xe0) && !(p[5] & 0x80);
}

static int SusamuneGhostTeachingSplitValid(const unsigned char *p,
        unsigned int startQf, unsigned int endQf,
        unsigned int previousQf, unsigned int route, unsigned int schema,
        unsigned int endpoint) {
    unsigned int qf = SusamuneGhostReadBe32(p);
    return qf >= startQf && qf <= endQf &&
           (endpoint == 0 || qf >= previousQf) &&
           SusamuneGhostReadBe32(p + 4) == schema && schema != 0 &&
           (((unsigned int)p[8] << 8) | p[9]) == route &&
           route != 0xffffu && p[10] == endpoint && p[11] == 0;
}

typedef char SusamuneGhostInputSampleSize[
    sizeof(struct SusamuneGhostInputSample) == 16 ? 1 : -1];
typedef char SusamuneGhostSplitSampleSize[
    sizeof(struct SusamuneGhostSplitSample) == 12 ? 1 : -1];
typedef char SusamuneGhostTeachingHeaderSize[
    sizeof(struct SusamuneGhostTeachingHeader) == 32 ? 1 : -1];

#endif
