#pragma once

#include "qcommon/q_shared.h"
#include <stdint.h>

typedef struct {
    qboolean running, coopStarted;
    int64_t commandWallOffset;
    int elapsedMs, levelElapsedMs, secondaryElapsedMs, respawnCount, startLag;
    float displacement;
    int displacementSamples;
    float topSpeed;
} practiceTimerSnapshot_t;

struct gentity_s;
qboolean G_SavePosition(struct gentity_s *ent, qboolean announce);
void G_PracticeBookmark_RestoreAtEntry(struct gentity_s *ent, int rewindMs);
void G_PracticeBookmark_RestoreAtRelease(struct gentity_s *ent);
