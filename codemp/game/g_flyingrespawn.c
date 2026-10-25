#include "g_flyingrespawn.h"
#include "sqlite3.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int FR_Index(int head, int offset, int capacity)
{
	return (int)(((size_t)head + offset) % capacity);
}

static void FR_ClearCapture(flyingRespawnCapture_t *capture)
{
	free(capture->commands);
	memset(capture, 0, sizeof(*capture));
}

static qboolean FR_AppendCapture(flyingRespawnCapture_t *capture, const flyingRespawnRawCmd_t *cmd)
{
	if (capture->commandCount == capture->commandCapacity) {
		int capacity;
		flyingRespawnRawCmd_t *commands;
		if (capture->commandCapacity > INT_MAX / 2) return qfalse;
		capacity = capture->commandCapacity ? capture->commandCapacity * 2 : 32;
		if ((size_t)capacity > SIZE_MAX / sizeof(*commands)) return qfalse;
		commands = (flyingRespawnRawCmd_t *)realloc(capture->commands, (size_t)capacity * sizeof(*commands));
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
	state->commandDroppedThrough = state->stateDroppedThrough = INT_MIN;
	state->executionDroppedThrough = INT_MIN;
}

void G_FlyingRespawn_FreeHistory(flyingRespawnState_t *state)
{
	free(state->rawCommands);
	free(state->executionCommands);
	free(state->movementStates);
	state->rawCommands = NULL;
	state->executionCommands = NULL;
	state->movementStates = NULL;
	state->historyCapacity = state->commandHead = state->commandCount = 0;
	state->stateHead = state->stateCount = 0;
	state->executionHead = state->executionCount = 0;
	state->commandDroppedThrough = state->stateDroppedThrough = INT_MIN;
	state->executionDroppedThrough = INT_MIN;
	state->historyGeneration++;
	memset(&state->replay, 0, sizeof(state->replay));
}

void G_FlyingRespawn_FreeClient(flyingRespawnState_t *state)
{
	G_FlyingRespawn_FreeHistory(state);
	FR_ClearCapture(&state->manualSave);
	FR_ClearCapture(&state->automaticCheckpoint);
}

static qboolean FR_Configure(flyingRespawnState_t *state, int rewindMs, int releaseOffsetMs, int testMode, int mapRewindMs)
{
	int retention, capacity;
	flyingRespawnRawCmd_t *commands, *executions;
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
	if ((size_t)capacity > SIZE_MAX / sizeof(*commands) || (size_t)capacity > SIZE_MAX / sizeof(*snapshots))
		return qfalse;
	commands = (flyingRespawnRawCmd_t *)malloc((size_t)capacity * sizeof(*commands));
	executions = (flyingRespawnRawCmd_t *)malloc((size_t)capacity * sizeof(*executions));
	snapshots = (flyingRespawnSnapshot_t *)malloc((size_t)capacity * sizeof(*snapshots));
	if (!commands || !executions || !snapshots) {
		free(commands);
		free(executions);
		free(snapshots);
		return qfalse;
	}
	G_FlyingRespawn_FreeHistory(state);
	state->rawCommands = commands;
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

void G_FlyingRespawn_ResetRun(flyingRespawnState_t *state, int serverTime)
{
	FR_ClearCapture(&state->automaticCheckpoint);
	memset(&state->replay, 0, sizeof(state->replay));
	state->respawnCount = 0;
	memset(state->checkpointContactSeen, 0, sizeof(state->checkpointContactSeen));
	state->runInvalid = state->mismatch = state->testReplayUsed = qfalse;
	state->commandHead = state->commandCount = state->stateHead = state->stateCount = 0;
	state->executionHead = state->executionCount = 0;
	state->commandDroppedThrough = state->stateDroppedThrough = INT_MIN;
	state->executionDroppedThrough = INT_MIN;
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
	G_FlyingRespawn_ResetRun(state, time);
	if (!configured) G_FlyingRespawn_FreeHistory(state);
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
	if (!G_FlyingRespawn_CaptureAutomaticStart(state, time, movement, rewindMs, releaseMs)) return qfalse;
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

void G_FlyingRespawn_RecordRawCmd(flyingRespawnState_t *state, int serverTime, const usercmd_t *cmd)
{
	int slot;
	int64_t cutoff = (int64_t)serverTime - state->retentionMs - FLYING_RESPAWN_SAMPLE_MARGIN_MS;
	if (!state->historyCapacity || !cmd || state->replay.active) return;
	if (state->commandCount && serverTime < state->rawCommands[FR_Index(state->commandHead, state->commandCount - 1, state->historyCapacity)].serverTime) return;
	while (state->commandCount > 1 && state->rawCommands[FR_Index(state->commandHead, 1, state->historyCapacity)].serverTime <= cutoff) {
		state->commandDroppedThrough = state->rawCommands[state->commandHead].serverTime;
		state->commandHead = FR_Index(state->commandHead, 1, state->historyCapacity);
		state->commandCount--;
	}
	if (state->commandCount == state->historyCapacity) {
		state->commandDroppedThrough = state->rawCommands[state->commandHead].serverTime;
		state->commandHead = FR_Index(state->commandHead, 1, state->historyCapacity);
		state->commandCount--;
	}
	slot = FR_Index(state->commandHead, state->commandCount++, state->historyCapacity);
	state->rawCommands[slot].serverTime = serverTime;
	state->rawCommands[slot].cmd = *cmd;
	state->historyGeneration++;
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

static qboolean FR_SelectHistory(const flyingRespawnState_t *state, int targetServerTime, int endServerTime, flyingRespawnSelection_t *selection, qboolean executionHistory)
{
	const flyingRespawnRawCmd_t *commands = executionHistory ? state->executionCommands : state->rawCommands;
	int head = executionHistory ? state->executionHead : state->commandHead;
	int count = executionHistory ? state->executionCount : state->commandCount;
	int dropped = executionHistory ? state->executionDroppedThrough : state->commandDroppedThrough;
	int i, snapshot = -1;
	flyingRespawnSelection_t result;
	if (!selection || targetServerTime > endServerTime || !state->historyCapacity) return qfalse;
	memset(&result, 0, sizeof(result));
	for (i = 0; i < state->stateCount; i++) {
		int slot = FR_Index(state->stateHead, i, state->historyCapacity);
		if (state->movementStates[slot].serverTime > targetServerTime) break;
		snapshot = slot;
	}
	if (snapshot < 0) return qfalse;
	result.snapshot = state->movementStates[snapshot];
	if (result.snapshot.serverTime < dropped) return qfalse;
	result.executionHistory = executionHistory;
	result.endServerTime = endServerTime;
	result.historyGeneration = state->historyGeneration;
	for (i = 0; i < count; i++) {
		int time = commands[FR_Index(head, i, state->historyCapacity)].serverTime;
		if (time <= result.snapshot.serverTime) continue;
		if (time > endServerTime) break;
		if (!result.commandCount) result.firstCommand = i;
		result.commandCount++;
	}
	*selection = result;
	return qtrue;
}

qboolean G_FlyingRespawn_SelectHistory(const flyingRespawnState_t *state, int targetServerTime, int endServerTime, flyingRespawnSelection_t *selection)
{
	return FR_SelectHistory(state, targetServerTime, endServerTime, selection, qfalse);
}

qboolean G_FlyingRespawn_SelectExecutionHistory(const flyingRespawnState_t *state, int targetServerTime, int endServerTime, flyingRespawnSelection_t *selection)
{
	return FR_SelectHistory(state, targetServerTime, endServerTime, selection, qtrue);
}

qboolean G_FlyingRespawn_GetSelectedCmd(const flyingRespawnState_t *state, const flyingRespawnSelection_t *selection, int index, flyingRespawnRawCmd_t *cmd)
{
	if (!selection || !cmd || selection->historyGeneration != state->historyGeneration || index < 0 || index >= selection->commandCount) return qfalse;
	if (selection->executionHistory)
		*cmd = state->executionCommands[FR_Index(state->executionHead, selection->firstCommand + index, state->historyCapacity)];
	else
		*cmd = state->rawCommands[FR_Index(state->commandHead, selection->firstCommand + index, state->historyCapacity)];
	return qtrue;
}

static void FR_ShiftMovement(flyingRespawnMovement_t *movement, int offset)
{
	playerState_t ps;
	memset(&ps, 0, sizeof(ps));
	G_FlyingRespawn_WriteMovement(&ps, movement, offset);
	G_FlyingRespawn_ReadMovement(movement, &ps, movement->movementStyle, movement->checkpoints, movement->courseID);
}

qboolean G_FlyingRespawn_BeginTestReplay(flyingRespawnState_t *state, int serverTime, int durationMs)
{
	flyingRespawnSelection_t selection;
	int i;
	const flyingRespawnSnapshot_t *end = NULL;
	if (!state->testMode || state->replay.active || durationMs < 0 || durationMs > FLYING_RESPAWN_MAX_TEST_MS || (int64_t)serverTime - durationMs < INT_MIN) return qfalse;
	if (!G_FlyingRespawn_SelectExecutionHistory(state, serverTime - durationMs, serverTime, &selection)) return qfalse;
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
	state->replay.releaseServerTime = serverTime + serverTime - selection.snapshot.serverTime;
	state->replay.expectedMovement = end->movement;
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
	flyingRespawnRawCmd_t next;
	if (!cmd || !replay->active || replay->staticReplay || replay->commandCursor >= replay->selection.commandCount) return qfalse;
	if (replay->manualReplay) {
		if (!G_FlyingRespawn_NextManualCmd(state, time, &next)) return qfalse;
	} else {
		if (replay->testReplay) {
			if (!G_FlyingRespawn_GetSelectedCmd(state, &replay->selection, replay->commandCursor, &next)) return qfalse;
		} else next = state->automaticCheckpoint.commands[replay->commandCursor];
		if ((int64_t)time - replay->startServerTime < (int64_t)next.serverTime - replay->selection.snapshot.serverTime) return qfalse;
		replay->commandCursor++;
	}
	*cmd = next.cmd;
	cmd->serverTime += replay->startServerTime - replay->selection.snapshot.serverTime;
	return qtrue;
}

static void FR_MarkStaticContact(flyingRespawnState_t *state, int time)
{
	int entity = state->automaticCheckpoint.checkpointEntityNum;
	if (entity >= 0 && entity < MAX_GENTITIES) {
		state->checkpointContactSeen[entity] = 1;
		state->checkpointTouchTimes[entity] = time;
	}
}

qboolean G_FlyingRespawn_HandleSecondPress(flyingRespawnState_t *state, int time, flyingRespawnSnapshot_t *contact)
{
	flyingRespawnReplay_t *replay = &state->replay;
	if (!contact || !replay->active || replay->manualReplay || replay->testReplay || replay->staticReplay) return qfalse;
	*contact = state->automaticCheckpoint.snapshot;
	FR_ShiftMovement(&contact->movement, time - contact->serverTime);
	contact->serverTime = time;
	memset(contact->movement.velocity, 0, sizeof(contact->movement.velocity));
	G_FlyingRespawn_ApplyExpectedCheckpointProgress(state, &contact->movement);
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
	m->forcePowersActive = ps->fd.forcePowersActive;
	m->forcePowerSelected = ps->fd.forcePowerSelected;
	m->forceButtonNeedRelease = ps->fd.forceButtonNeedRelease;
	memcpy(m->forcePowerDebounce, ps->fd.forcePowerDebounce, sizeof(m->forcePowerDebounce));
	memcpy(m->forcePowerDuration, ps->fd.forcePowerDuration, sizeof(m->forcePowerDuration));
	m->forcePowerRegenDebounceTime = ps->fd.forcePowerRegenDebounceTime;
	m->forceJumpZStart = ps->fd.forceJumpZStart;
	m->forceJumpCharge = ps->fd.forceJumpCharge;
	m->forceJumpAddTime = ps->fd.forceJumpAddTime;
	m->legsAnim = ps->legsAnim;
	m->legsTimer = ps->legsTimer;
	m->torsoAnim = ps->torsoAnim;
	m->torsoTimer = ps->torsoTimer;
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
	int i;
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
	ps->fd.forcePowersActive = m->forcePowersActive;
	ps->fd.forcePowerSelected = m->forcePowerSelected;
	ps->fd.forceButtonNeedRelease = m->forceButtonNeedRelease;
	for (i = 0; i < NUM_FORCE_POWERS; ++i) {
		ps->fd.forcePowerDebounce[i] = m->forcePowerDebounce[i] ? FR_ShiftTime(m->forcePowerDebounce[i], offset) : 0;
		ps->fd.forcePowerDuration[i] = m->forcePowerDuration[i] ? FR_ShiftTime(m->forcePowerDuration[i], offset) : 0;
	}
	ps->fd.forcePowerRegenDebounceTime = m->forcePowerRegenDebounceTime ? FR_ShiftTime(m->forcePowerRegenDebounceTime, offset) : 0;
	ps->fd.forceJumpAddTime = m->forceJumpAddTime ? FR_ShiftTime(m->forceJumpAddTime, offset) : 0;
	ps->fd.forceJumpZStart = m->forceJumpZStart;
	ps->fd.forceJumpCharge = m->forceJumpCharge;
	ps->legsAnim = m->legsAnim;
	ps->legsTimer = m->legsTimer;
	ps->torsoAnim = m->torsoAnim;
	ps->torsoTimer = m->torsoTimer;
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
	flyingRespawnRawCmd_t cmd;
	int i;
	if (rewindMs < 0 || (int64_t)serverTime - rewindMs < INT_MIN ||
		!G_FlyingRespawn_SelectExecutionHistory(state, serverTime - rewindMs, serverTime, &selection)) return qfalse;
	capture->start = selection.snapshot;
	for (i = 0; i < selection.commandCount; ++i) {
		if (!G_FlyingRespawn_GetSelectedCmd(state, &selection, i, &cmd) || !FR_AppendCapture(capture, &cmd)) {
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
	}
	capture.snapshot.serverTime = time;
	capture.snapshot.movement = *movement;
	capture.handoff = capture.snapshot;
	capture.valid = qtrue;
	FR_ClearCapture(&state->manualSave);
	state->manualSave = capture;
	return qtrue;
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
	state->replay.releaseServerTime = time + (int)duration;
	return qtrue;
}

qboolean G_FlyingRespawn_NextManualCmd(flyingRespawnState_t *state, int time, flyingRespawnRawCmd_t *cmd)
{
	flyingRespawnReplay_t *replay = &state->replay;
	const flyingRespawnCapture_t *capture = &state->manualSave;
	const flyingRespawnRawCmd_t *next;
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

qboolean G_FlyingRespawn_CaptureAutomaticStart(flyingRespawnState_t *state, int time, const flyingRespawnMovement_t *movement, int rewindMs, int releaseMs)
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

int G_FlyingRespawn_HasRespawnCountColumn(sqlite3 *db)
{
	sqlite3_stmt *stmt = NULL;
	int result, found = 0;
	result = sqlite3_prepare_v2(db, "PRAGMA table_info(LocalRun)", -1, &stmt, NULL);
	if (result != SQLITE_OK) return -1;
	while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
		const char *name = (const char *)sqlite3_column_text(stmt, 1);
		if (name && sqlite3_stricmp(name, "respawn_count") == 0) found = 1;
	}
	sqlite3_finalize(stmt);
	return result == SQLITE_DONE ? found : -1;
}

int G_FlyingRespawn_MigrateLocalRun(sqlite3 *db)
{
	int found = G_FlyingRespawn_HasRespawnCountColumn(db);
	if (found < 0) return sqlite3_errcode(db);
	if (found) return SQLITE_OK;
	return sqlite3_exec(db, "ALTER TABLE LocalRun ADD COLUMN respawn_count INTEGER NOT NULL DEFAULT 0", NULL, NULL, NULL);
}
