#include "g_flyingrespawn.h"
#include "bg_public.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static qboolean FR_NextManualCmd(flyingRespawnState_t *, int, flyingRespawnCmd_t *);
static qboolean FR_CaptureAutomaticStart(flyingRespawnState_t *, int, const flyingRespawnMovement_t *, int, int);
static int FR_ShiftTime(int time, int offset);

static int FR_Index(int head, int offset, int capacity)
{
	return (int)(((size_t)head + offset) % capacity);
}

static void FR_ClearCapture(flyingRespawnCapture_t *capture)
{
	free(capture->commands);
	memset(capture, 0, sizeof(*capture));
}

static qboolean FR_AppendCapture(flyingRespawnCapture_t *capture, const flyingRespawnCmd_t *cmd)
{
	if (capture->commandCount == capture->commandCapacity) {
		int capacity;
		flyingRespawnCmd_t *commands;
		if (capture->commandCapacity > INT_MAX / 2) return qfalse;
		capacity = capture->commandCapacity ? capture->commandCapacity * 2 : 32;
		if ((size_t)capacity > SIZE_MAX / sizeof(*commands)) return qfalse;
		commands = (flyingRespawnCmd_t *)realloc(capture->commands, (size_t)capacity * sizeof(*commands));
		if (!commands) return qfalse;
		capture->commands = commands;
		capture->commandCapacity = capacity;
	}
	capture->commands[capture->commandCount++] = *cmd;
	return qtrue;
}

void G_FlyingRespawn_InitClient(flyingRespawnState_t *state)
{
	memset(state, 0, sizeof(*state));
	state->stateDroppedThrough = INT_MIN;
	state->executionDroppedThrough = INT_MIN;
	state->historyContinuousSince = INT_MIN;
	state->historyBoundaryBeforeMovement = qfalse;
}

static void FR_FreeHistory(flyingRespawnState_t *state)
{
	free(state->executionCommands);
	free(state->movementStates);
	state->executionCommands = NULL;
	state->movementStates = NULL;
	state->historyCapacity = 0;
	state->stateHead = state->stateCount = 0;
	state->executionHead = state->executionCount = 0;
	state->stateDroppedThrough = INT_MIN;
	state->executionDroppedThrough = INT_MIN;
	state->historyContinuousSince = INT_MIN;
	state->historyBoundaryBeforeMovement = qfalse;
	state->historyGeneration++;
	memset(&state->replay, 0, sizeof(state->replay));
}

void G_FlyingRespawn_FreeClient(flyingRespawnState_t *state)
{
	FR_FreeHistory(state);
	FR_ClearCapture(&state->manualSave);
	FR_ClearCapture(&state->automaticCheckpoint);
}

static qboolean FR_Configure(flyingRespawnState_t *state, int rewindMs, int releaseOffsetMs, int testMode, int mapRewindMs)
{
	int retention, capacity;
	flyingRespawnCmd_t *executions;
	flyingRespawnSnapshot_t *snapshots;
	if (rewindMs < 0 || releaseOffsetMs < 0 || rewindMs > INT_MAX - FLYING_RESPAWN_SAMPLE_MARGIN_MS - 2)
		return qfalse;
	retention = mapRewindMs > rewindMs ? mapRewindMs : rewindMs;
	if (retention > INT_MAX - FLYING_RESPAWN_SAMPLE_MARGIN_MS - 2) return qfalse;
	if (testMode == 1 && retention < FLYING_RESPAWN_MAX_TEST_MS) retention = FLYING_RESPAWN_MAX_TEST_MS;
	capacity = retention + FLYING_RESPAWN_SAMPLE_MARGIN_MS + 2; /* At most one distinct effective movement command per millisecond. */
	if (state->historyCapacity == capacity && state->rewindMs == rewindMs && state->testMode == (testMode == 1)) {
		state->releaseOffsetMs = releaseOffsetMs;
		return qtrue;
	}
	if ((size_t)capacity > SIZE_MAX / sizeof(*executions) || (size_t)capacity > SIZE_MAX / sizeof(*snapshots))
		return qfalse;
	executions = (flyingRespawnCmd_t *)malloc((size_t)capacity * sizeof(*executions));
	snapshots = (flyingRespawnSnapshot_t *)malloc((size_t)capacity * sizeof(*snapshots));
	if (!executions || !snapshots) {
		free(executions);
		free(snapshots);
		return qfalse;
	}
	FR_FreeHistory(state);
	state->executionCommands = executions;
	state->movementStates = snapshots;
	state->historyCapacity = capacity;
	state->rewindMs = rewindMs;
	state->releaseOffsetMs = releaseOffsetMs;
	state->retentionMs = retention;
	state->testMode = testMode == 1 ? qtrue : qfalse;
	return qtrue;
}

qboolean G_FlyingRespawn_Configure(flyingRespawnState_t *state, int rewindMs, int releaseOffsetMs, int testMode)
{
	return FR_Configure(state, rewindMs, releaseOffsetMs, testMode, rewindMs);
}

void G_FlyingRespawn_ResetSecondarySegment(flyingRespawnState_t *state, int serverTime, int baseMs)
{
	state->secondaryBaseMs = baseMs < 0 ? 0 : baseMs;
	state->secondarySegmentStart = serverTime;
	state->secondaryRunning = qtrue;
}

int G_FlyingRespawn_GetSecondaryElapsed(const flyingRespawnState_t *state, int serverTime)
{
	int64_t elapsed = state->secondaryBaseMs;
	if (state->secondaryRunning && serverTime > state->secondarySegmentStart)
		elapsed += (int64_t)serverTime - state->secondarySegmentStart;
	return elapsed > INT_MAX ? INT_MAX : (int)elapsed;
}

static void FR_ResetRun(flyingRespawnState_t *state, int serverTime)
{
	FR_ClearCapture(&state->automaticCheckpoint);
	memset(&state->replay, 0, sizeof(state->replay));
	state->respawnCount = 0;
	memset(state->checkpointContactSeen, 0, sizeof(state->checkpointContactSeen));
	state->runInvalid = state->mismatch = state->testReplayUsed = qfalse;
	state->stateHead = state->stateCount = 0;
	state->executionHead = state->executionCount = 0;
	state->stateDroppedThrough = INT_MIN;
	state->executionDroppedThrough = INT_MIN;
	state->historyContinuousSince = INT_MIN;
	state->historyBoundaryBeforeMovement = qfalse;
	state->historyGeneration++;
	G_FlyingRespawn_ResetSecondarySegment(state, serverTime, 0);
}

static qboolean FR_ParseMilliseconds(const char *text, int *value)
{
	char *end;
	long parsed;
	if (!text || !*text) return qfalse;
	errno = 0;
	parsed = strtol(text, &end, 10);
	if (errno == ERANGE || end == text || *end || parsed < INT_MIN || parsed > INT_MAX) return qfalse;
	*value = (int)parsed;
	return qtrue;
}

void G_FlyingRespawn_ParseCheckpointConfig(flyingRespawnCheckpointConfig_t *config, const char *disabled, const char *rewind, const char *release)
{
	int value;
	memset(config, 0, sizeof(*config));
	config->disabled = FR_ParseMilliseconds(disabled, &value) && value == 1 ? qtrue : qfalse;
	if (FR_ParseMilliseconds(rewind, &value) && value >= 0 && value <= INT_MAX - FLYING_RESPAWN_SAMPLE_MARGIN_MS - 2) {
		config->hasRewind = qtrue;
		config->rewindMs = value;
	}
	if (FR_ParseMilliseconds(release, &value)) {
		config->hasRelease = qtrue;
		config->releaseMs = value < 0 ? 0 : value;
	}
}

qboolean G_FlyingRespawn_OnTimerStart(flyingRespawnState_t *state, int time, int rewindMs, int releaseMs, int testMode, int mapRewindMs)
{
	qboolean configured = FR_Configure(state, rewindMs, releaseMs, testMode, mapRewindMs);
	/* A failed configuration must not expose the preceding run's checkpoint. */
	FR_ResetRun(state, time);
	if (!configured) FR_FreeHistory(state);
	return configured;
}

qboolean G_FlyingRespawn_OnCheckpointTouch(flyingRespawnState_t *state, int time, int entityNum, const flyingRespawnCheckpointConfig_t *config, const flyingRespawnMovement_t *movement)
{
	int rewindMs, releaseMs;
	if (entityNum < 0 || entityNum >= MAX_GENTITIES) return qfalse;
	if (state->replay.active) {
		/* Keep every overlapping contact continuous across the replay-to-live handoff. */
		state->checkpointContactSeen[entityNum] = 1;
		state->checkpointTouchTimes[entityNum] = time;
		return qfalse;
	}
	/* Engine touches repeat while inside a brush. A gap permits a new crossing. */
	if (state->checkpointContactSeen[entityNum] && time >= state->checkpointTouchTimes[entityNum] &&
		(int64_t)time - state->checkpointTouchTimes[entityNum] <= 250) {
		state->checkpointTouchTimes[entityNum] = time;
		return qfalse;
	}
	state->checkpointContactSeen[entityNum] = 1;
	state->checkpointTouchTimes[entityNum] = time;
	FR_ClearCapture(&state->automaticCheckpoint);
	if (!state->secondaryRunning || !config || config->disabled) return qfalse;
	rewindMs = config->hasRewind ? config->rewindMs : state->rewindMs;
	releaseMs = config->hasRelease ? config->releaseMs : state->releaseOffsetMs;
	if (!FR_CaptureAutomaticStart(state, time, movement, rewindMs, releaseMs)) return qfalse;
	state->automaticCheckpoint.checkpointEntityNum = entityNum;
	return qtrue;
}

qboolean G_FlyingRespawn_IsExpectedCheckpointTouch(const flyingRespawnState_t *state, int entityNum)
{
	return state->replay.active && !state->replay.manualReplay && !state->replay.testReplay &&
		state->automaticCheckpoint.valid && state->automaticCheckpoint.checkpointEntityNum == entityNum;
}

qboolean G_FlyingRespawn_ApplyExpectedCheckpointProgress(flyingRespawnState_t *state, flyingRespawnMovement_t *movement)
{
	if (!movement || !state->automaticCheckpoint.valid) return qfalse;
	movement->checkpoints = state->automaticCheckpoint.snapshot.movement.checkpoints;
	movement->courseID = state->automaticCheckpoint.snapshot.movement.courseID;
	if (state->replay.active && !state->replay.manualReplay && !state->replay.testReplay)
		state->replay.checkpointTouched = qtrue;
	return qtrue;
}

void G_FlyingRespawn_RecordExecutionCmd(flyingRespawnState_t *state, int serverTime, const usercmd_t *cmd)
{
	int slot;
	int64_t cutoff = (int64_t)serverTime - state->retentionMs - FLYING_RESPAWN_SAMPLE_MARGIN_MS;
	if (!state->historyCapacity || !cmd || state->replay.active) return;
	if (state->executionCount && serverTime < state->executionCommands[FR_Index(state->executionHead, state->executionCount - 1, state->historyCapacity)].serverTime) return;
	while (state->executionCount > 1 && state->executionCommands[FR_Index(state->executionHead, 1, state->historyCapacity)].serverTime <= cutoff) {
		state->executionDroppedThrough = state->executionCommands[state->executionHead].serverTime;
		state->executionHead = FR_Index(state->executionHead, 1, state->historyCapacity);
		state->executionCount--;
	}
	if (state->executionCount == state->historyCapacity) {
		state->executionDroppedThrough = state->executionCommands[state->executionHead].serverTime;
		state->executionHead = FR_Index(state->executionHead, 1, state->historyCapacity);
		state->executionCount--;
	}
	slot = FR_Index(state->executionHead, state->executionCount++, state->historyCapacity);
	state->executionCommands[slot].serverTime = serverTime;
	state->executionCommands[slot].cmd = *cmd;
	if (state->automaticCheckpoint.pending && !FR_AppendCapture(&state->automaticCheckpoint, &state->executionCommands[slot]))
		FR_ClearCapture(&state->automaticCheckpoint);
	state->historyGeneration++;
}

void G_FlyingRespawn_RecordState(flyingRespawnState_t *state, int serverTime, const flyingRespawnMovement_t *movement)
{
	int slot;
	int64_t cutoff = (int64_t)serverTime - state->retentionMs;
	if (!state->historyCapacity || !movement || state->replay.active) return;
	if (state->stateCount) {
		slot = FR_Index(state->stateHead, state->stateCount - 1, state->historyCapacity);
		if (serverTime < state->movementStates[slot].serverTime) return;
		if (serverTime == state->movementStates[slot].serverTime) {
			/* Keep a pre-movement discontinuity snapshot and its same-tick commands. */
			if (serverTime == state->historyContinuousSince && state->historyBoundaryBeforeMovement) return;
			state->movementStates[slot].movement = *movement;
			state->historyGeneration++;
			return;
		}
	}
	while (state->stateCount > 1 && state->movementStates[FR_Index(state->stateHead, 1, state->historyCapacity)].serverTime <= cutoff) {
		state->stateDroppedThrough = state->movementStates[state->stateHead].serverTime;
		state->stateHead = FR_Index(state->stateHead, 1, state->historyCapacity);
		state->stateCount--;
	}
	if (state->stateCount == state->historyCapacity) {
		state->stateDroppedThrough = state->movementStates[state->stateHead].serverTime;
		state->stateHead = FR_Index(state->stateHead, 1, state->historyCapacity);
		state->stateCount--;
	}
	slot = FR_Index(state->stateHead, state->stateCount++, state->historyCapacity);
	state->movementStates[slot].serverTime = serverTime;
	state->movementStates[slot].movement = *movement;
	state->historyGeneration++;
}

static qboolean FR_SelectHistory(const flyingRespawnState_t *state, int targetServerTime, int endServerTime, flyingRespawnSelection_t *selection)
{
	const flyingRespawnCmd_t *commands = state->executionCommands;
	int head = state->executionHead;
	int count = state->executionCount;
	int dropped = state->executionDroppedThrough;
	int i, snapshot = -1;
	flyingRespawnSelection_t result;
	if (!selection || targetServerTime > endServerTime || !state->historyCapacity) return qfalse;
	if (endServerTime < state->historyContinuousSince) return qfalse;
	if (targetServerTime < state->historyContinuousSince) targetServerTime = state->historyContinuousSince;
	memset(&result, 0, sizeof(result));
	for (i = 0; i < state->stateCount; i++) {
		int slot = FR_Index(state->stateHead, i, state->historyCapacity);
		if (state->movementStates[slot].serverTime > targetServerTime) break;
		snapshot = slot;
	}
	if (snapshot < 0) return qfalse;
	result.snapshot = state->movementStates[snapshot];
	if (result.snapshot.serverTime < dropped) return qfalse;
	result.endServerTime = endServerTime;
	result.historyGeneration = state->historyGeneration;
	for (i = 0; i < count; i++) {
		int time = commands[FR_Index(head, i, state->historyCapacity)].serverTime;
		if (time < result.snapshot.serverTime || (time == result.snapshot.serverTime &&
			!(state->historyBoundaryBeforeMovement && time == state->historyContinuousSince))) continue;
		if (time > endServerTime) break;
		if (!result.commandCount) result.firstCommand = i;
		result.commandCount++;
	}
	*selection = result;
	return qtrue;
}

static qboolean FR_GetSelectedCmd(const flyingRespawnState_t *state, const flyingRespawnSelection_t *selection, int index, flyingRespawnCmd_t *cmd)
{
	if (!selection || !cmd || selection->historyGeneration != state->historyGeneration || index < 0 || index >= selection->commandCount) return qfalse;
	*cmd = state->executionCommands[FR_Index(state->executionHead, selection->firstCommand + index, state->historyCapacity)];
	return qtrue;
}

static void FR_ShiftMovement(flyingRespawnMovement_t *movement, int offset)
{
	playerState_t ps;
	int jetPackDebReduce = G_FlyingRespawn_ShiftDeadline(movement->jetPackDebReduce, offset);
	int jetPackDebRecharge = G_FlyingRespawn_ShiftDeadline(movement->jetPackDebRecharge, offset);
	int overheatRechargeDebReduce = G_FlyingRespawn_ShiftDeadline(movement->overheatRechargeDebReduce, offset);
	int savedJumpLevel = movement->savedJumpLevel;
	qboolean fjDidJump = movement->fjDidJump, jetPackOn = movement->jetPackOn;
	memset(&ps, 0, sizeof(ps));
	G_FlyingRespawn_WriteMovement(&ps, movement, offset);
	G_FlyingRespawn_ReadMovement(movement, &ps, movement->movementStyle, movement->checkpoints, movement->courseID);
	movement->jetPackDebReduce = jetPackDebReduce;
	movement->jetPackDebRecharge = jetPackDebRecharge;
	movement->overheatRechargeDebReduce = overheatRechargeDebReduce;
	movement->savedJumpLevel = savedJumpLevel;
	movement->fjDidJump = fjDidJump;
	movement->jetPackOn = jetPackOn;
}

int G_FlyingRespawn_ShiftDeadline(int deadline, int offset)
{
	return deadline ? FR_ShiftTime(deadline, offset) : 0;
}

static usercmd_t FR_StartCmd(const flyingRespawnState_t *state, int time)
{
	usercmd_t cmd;
	int i;
	memset(&cmd, 0, sizeof(cmd));
	for (i = 0; i < state->executionCount; ++i) {
		const flyingRespawnCmd_t *entry = &state->executionCommands[FR_Index(state->executionHead, i, state->historyCapacity)];
		if (entry->serverTime > time) break;
		cmd = entry->cmd;
	}
	return cmd;
}

qboolean G_FlyingRespawn_BeginTestReplay(flyingRespawnState_t *state, int serverTime, int durationMs)
{
	flyingRespawnSelection_t selection;
	int i;
	const flyingRespawnSnapshot_t *end = NULL;
	if (!state->testMode || state->replay.active || durationMs < 0 || durationMs > FLYING_RESPAWN_MAX_TEST_MS || (int64_t)serverTime - durationMs < INT_MIN) return qfalse;
	/* A boundary seed intentionally precedes same-tick execution. Wait for
	 * a later post-movement state before using it as a diagnostic endpoint. */
	if (state->historyBoundaryBeforeMovement && state->historyCapacity && state->stateCount && state->executionCount &&
		state->movementStates[FR_Index(state->stateHead, state->stateCount - 1, state->historyCapacity)].serverTime == state->historyContinuousSince &&
		state->executionCommands[FR_Index(state->executionHead, state->executionCount - 1, state->historyCapacity)].serverTime >= state->historyContinuousSince) return qfalse;
	if (!FR_SelectHistory(state, serverTime - durationMs, serverTime, &selection)) return qfalse;
	if ((int64_t)serverTime - selection.snapshot.serverTime > FLYING_RESPAWN_MAX_TEST_MS) return qfalse;
	if ((int64_t)serverTime + serverTime - selection.snapshot.serverTime > INT_MAX) return qfalse;
	for (i = 0; i < state->stateCount; ++i) {
		const flyingRespawnSnapshot_t *snapshot = &state->movementStates[FR_Index(state->stateHead, i, state->historyCapacity)];
		if (snapshot->serverTime > serverTime) break;
		end = snapshot;
	}
	if (!end) return qfalse;
	memset(&state->replay, 0, sizeof(state->replay));
	state->replay.active = state->replay.testReplay = qtrue;
	state->replay.selection = selection;
	state->replay.startServerTime = serverTime;
	state->replay.frameCmd = FR_StartCmd(state, selection.snapshot.serverTime);
	state->replay.frameCmd.serverTime = FR_ShiftTime(state->replay.frameCmd.serverTime, serverTime - selection.snapshot.serverTime);
	state->replay.releaseServerTime = serverTime + serverTime - selection.snapshot.serverTime;
	state->replay.expectedMovement = end->movement;
	state->replay.syntheticContact = *end;
	FR_ShiftMovement(&state->replay.expectedMovement, serverTime - selection.snapshot.serverTime);
	state->replay.expectedSecondaryMs = G_FlyingRespawn_GetSecondaryElapsed(state, serverTime);
	state->secondaryBaseMs = state->replay.expectedSecondaryMs;
	state->secondaryRunning = qfalse;
	state->runInvalid = state->testReplayUsed = qtrue;
	return qtrue;
}

qboolean G_FlyingRespawn_BeginCheckpointReplay(flyingRespawnState_t *state, int time, qboolean enabled)
{
	flyingRespawnCapture_t *capture = &state->automaticCheckpoint;
	int64_t duration = (int64_t)capture->handoff.serverTime - capture->start.serverTime;
	if (!enabled || state->replay.active || !capture->valid || capture->pending || duration < 0 ||
		(int64_t)time + duration > INT_MAX) return qfalse;
	memset(&state->replay, 0, sizeof(state->replay));
	state->replay.active = qtrue;
	state->replay.selection.snapshot = capture->start;
	state->replay.selection.endServerTime = capture->handoff.serverTime;
	state->replay.selection.commandCount = capture->commandCount;
	state->replay.startServerTime = time;
	state->replay.frameCmd = capture->startCmd;
	state->replay.frameCmd.serverTime = FR_ShiftTime(state->replay.frameCmd.serverTime, time - capture->start.serverTime);
	state->replay.releaseServerTime = time + (int)duration;
	state->replay.expectedMovement = capture->handoff.movement;
	FR_ShiftMovement(&state->replay.expectedMovement, time - capture->start.serverTime);
	state->replay.expectedSecondaryMs = capture->secondaryElapsedMs;
	state->secondaryBaseMs = capture->secondaryElapsedMs;
	state->secondaryRunning = qfalse;
	if (state->respawnCount < INT_MAX) state->respawnCount++;
	/* A zero-duration capture starts at the post-contact state already. */
	state->replay.checkpointTouched = capture->start.serverTime >= capture->snapshot.serverTime;
	return qtrue;
}

void G_FlyingRespawn_BufferLiveCmd(flyingRespawnState_t *state, const usercmd_t *cmd)
{
	if (!state->replay.active || !cmd) return;
	state->replay.liveCmd = *cmd;
	state->replay.hasLiveCmd = qtrue;
}

void G_FlyingRespawn_AcknowledgeStaticInput(const flyingRespawnState_t *state, int serverTime, playerState_t *ps)
{
	int acknowledged;
	if (!state || !ps || !state->replay.active || !state->replay.staticReplay ||
		state->replay.manualReplay || !state->replay.hasLiveCmd) return;
	acknowledged = state->replay.liveCmd.serverTime;
	if (acknowledged > serverTime) acknowledged = serverTime;
	if (ps->commandTime < acknowledged) ps->commandTime = acknowledged;
}

int G_FlyingRespawn_QuantizeCmdTime(const flyingRespawnState_t *state, int time, int quantum)
{
	int64_t offset = state->replay.active ?
		(int64_t)state->replay.startServerTime - state->replay.selection.snapshot.serverTime : 0;
	int64_t rounded;
	if (quantum < 1) return time;
	/* Quantize on the source clock, then relocate. Otherwise an offset changes
	 * fixed-pmove step sizes even though the execution stream is identical. */
	rounded = (((int64_t)time - offset + quantum - 1) / quantum) * quantum + offset;
	if (rounded > INT_MAX) return INT_MAX;
	return rounded < INT_MIN ? INT_MIN : (int)rounded;
}

qboolean G_FlyingRespawn_CancelReplay(flyingRespawnState_t *state, int time, usercmd_t *cmd)
{
	if (!state->replay.active || !cmd) return qfalse;
	if (state->replay.hasLiveCmd) *cmd = state->replay.liveCmd;
	else memset(cmd, 0, sizeof(*cmd));
	cmd->serverTime = time;
	state->replay.active = qfalse;
	return qtrue;
}

qboolean G_FlyingRespawn_OverrideCmd(flyingRespawnState_t *state, int time, usercmd_t *cmd)
{
	flyingRespawnReplay_t *replay = &state->replay;
	flyingRespawnCmd_t next;
	if (!cmd || !replay->active || replay->staticReplay || replay->commandCursor >= replay->selection.commandCount) return qfalse;
	if (replay->manualReplay) {
		if (!FR_NextManualCmd(state, time, &next)) return qfalse;
	} else {
		if (replay->testReplay) {
			if (!FR_GetSelectedCmd(state, &replay->selection, replay->commandCursor, &next)) return qfalse;
		} else next = state->automaticCheckpoint.commands[replay->commandCursor];
		if ((int64_t)time - replay->startServerTime < (int64_t)next.serverTime - replay->selection.snapshot.serverTime) return qfalse;
		replay->commandCursor++;
	}
	*cmd = next.cmd;
	cmd->serverTime += replay->startServerTime - replay->selection.snapshot.serverTime;
	replay->frameCmd = *cmd;
	return qtrue;
}

qboolean G_FlyingRespawn_FrameCmd(flyingRespawnState_t *state, int time, usercmd_t *cmd)
{
	flyingRespawnReplay_t *replay = &state->replay;
	int i;
	if (!cmd || !replay->active) return qtrue;
	if (replay->staticReplay) {
		memset(cmd, 0, sizeof(*cmd));
		/* Frame consumers still use this timestamp for liveness/timeout checks. */
		cmd->serverTime = time;
		return qfalse;
	}
	*cmd = replay->frameCmd;
	/* Synchronous frame consumers run before movement. Inspect due source input
	 * without consuming any step or ever consulting the buffered live command. */
	for (i = replay->commandCursor; i < replay->selection.commandCount; ++i) {
		flyingRespawnCmd_t next;
		if (replay->testReplay) {
			if (!FR_GetSelectedCmd(state, &replay->selection, i, &next)) break;
		} else next = (replay->manualReplay ? state->manualSave.commands : state->automaticCheckpoint.commands)[i];
		if ((int64_t)time - replay->startServerTime < (int64_t)next.serverTime - replay->selection.snapshot.serverTime) break;
		*cmd = next.cmd;
		cmd->serverTime = FR_ShiftTime(cmd->serverTime, replay->startServerTime - replay->selection.snapshot.serverTime);
	}
	return qtrue;
}

static void FR_MarkStaticContact(flyingRespawnState_t *state, int time)
{
	int entity = state->automaticCheckpoint.checkpointEntityNum;
	if (state->replay.testReplay) return;
	if (entity >= 0 && entity < MAX_GENTITIES) {
		state->checkpointContactSeen[entity] = 1;
		state->checkpointTouchTimes[entity] = time;
	}
}

static void FR_ShiftForceDeadlines(playerState_t *ps, int offset)
{
	int i;
	for (i = 0; i < NUM_FORCE_POWERS; ++i) {
		ps->fd.forcePowerDebounce[i] = G_FlyingRespawn_ShiftDeadline(ps->fd.forcePowerDebounce[i], offset);
		ps->fd.forcePowerDuration[i] = G_FlyingRespawn_ShiftDeadline(ps->fd.forcePowerDuration[i], offset);
	}
	ps->fd.forcePowerRegenDebounceTime = G_FlyingRespawn_ShiftDeadline(ps->fd.forcePowerRegenDebounceTime, offset);
	ps->fd.forceJumpAddTime = G_FlyingRespawn_ShiftDeadline(ps->fd.forceJumpAddTime, offset);
	ps->fd.forceRageRecoveryTime = G_FlyingRespawn_ShiftDeadline(ps->fd.forceRageRecoveryTime, offset);
}

void G_FlyingRespawn_ReleaseStaticForce(const flyingRespawnState_t *state, int time, playerState_t *ps, int *jetPackDebReduce)
{
	int late = time - state->replay.releaseServerTime;
	if (!ps || state->replay.active || !state->replay.staticReplay || late <= 0) return;
	FR_ShiftForceDeadlines(ps, late);
	ps->groundTime = G_FlyingRespawn_ShiftDeadline(ps->groundTime, late);
	if (jetPackDebReduce && *jetPackDebReduce) *jetPackDebReduce = FR_ShiftTime(*jetPackDebReduce, late);
}

qboolean G_FlyingRespawn_HandleSecondPress(flyingRespawnState_t *state, int time, flyingRespawnSnapshot_t *contact)
{
	flyingRespawnReplay_t *replay = &state->replay;
	int i, remaining;
	if (!contact || !replay->active || replay->manualReplay || replay->staticReplay) return qfalse;
	*contact = replay->testReplay ? replay->syntheticContact : state->automaticCheckpoint.snapshot;
	FR_ShiftMovement(&contact->movement, time - contact->serverTime);
	/* Force cannot run while held. Keep each source timer's remaining interval
	 * relative to GO, rather than letting the static waiting period expire it. */
	remaining = replay->releaseServerTime > time ? replay->releaseServerTime - time : 0;
	for (i = 0; i < NUM_FORCE_POWERS; ++i) {
		if (contact->movement.forcePowerDebounce[i]) contact->movement.forcePowerDebounce[i] = FR_ShiftTime(contact->movement.forcePowerDebounce[i], remaining);
		if (contact->movement.forcePowerDuration[i]) contact->movement.forcePowerDuration[i] = FR_ShiftTime(contact->movement.forcePowerDuration[i], remaining);
	}
	if (contact->movement.forcePowerRegenDebounceTime) contact->movement.forcePowerRegenDebounceTime = FR_ShiftTime(contact->movement.forcePowerRegenDebounceTime, remaining);
	if (contact->movement.forceJumpAddTime) contact->movement.forceJumpAddTime = FR_ShiftTime(contact->movement.forceJumpAddTime, remaining);
	contact->movement.jetPackDebReduce = G_FlyingRespawn_ShiftDeadline(contact->movement.jetPackDebReduce, remaining);
	contact->movement.jetPackDebRecharge = G_FlyingRespawn_ShiftDeadline(contact->movement.jetPackDebRecharge, remaining);
	contact->movement.overheatRechargeDebReduce = G_FlyingRespawn_ShiftDeadline(contact->movement.overheatRechargeDebReduce, remaining);
	contact->movement.forceRageRecoveryTime = G_FlyingRespawn_ShiftDeadline(contact->movement.forceRageRecoveryTime, remaining);
	contact->movement.groundTime = G_FlyingRespawn_ShiftDeadline(contact->movement.groundTime, remaining);
	contact->serverTime = time;
	memset(contact->movement.velocity, 0, sizeof(contact->movement.velocity));
	if (!replay->testReplay) G_FlyingRespawn_ApplyExpectedCheckpointProgress(state, &contact->movement);
	replay->staticReplay = qtrue;
	replay->expectedMovement = contact->movement;
	replay->commandCursor = replay->selection.commandCount;
	FR_MarkStaticContact(state, time);
	return qtrue;
}

static qboolean FR_MovementMatches(flyingRespawnReplay_t *replay, const flyingRespawnMovement_t *actual)
{
	flyingRespawnMovement_t a = *actual, expected = replay->expectedMovement;
	int i;
	float originError = 0, velocityError = 0;
	if (replay->staticReplay) a.commandTime = expected.commandTime;
	qboolean matches = qtrue;
	for (i = 0; i < 3; ++i) {
		float origin = actual->origin[i] - expected.origin[i];
		float velocity = actual->velocity[i] - expected.velocity[i];
		float angle = remainderf(actual->viewangles[i] - expected.viewangles[i], 360.0f);
		originError += origin * origin;
		velocityError += velocity * velocity;
		if (!isfinite(angle) || fabsf(angle) > 0.1f) matches = qfalse;
		/* These use tolerances; the remaining discrete/Force/movement/progress fields are exact. */
		a.origin[i] = expected.origin[i] = 0;
		a.velocity[i] = expected.velocity[i] = 0;
		a.viewangles[i] = expected.viewangles[i] = 0;
	}
	originError = sqrtf(originError);
	velocityError = sqrtf(velocityError);
	if (originError > replay->maxOriginError) replay->maxOriginError = originError;
	if (velocityError > replay->maxVelocityError) replay->maxVelocityError = velocityError;
	if (!isfinite(originError) || !isfinite(velocityError) || originError > 0.125f || velocityError > 0.125f) matches = qfalse;
	/* ReadMovement zeroes padding. Compare named groups through canonical player state
	 * serialization so callers' struct padding cannot affect validation. */
	FR_ShiftMovement(&a, 0);
	FR_ShiftMovement(&expected, 0);
	return matches && memcmp(&a, &expected, sizeof(a)) == 0;
}

void G_FlyingRespawn_RecordHandoff(flyingRespawnState_t *state, int time, const flyingRespawnMovement_t *movement)
{
	/* Replay handoff and external setup both break continuity with older poses. */
	state->stateHead = state->stateCount = 0;
	state->executionHead = state->executionCount = 0;
	state->stateDroppedThrough = state->executionDroppedThrough = INT_MIN;
	state->historyContinuousSince = time;
	state->historyGeneration++;
	if (movement) G_FlyingRespawn_RecordState(state, time, movement);
	state->historyBoundaryBeforeMovement = qtrue;
}

qboolean G_FlyingRespawn_Advance(flyingRespawnState_t *state, int time, const flyingRespawnMovement_t *actual, usercmd_t *cmd)
{
	flyingRespawnReplay_t *replay = &state->replay;
	if (!actual || !cmd || !replay->active || replay->manualReplay || time < replay->releaseServerTime ||
		replay->commandCursor < replay->selection.commandCount) return qfalse;
	if (!FR_MovementMatches(replay, actual) || G_FlyingRespawn_GetSecondaryElapsed(state, time) != replay->expectedSecondaryMs ||
		(!replay->testReplay && !replay->checkpointTouched)) {
		replay->mismatchCount++;
		state->mismatch = state->runInvalid = qtrue;
	}
	if (replay->hasLiveCmd) *cmd = replay->liveCmd;
	else memset(cmd, 0, sizeof(*cmd));
	cmd->serverTime = time > actual->commandTime ? time : actual->commandTime + 1;
	G_FlyingRespawn_ResetSecondarySegment(state, time, replay->expectedSecondaryMs);
	if (replay->staticReplay) FR_MarkStaticContact(state, time);
	replay->active = qfalse;
	return qtrue;
}

/* Snapshot only movement/progress: official timer fields deliberately have no path here. */
void G_FlyingRespawn_ReadMovement(flyingRespawnMovement_t *m, const playerState_t *ps, int style, int checkpoints, int courseID)
{
	memset(m, 0, sizeof(*m));
	memcpy(m->origin, ps->origin, sizeof(m->origin));
	memcpy(m->velocity, ps->velocity, sizeof(m->velocity));
	memcpy(m->viewangles, ps->viewangles, sizeof(m->viewangles));
	memcpy(m->deltaAngles, ps->delta_angles, sizeof(m->deltaAngles));
	memcpy(m->moveDir, ps->moveDir, sizeof(m->moveDir));
	m->commandTime = ps->commandTime;
	m->pmFlags = ps->pm_flags;
	m->pmTime = ps->pm_time;
	m->pmType = ps->pm_type;
	m->groundEntityNum = ps->groundEntityNum;
	m->movementStyle = style;
	m->checkpoints = checkpoints;
	m->courseID = courseID;
	m->forcePower = ps->fd.forcePower;
	m->forceJumpLevel = ps->fd.forcePowerLevel[FP_LEVITATION];
	m->forcePowersActive = ps->fd.forcePowersActive;
	m->forcePowerSelected = ps->fd.forcePowerSelected;
	m->forceButtonNeedRelease = ps->fd.forceButtonNeedRelease;
	memcpy(m->forcePowerDebounce, ps->fd.forcePowerDebounce, sizeof(m->forcePowerDebounce));
	memcpy(m->forcePowerDuration, ps->fd.forcePowerDuration, sizeof(m->forcePowerDuration));
	m->forcePowerRegenDebounceTime = ps->fd.forcePowerRegenDebounceTime;
	m->forceJumpZStart = ps->fd.forceJumpZStart;
	m->forceJumpCharge = ps->fd.forceJumpCharge;
	m->forceJumpAddTime = ps->fd.forceJumpAddTime;
	m->forceRageRecoveryTime = ps->fd.forceRageRecoveryTime;
	m->groundTime = ps->groundTime;
	m->dashTime = ps->stats[STAT_DASHTIME];
	m->wallJumpTime = ps->stats[STAT_WJTIME];
	m->jumpTime = ps->stats[STAT_JUMPTIME];
	m->jetpackFuel = ps->jetpackFuel;
	m->jetpackFlags = ps->eFlags & (EF_JETPACK | EF_JETPACK_ACTIVE | EF_JETPACK_FLAMING);
	m->legsAnim = ps->legsAnim;
	m->legsTimer = ps->legsTimer;
	m->torsoAnim = ps->torsoAnim;
	m->torsoTimer = ps->torsoTimer;
	m->weapon = ps->weapon;
	m->weaponstate = ps->weaponstate;
	m->weaponTime = ps->weaponTime;
	m->saberMove = ps->saberMove;
	m->saberAnimLevel = ps->fd.saberAnimLevel;
	m->saberDrawAnimLevel = ps->fd.saberDrawAnimLevel;
	m->saberAnimLevelBase = ps->fd.saberAnimLevelBase;
	m->saberHolstered = ps->saberHolstered;
	m->bobCycle = ps->bobCycle;
	m->movementDir = ps->movementDir;
	m->lastOnGround = ps->lastOnGround;
	m->gravity = ps->gravity;
	m->speed = ps->speed;
	m->basespeed = ps->basespeed;
}

static int FR_ShiftTime(int time, int offset)
{
	int64_t result = (int64_t)time + offset;
	if (result > INT_MAX) return INT_MAX;
	return result < INT_MIN ? INT_MIN : (int)result;
}

void G_FlyingRespawn_WriteMovement(playerState_t *ps, const flyingRespawnMovement_t *m, int offset)
{
	memcpy(ps->origin, m->origin, sizeof(m->origin));
	memcpy(ps->velocity, m->velocity, sizeof(m->velocity));
	memcpy(ps->viewangles, m->viewangles, sizeof(m->viewangles));
	memcpy(ps->delta_angles, m->deltaAngles, sizeof(m->deltaAngles));
	memcpy(ps->moveDir, m->moveDir, sizeof(m->moveDir));
	ps->commandTime = FR_ShiftTime(m->commandTime, offset);
	ps->pm_flags = m->pmFlags;
	ps->pm_time = m->pmTime;
	ps->pm_type = m->pmType;
	ps->groundEntityNum = m->groundEntityNum;
	ps->fd.forcePower = m->forcePower;
	ps->fd.forcePowerLevel[FP_LEVITATION] = m->forceJumpLevel;
	ps->fd.forcePowersActive = m->forcePowersActive;
	ps->fd.forcePowerSelected = m->forcePowerSelected;
	ps->fd.forceButtonNeedRelease = m->forceButtonNeedRelease;
	memcpy(ps->fd.forcePowerDebounce, m->forcePowerDebounce, sizeof(m->forcePowerDebounce));
	memcpy(ps->fd.forcePowerDuration, m->forcePowerDuration, sizeof(m->forcePowerDuration));
	ps->fd.forcePowerRegenDebounceTime = m->forcePowerRegenDebounceTime;
	ps->fd.forceJumpAddTime = m->forceJumpAddTime;
	ps->fd.forceJumpZStart = m->forceJumpZStart;
	ps->fd.forceJumpCharge = m->forceJumpCharge;
	ps->fd.forceRageRecoveryTime = m->forceRageRecoveryTime;
	FR_ShiftForceDeadlines(ps, offset);
	ps->groundTime = G_FlyingRespawn_ShiftDeadline(m->groundTime, offset);
	ps->stats[STAT_DASHTIME] = m->dashTime;
	ps->stats[STAT_WJTIME] = m->wallJumpTime;
	ps->stats[STAT_JUMPTIME] = m->jumpTime;
	ps->jetpackFuel = m->jetpackFuel;
	ps->eFlags = (ps->eFlags & ~(EF_JETPACK | EF_JETPACK_ACTIVE | EF_JETPACK_FLAMING)) | m->jetpackFlags;
	ps->legsAnim = m->legsAnim;
	ps->legsTimer = m->legsTimer;
	ps->torsoAnim = m->torsoAnim;
	ps->torsoTimer = m->torsoTimer;
	ps->weapon = m->weapon;
	ps->weaponstate = m->weaponstate;
	ps->weaponTime = m->weaponTime;
	ps->saberMove = m->saberMove;
	ps->fd.saberAnimLevel = m->saberAnimLevel;
	ps->fd.saberDrawAnimLevel = m->saberDrawAnimLevel;
	ps->fd.saberAnimLevelBase = m->saberAnimLevelBase;
	ps->saberHolstered = m->saberHolstered;
	ps->bobCycle = m->bobCycle;
	ps->movementDir = m->movementDir;
	ps->lastOnGround = m->lastOnGround ? FR_ShiftTime(m->lastOnGround, offset) : 0;
	ps->gravity = m->gravity;
	ps->speed = m->speed;
	ps->basespeed = m->basespeed;
}

static qboolean FR_FreezeHistory(const flyingRespawnState_t *state, int serverTime, int rewindMs, flyingRespawnCapture_t *capture)
{
	flyingRespawnSelection_t selection;
	flyingRespawnCmd_t cmd;
	int i;
	if (rewindMs < 0 || (int64_t)serverTime - rewindMs < INT_MIN ||
		!FR_SelectHistory(state, serverTime - rewindMs, serverTime, &selection)) return qfalse;
	capture->start = selection.snapshot;
	capture->startCmd = FR_StartCmd(state, capture->start.serverTime);
	for (i = 0; i < selection.commandCount; ++i) {
		if (!FR_GetSelectedCmd(state, &selection, i, &cmd) || !FR_AppendCapture(capture, &cmd)) {
			FR_ClearCapture(capture);
			return qfalse;
		}
	}
	return qtrue;
}

qboolean G_FlyingRespawn_CaptureManual(flyingRespawnState_t *state, int time, const flyingRespawnMovement_t *movement)
{
	flyingRespawnCapture_t capture;
	if (!movement || state->replay.active) return qfalse;
	memset(&capture, 0, sizeof(capture));
	if (!state->rewindMs || !FR_FreezeHistory(state, time, state->rewindMs, &capture)) {
		/* A bookmark made before a full approach exists remains an ordinary saved pose. */
		capture.start.serverTime = time;
		capture.start.movement = *movement;
		capture.startCmd = FR_StartCmd(state, time);
	}
	capture.snapshot.serverTime = time;
	capture.snapshot.movement = *movement;
	capture.handoff = capture.snapshot;
	capture.valid = qtrue;
	FR_ClearCapture(&state->manualSave);
	state->manualSave = capture;
	return qtrue;
}

void G_FlyingRespawn_ClearAutomaticCheckpoint(flyingRespawnState_t *state)
{
	FR_ClearCapture(&state->automaticCheckpoint);
}

qboolean G_FlyingRespawn_BeginManualReplay(flyingRespawnState_t *state, int time, qboolean practice)
{
	flyingRespawnCapture_t *capture = &state->manualSave;
	int64_t duration = (int64_t)capture->snapshot.serverTime - capture->start.serverTime;
	if (!practice || state->replay.active || !capture->valid || duration < 0 || (int64_t)time + duration > INT_MAX) return qfalse;
	memset(&state->replay, 0, sizeof(state->replay));
	state->replay.active = state->replay.manualReplay = qtrue;
	state->replay.selection.snapshot = capture->start;
	state->replay.selection.endServerTime = capture->snapshot.serverTime;
	state->replay.selection.commandCount = capture->commandCount;
	state->replay.startServerTime = time;
	state->replay.frameCmd = capture->startCmd;
	state->replay.frameCmd.serverTime = FR_ShiftTime(state->replay.frameCmd.serverTime, time - capture->start.serverTime);
	state->replay.releaseServerTime = time + (int)duration;
	return qtrue;
}

static qboolean FR_NextManualCmd(flyingRespawnState_t *state, int time, flyingRespawnCmd_t *cmd)
{
	flyingRespawnReplay_t *replay = &state->replay;
	const flyingRespawnCapture_t *capture = &state->manualSave;
	const flyingRespawnCmd_t *next;
	if (!cmd || !replay->active || !replay->manualReplay || replay->commandCursor >= capture->commandCount) return qfalse;
	next = &capture->commands[replay->commandCursor];
	if ((int64_t)time - replay->startServerTime < (int64_t)next->serverTime - capture->start.serverTime) return qfalse;
	*cmd = *next;
	replay->commandCursor++;
	return qtrue;
}

qboolean G_FlyingRespawn_FinishManualReplay(flyingRespawnState_t *state, int time, qboolean practice)
{
	flyingRespawnReplay_t *replay = &state->replay;
	if (!practice || !replay->active || !replay->manualReplay || time < replay->releaseServerTime ||
		replay->commandCursor < state->manualSave.commandCount) return qfalse;
	replay->active = qfalse;
	return qtrue;
}

static qboolean FR_CaptureAutomaticStart(flyingRespawnState_t *state, int time, const flyingRespawnMovement_t *movement, int rewindMs, int releaseMs)
{
	flyingRespawnCapture_t capture;
	if (state->replay.active || !movement || rewindMs < 0 || releaseMs < 0 || (int64_t)time + releaseMs > INT_MAX) return qfalse;
	FR_ClearCapture(&state->automaticCheckpoint);
	memset(&capture, 0, sizeof(capture));
	if (rewindMs) {
		if (!FR_FreezeHistory(state, time, rewindMs, &capture)) return qfalse;
	} else {
		capture.start.serverTime = time;
		capture.start.movement = *movement;
		capture.startCmd = FR_StartCmd(state, time);
	}
	capture.snapshot.serverTime = time;
	capture.snapshot.movement = *movement;
	capture.secondaryElapsedMs = G_FlyingRespawn_GetSecondaryElapsed(state, time);
	capture.releaseServerTime = time + releaseMs;
	capture.pending = releaseMs > 0 ? qtrue : qfalse;
	capture.valid = !capture.pending;
	capture.handoff = capture.snapshot;
	state->automaticCheckpoint = capture;
	return qtrue;
}

qboolean G_FlyingRespawn_CaptureAutomaticHandoff(flyingRespawnState_t *state, int time, const flyingRespawnMovement_t *movement)
{
	flyingRespawnCapture_t *capture = &state->automaticCheckpoint;
	if (!movement || !capture->pending || state->replay.active || time < capture->releaseServerTime) return qfalse;
	capture->handoff.serverTime = time;
	capture->handoff.movement = *movement;
	capture->pending = qfalse;
	capture->valid = qtrue;
	return qtrue;
}

qboolean G_FlyingRespawn_IsRunInvalid(const flyingRespawnState_t *state) { return state->runInvalid; }
int G_FlyingRespawn_GetRespawnCount(const flyingRespawnState_t *state) { return state->respawnCount; }
