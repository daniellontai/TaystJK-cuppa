#ifndef G_FLYINGRESPAWN_H
#define G_FLYINGRESPAWN_H

#include "qcommon/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLYING_RESPAWN_MAX_TEST_MS 30000
/* Raw commands also cover the sampled predecessor, up to one second earlier. */
#define FLYING_RESPAWN_SAMPLE_MARGIN_MS 1000

typedef struct {
	vec3_t origin, velocity, viewangles;
	int deltaAngles[3];
	int commandTime, pmFlags, pmTime, groundEntityNum;
	int movementStyle, forcePower;
	float forceJumpZStart;
	int legsAnim, legsTimer, torsoAnim, torsoTimer;
} flyingRespawnMovement_t;

typedef struct {
	int serverTime;
	usercmd_t cmd; /* Original input, before server movement code mutates it. */
} flyingRespawnRawCmd_t;

typedef struct {
	int serverTime;
	flyingRespawnMovement_t movement;
} flyingRespawnSnapshot_t;

typedef struct {
	qboolean valid;
	flyingRespawnSnapshot_t snapshot;
	int secondaryElapsedMs;
} flyingRespawnCapture_t;

typedef struct {
	flyingRespawnSnapshot_t snapshot;
	int endServerTime;
	int firstCommand, commandCount;
	unsigned int historyGeneration;
} flyingRespawnSelection_t;

typedef struct {
	qboolean active, testReplay;
	flyingRespawnSelection_t selection;
	int commandCursor, startServerTime, releaseServerTime;
	int mismatchCount;
	float maxOriginError, maxVelocityError;
} flyingRespawnReplay_t;

typedef struct {
	/* These are distinct capture owners; run restart clears only automatic capture. */
	flyingRespawnCapture_t manualSave, automaticCheckpoint;
	flyingRespawnRawCmd_t *rawCommands;
	flyingRespawnSnapshot_t *movementStates;
	int commandHead, commandCount, stateHead, stateCount, historyCapacity;
	int commandDroppedThrough, stateDroppedThrough;
	unsigned int historyGeneration;
	int rewindMs, releaseOffsetMs, retentionMs;
	qboolean testMode;
	qboolean secondaryRunning;
	int secondaryBaseMs, secondarySegmentStart;
	int respawnCount;
	qboolean runInvalid, mismatch;
	flyingRespawnReplay_t replay;
} flyingRespawnState_t;

/* Init only on a new/zeroed client. Free before disposing/reinitializing a client. */
void G_FlyingRespawn_InitClient(flyingRespawnState_t *state);
void G_FlyingRespawn_FreeHistory(flyingRespawnState_t *state);
qboolean G_FlyingRespawn_Configure(flyingRespawnState_t *state, int rewindMs, int releaseOffsetMs, int testMode);
void G_FlyingRespawn_ResetRun(flyingRespawnState_t *state, int serverTime);
void G_FlyingRespawn_RecordRawCmd(flyingRespawnState_t *state, int serverTime, const usercmd_t *cmd);
void G_FlyingRespawn_RecordState(flyingRespawnState_t *state, int serverTime, const flyingRespawnMovement_t *movement);
qboolean G_FlyingRespawn_SelectHistory(const flyingRespawnState_t *state, int targetServerTime, int endServerTime, flyingRespawnSelection_t *selection);
qboolean G_FlyingRespawn_GetSelectedCmd(const flyingRespawnState_t *state, const flyingRespawnSelection_t *selection, int index, flyingRespawnRawCmd_t *cmd);
qboolean G_FlyingRespawn_BeginTestReplay(flyingRespawnState_t *state, int serverTime, int durationMs);
void G_FlyingRespawn_ResetSecondarySegment(flyingRespawnState_t *state, int serverTime, int baseMs);
int G_FlyingRespawn_GetSecondaryElapsed(const flyingRespawnState_t *state, int serverTime);
qboolean G_FlyingRespawn_IsRunInvalid(const flyingRespawnState_t *state);
int G_FlyingRespawn_GetRespawnCount(const flyingRespawnState_t *state);

struct sqlite3;
/* Inspection returns 0 absent, 1 present, -1 on SQLite error. */
int G_FlyingRespawn_HasRespawnCountColumn(struct sqlite3 *db);
int G_FlyingRespawn_MigrateLocalRun(struct sqlite3 *db);

#ifdef __cplusplus
}
#endif
#endif
