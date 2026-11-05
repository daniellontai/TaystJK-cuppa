#ifndef G_FLYINGRESPAWN_H
#define G_FLYINGRESPAWN_H

#include "qcommon/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLYING_RESPAWN_MAX_TEST_MS 30000
/* Execution commands also cover the sampled predecessor, up to one second earlier. */
#define FLYING_RESPAWN_SAMPLE_MARGIN_MS 1000

typedef struct {
	vec3_t origin, velocity, viewangles;
	int deltaAngles[3];
	int commandTime, pmFlags, pmTime, groundEntityNum;
	int movementStyle, forcePower;
	int forceJumpLevel, savedJumpLevel;
	int forcePowersActive, forcePowerSelected, forceButtonNeedRelease;
	int forcePowerDebounce[NUM_FORCE_POWERS], forcePowerDuration[NUM_FORCE_POWERS];
	int forcePowerRegenDebounceTime, forceJumpAddTime;
	int forceRageRecoveryTime, groundTime;
	int dashTime, wallJumpTime, jumpTime, jetpackFuel, jetpackFlags;
	float forceJumpZStart, forceJumpCharge;
	int legsAnim, legsTimer, torsoAnim, torsoTimer;
	int weapon, weaponstate, weaponTime, saberMove;
	int saberAnimLevel, saberDrawAnimLevel, saberAnimLevelBase, saberHolstered;
	int pmType, bobCycle, movementDir, lastOnGround, gravity, basespeed;
	float speed;
	vec3_t moveDir;
	int checkpoints, courseID;
	int jetPackDebReduce; /* Client-owned absolute Tribes Force-drain deadline. */
	int jetPackDebRecharge, overheatRechargeDebReduce;
	qboolean fjDidJump, jetPackOn;
} flyingRespawnMovement_t;

typedef struct {
	int serverTime;
	usercmd_t cmd; /* Original input, before server movement code mutates it. */
} flyingRespawnCmd_t;

typedef struct {
	int serverTime;
	flyingRespawnMovement_t movement;
} flyingRespawnSnapshot_t;

typedef struct {
	qboolean disabled, hasRewind, hasRelease;
	int rewindMs, releaseMs;
} flyingRespawnCheckpointConfig_t;

typedef struct {
	qboolean valid;
	int checkpointEntityNum;
	flyingRespawnSnapshot_t snapshot;
	flyingRespawnSnapshot_t start, handoff;
	usercmd_t startCmd;
	/* Frozen applied commands, separate from rolling execution history. */
	flyingRespawnCmd_t *commands;
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
} flyingRespawnSelection_t;

typedef struct {
	qboolean active, testReplay, manualReplay, staticReplay;
	qboolean checkpointTouched;
	flyingRespawnSelection_t selection;
	int commandCursor, startServerTime, releaseServerTime;
	int mismatchCount;
	float maxOriginError, maxVelocityError;
	usercmd_t liveCmd;
	usercmd_t frameCmd;
	qboolean hasLiveCmd;
	flyingRespawnMovement_t expectedMovement;
	flyingRespawnSnapshot_t syntheticContact;
	int expectedSecondaryMs;
} flyingRespawnReplay_t;

typedef struct {
	/* These are distinct capture owners; run restart clears only automatic capture. */
	flyingRespawnCapture_t manualSave, automaticCheckpoint;
	/* Commands actually passed to normal movement, including coalesced/forced server steps. */
	flyingRespawnCmd_t *executionCommands;
	flyingRespawnSnapshot_t *movementStates;
	int stateHead, stateCount, historyCapacity;
	int stateDroppedThrough;
	int executionHead, executionCount, executionDroppedThrough;
	unsigned int historyGeneration;
	int historyContinuousSince; /* Rewind may shorten at a replay handoff or external setup. */
	qboolean historyBoundaryBeforeMovement;
	qboolean historyNoclip; /* Noclip movement occurred; await the first normal-mode boundary. */
	int rewindMs, releaseOffsetMs, retentionMs;
	qboolean testMode;
	qboolean secondaryRunning;
	int secondaryBaseMs, secondarySegmentStart;
	int respawnCount;
	/* Brushes may overlap: each contact gap must survive touches of other entities. */
	byte checkpointContactSeen[MAX_GENTITIES];
	int checkpointTouchTimes[MAX_GENTITIES];
	qboolean runInvalid, mismatch, testReplayUsed;
	flyingRespawnReplay_t replay;
} flyingRespawnState_t;

/* Init only on a new/zeroed client. Free before disposing/reinitializing a client. */
void G_FlyingRespawn_InitClient(flyingRespawnState_t *state);
/* Lifecycle disposal includes frozen owners. Configuration frees only rolling history. */
void G_FlyingRespawn_FreeClient(flyingRespawnState_t *state);
qboolean G_FlyingRespawn_Configure(flyingRespawnState_t *state, int rewindMs, int releaseOffsetMs, int testMode);
/* Optional malformed values inherit; a negative mapper release clamps to zero. */
void G_FlyingRespawn_ParseCheckpointConfig(flyingRespawnCheckpointConfig_t *config, const char *disabled, const char *rewind, const char *release);
/* Retain the largest map override, while snapshotting defaults independently. */
qboolean G_FlyingRespawn_OnTimerStart(flyingRespawnState_t *state, int serverTime, int rewindMs, int releaseMs, int testMode, int mapRewindMs);
/* Call after normal progress. Every new crossing discards the prior owner; continuous
 * same-entity touches (at most 250 ms apart) keep the original contact/deadline. */
qboolean G_FlyingRespawn_OnCheckpointTouch(flyingRespawnState_t *state, int serverTime, int entityNum, const flyingRespawnCheckpointConfig_t *config, const flyingRespawnMovement_t *movement);
qboolean G_FlyingRespawn_IsExpectedCheckpointTouch(const flyingRespawnState_t *state, int entityNum);
/* Also used by static second-respawn after installing the recorded contact pose. */
qboolean G_FlyingRespawn_ApplyExpectedCheckpointProgress(flyingRespawnState_t *state, flyingRespawnMovement_t *movement);
void G_FlyingRespawn_RecordExecutionCmd(flyingRespawnState_t *state, int serverTime, const usercmd_t *cmd);
void G_FlyingRespawn_RecordState(flyingRespawnState_t *state, int serverTime, const flyingRespawnMovement_t *movement);
/* Start a continuous stream before live movement after replay or external setup. */
void G_FlyingRespawn_RecordHandoff(flyingRespawnState_t *state, int serverTime, const flyingRespawnMovement_t *movement);
qboolean G_FlyingRespawn_BeginTestReplay(flyingRespawnState_t *state, int serverTime, int durationMs);
qboolean G_FlyingRespawn_BeginCheckpointReplay(flyingRespawnState_t *state, int serverTime, qboolean enabled);
void G_FlyingRespawn_BufferLiveCmd(flyingRespawnState_t *state, const usercmd_t *cmd);
/* Non-consuming authoritative input for frame consumers; false freezes static Force. */
qboolean G_FlyingRespawn_FrameCmd(flyingRespawnState_t *state, int serverTime, usercmd_t *cmd);
/* Preserve static Force timer intervals if the first GO tick is after its deadline. */
void G_FlyingRespawn_ReleaseStaticForce(const flyingRespawnState_t *state, int serverTime, playerState_t *ps, int *jetPackDebReduce);
int G_FlyingRespawn_ShiftDeadline(int deadline, int timeOffset);
/* Only returns recorded movement steps when they are due; live input stays buffered. */
qboolean G_FlyingRespawn_OverrideCmd(flyingRespawnState_t *state, int serverTime, usercmd_t *cmd);
/* Returns true once at GO, after validating the pre-live state and installing live input. */
qboolean G_FlyingRespawn_Advance(flyingRespawnState_t *state, int serverTime, const flyingRespawnMovement_t *actual, usercmd_t *cmd);
/* Returns a ready-to-install contact snapshot with already shifted absolute timestamps. */
qboolean G_FlyingRespawn_HandleSecondPress(flyingRespawnState_t *state, int serverTime, flyingRespawnSnapshot_t *contact);
int G_FlyingRespawn_QuantizeCmdTime(const flyingRespawnState_t *state, int commandTime, int quantumMs);
qboolean G_FlyingRespawn_CancelReplay(flyingRespawnState_t *state, int serverTime, usercmd_t *cmd);
void G_FlyingRespawn_ReadMovement(flyingRespawnMovement_t *movement, const playerState_t *ps, int movementStyle, int checkpoints, int courseID);
void G_FlyingRespawn_WriteMovement(playerState_t *ps, const flyingRespawnMovement_t *movement, int timeOffset);
qboolean G_FlyingRespawn_CaptureManual(flyingRespawnState_t *state, int serverTime, const flyingRespawnMovement_t *movement);
void G_FlyingRespawn_ClearAutomaticCheckpoint(flyingRespawnState_t *state);
qboolean G_FlyingRespawn_BeginManualReplay(flyingRespawnState_t *state, int serverTime, qboolean practice);
/* True once, after all recorded movement ran; caller may then restore the practice timer. */
qboolean G_FlyingRespawn_FinishManualReplay(flyingRespawnState_t *state, int serverTime, qboolean practice);
qboolean G_FlyingRespawn_CaptureAutomaticHandoff(flyingRespawnState_t *state, int serverTime, const flyingRespawnMovement_t *movement);
void G_FlyingRespawn_ResetSecondarySegment(flyingRespawnState_t *state, int serverTime, int baseMs);
int G_FlyingRespawn_GetSecondaryElapsed(const flyingRespawnState_t *state, int serverTime);
qboolean G_FlyingRespawn_IsRunInvalid(const flyingRespawnState_t *state);
int G_FlyingRespawn_GetRespawnCount(const flyingRespawnState_t *state);

#ifdef __cplusplus
}
#endif
#endif
