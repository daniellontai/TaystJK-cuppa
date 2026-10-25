#include "g_flyingrespawn.h"
#include "sqlite3.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int FR_Index(int head, int offset, int capacity)
{
	return (int)(((size_t)head + offset) % capacity);
}

void G_FlyingRespawn_InitClient(flyingRespawnState_t *state)
{
	memset(state, 0, sizeof(*state));
	state->commandDroppedThrough = state->stateDroppedThrough = INT_MIN;
}

void G_FlyingRespawn_FreeHistory(flyingRespawnState_t *state)
{
	free(state->rawCommands);
	free(state->movementStates);
	state->rawCommands = NULL;
	state->movementStates = NULL;
	state->historyCapacity = state->commandHead = state->commandCount = 0;
	state->stateHead = state->stateCount = 0;
	state->commandDroppedThrough = state->stateDroppedThrough = INT_MIN;
	state->historyGeneration++;
	memset(&state->replay, 0, sizeof(state->replay));
}

qboolean G_FlyingRespawn_Configure(flyingRespawnState_t *state, int rewindMs, int releaseOffsetMs, int testMode)
{
	int retention, capacity;
	flyingRespawnRawCmd_t *commands;
	flyingRespawnSnapshot_t *snapshots;
	if (rewindMs < 0 || releaseOffsetMs < 0 || rewindMs > INT_MAX - FLYING_RESPAWN_SAMPLE_MARGIN_MS - 2)
		return qfalse;
	retention = testMode == 1 && rewindMs < FLYING_RESPAWN_MAX_TEST_MS ? FLYING_RESPAWN_MAX_TEST_MS : rewindMs;
	capacity = retention + FLYING_RESPAWN_SAMPLE_MARGIN_MS + 2; /* At most one distinct effective movement command per millisecond. */
	if (state->historyCapacity == capacity && state->rewindMs == rewindMs && state->testMode == (testMode == 1)) {
		state->releaseOffsetMs = releaseOffsetMs;
		return qtrue;
	}
	if ((size_t)capacity > SIZE_MAX / sizeof(*commands) || (size_t)capacity > SIZE_MAX / sizeof(*snapshots))
		return qfalse;
	commands = (flyingRespawnRawCmd_t *)malloc((size_t)capacity * sizeof(*commands));
	snapshots = (flyingRespawnSnapshot_t *)malloc((size_t)capacity * sizeof(*snapshots));
	if (!commands || !snapshots) {
		free(commands);
		free(snapshots);
		return qfalse;
	}
	G_FlyingRespawn_FreeHistory(state);
	state->rawCommands = commands;
	state->movementStates = snapshots;
	state->historyCapacity = capacity;
	state->rewindMs = rewindMs;
	state->releaseOffsetMs = releaseOffsetMs;
	state->retentionMs = retention;
	state->testMode = testMode == 1 ? qtrue : qfalse;
	return qtrue;
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
	memset(&state->automaticCheckpoint, 0, sizeof(state->automaticCheckpoint));
	memset(&state->replay, 0, sizeof(state->replay));
	state->respawnCount = 0;
	state->runInvalid = state->mismatch = qfalse;
	state->commandHead = state->commandCount = state->stateHead = state->stateCount = 0;
	state->commandDroppedThrough = state->stateDroppedThrough = INT_MIN;
	state->historyGeneration++;
	G_FlyingRespawn_ResetSecondarySegment(state, serverTime, 0);
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

qboolean G_FlyingRespawn_SelectHistory(const flyingRespawnState_t *state, int targetServerTime, int endServerTime, flyingRespawnSelection_t *selection)
{
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
	if (result.snapshot.serverTime < state->commandDroppedThrough) return qfalse;
	result.endServerTime = endServerTime;
	result.historyGeneration = state->historyGeneration;
	for (i = 0; i < state->commandCount; i++) {
		int time = state->rawCommands[FR_Index(state->commandHead, i, state->historyCapacity)].serverTime;
		if (time <= result.snapshot.serverTime) continue;
		if (time > endServerTime) break;
		if (!result.commandCount) result.firstCommand = i;
		result.commandCount++;
	}
	*selection = result;
	return qtrue;
}

qboolean G_FlyingRespawn_GetSelectedCmd(const flyingRespawnState_t *state, const flyingRespawnSelection_t *selection, int index, flyingRespawnRawCmd_t *cmd)
{
	if (!selection || !cmd || selection->historyGeneration != state->historyGeneration || index < 0 || index >= selection->commandCount) return qfalse;
	*cmd = state->rawCommands[FR_Index(state->commandHead, selection->firstCommand + index, state->historyCapacity)];
	return qtrue;
}

qboolean G_FlyingRespawn_BeginTestReplay(flyingRespawnState_t *state, int serverTime, int durationMs)
{
	flyingRespawnSelection_t selection;
	if (!state->testMode || state->replay.active || durationMs < 0 || durationMs > FLYING_RESPAWN_MAX_TEST_MS || (int64_t)serverTime - durationMs < INT_MIN) return qfalse;
	if (!G_FlyingRespawn_SelectHistory(state, serverTime - durationMs, serverTime, &selection)) return qfalse;
	memset(&state->replay, 0, sizeof(state->replay));
	state->replay.active = state->replay.testReplay = qtrue;
	state->replay.selection = selection;
	state->replay.startServerTime = serverTime;
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
