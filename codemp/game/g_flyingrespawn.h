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
	int forcePowersActive, forcePowerSelected, forceButtonNeedRelease;
	int forcePowerDebounce[NUM_FORCE_POWERS], forcePowerDuration[NUM_FORCE_POWERS];
	int forcePowerRegenDebounceTime, forceJumpAddTime;
	float forceJumpZStart, forceJumpCharge;
	int legsAnim, legsTimer, torsoAnim, torsoTimer;
	int pmType, bobCycle, movementDir, lastOnGround, gravity, basespeed;
	float speed;
	vec3_t moveDir;
	int checkpoints, courseID;
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
	flyingRespawnSnapshot_t start, handoff;
	/* Frozen applied commands, separate from the immutable raw receipt ring. */
	flyingRespawnRawCmd_t *commands;
	int commandCount, commandCapacity;
	qboolean pending;
	int releaseServerTime;
	int secondaryElapsedMs;
} flyingRespawnCapture_t;

typedef struct {
	flyingRespawnSnapshot_t snapshot;
	int endServerTime;
	int firstCommand, commandCount;
	unsigned int historyGeneration;
	qboolean executionHistory;
} flyingRespawnSelection_t;

typedef struct {
	qboolean active, testReplay, manualReplay;
	flyingRespawnSelection_t selection;
	int commandCursor, startServerTime, releaseServerTime;
	int mismatchCount;
	float maxOriginError, maxVelocityError;
	usercmd_t liveCmd;
	qboolean hasLiveCmd;
} flyingRespawnReplay_t;

typedef struct {
	/* These are distinct capture owners; run restart clears only automatic capture. */
	flyingRespawnCapture_t manualSave, automaticCheckpoint;
	flyingRespawnRawCmd_t *rawCommands;
	/* Commands actually passed to normal movement, including coalesced/forced server steps. */
	flyingRespawnRawCmd_t *executionCommands;
	flyingRespawnSnapshot_t *movementStates;
	int commandHead, commandCount, stateHead, stateCount, historyCapacity;
	int commandDroppedThrough, stateDroppedThrough;
	int executionHead, executionCount, executionDroppedThrough;
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
/* Lifecycle disposal includes frozen owners. Configuration frees only rolling history. */
void G_FlyingRespawn_FreeClient(flyingRespawnState_t *state);
qboolean G_FlyingRespawn_Configure(flyingRespawnState_t *state, int rewindMs, int releaseOffsetMs, int testMode);
void G_FlyingRespawn_ResetRun(flyingRespawnState_t *state, int serverTime);
void G_FlyingRespawn_RecordRawCmd(flyingRespawnState_t *state, int serverTime, const usercmd_t *cmd);
void G_FlyingRespawn_RecordExecutionCmd(flyingRespawnState_t *state, int serverTime, const usercmd_t *cmd);
void G_FlyingRespawn_RecordState(flyingRespawnState_t *state, int serverTime, const flyingRespawnMovement_t *movement);
qboolean G_FlyingRespawn_SelectHistory(const flyingRespawnState_t *state, int targetServerTime, int endServerTime, flyingRespawnSelection_t *selection);
qboolean G_FlyingRespawn_SelectExecutionHistory(const flyingRespawnState_t *state, int targetServerTime, int endServerTime, flyingRespawnSelection_t *selection);
qboolean G_FlyingRespawn_GetSelectedCmd(const flyingRespawnState_t *state, const flyingRespawnSelection_t *selection, int index, flyingRespawnRawCmd_t *cmd);
qboolean G_FlyingRespawn_BeginTestReplay(flyingRespawnState_t *state, int serverTime, int durationMs);
void G_FlyingRespawn_ReadMovement(flyingRespawnMovement_t *movement, const playerState_t *ps, int movementStyle, int checkpoints, int courseID);
void G_FlyingRespawn_WriteMovement(playerState_t *ps, const flyingRespawnMovement_t *movement, int timeOffset);
qboolean G_FlyingRespawn_CaptureManual(flyingRespawnState_t *state, int serverTime, const flyingRespawnMovement_t *movement);
qboolean G_FlyingRespawn_BeginManualReplay(flyingRespawnState_t *state, int serverTime, qboolean practice);
qboolean G_FlyingRespawn_NextManualCmd(flyingRespawnState_t *state, int serverTime, flyingRespawnRawCmd_t *cmd);
/* True once, after all recorded movement ran; caller may then restore the practice timer. */
qboolean G_FlyingRespawn_FinishManualReplay(flyingRespawnState_t *state, int serverTime, qboolean practice);
qboolean G_FlyingRespawn_CaptureAutomaticStart(flyingRespawnState_t *state, int serverTime, const flyingRespawnMovement_t *movement, int rewindMs, int releaseOffsetMs);
qboolean G_FlyingRespawn_CaptureAutomaticHandoff(flyingRespawnState_t *state, int serverTime, const flyingRespawnMovement_t *movement);
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
