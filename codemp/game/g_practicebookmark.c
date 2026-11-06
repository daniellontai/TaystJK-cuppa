#include "g_local.h"
#include <limits.h>

qboolean G_SavePosition(gentity_t *ent, qboolean announce)
{
	flyingRespawnMovement_t movement;
	int saveWall;
	practiceTimerSnapshot_t timer = {0};
	if (!ent->client) return qfalse;
	if (ent->client->noclip) {
		if (announce) trap->SendServerCommand(ent - g_entities, "print \"Can't save position during noclip.\n\"");
		return qfalse;
	}
	if (ent->health <= 0) {
		if (announce) trap->SendServerCommand(ent - g_entities, "print \"You must be alive to save position.\n\"");
		return qfalse;
	}
	/* No normal movement has selected its mode after noclip yet. Save this
	 * exact pose without borrowing the old, discontinuous approach. */
	if (ent->client->pers.flyingRespawn.historyNoclip && !ent->client->pers.flyingRespawn.replay.active)
		G_FlyingRespawn_RecordHandoff(&ent->client->pers.flyingRespawn, level.time, NULL);
	G_FlyingRespawn_CaptureMovement(ent, &movement);
	if (!G_FlyingRespawn_CaptureManual(&ent->client->pers.flyingRespawn, level.time, &movement)) return qfalse;
	saveWall = trap->Milliseconds();
	timer.commandWallOffset = (int64_t)movement.commandTime - saveWall;
	timer.running = ent->client->pers.stats.startTime != 0;
	if (timer.running) {
		timer.coopStarted = ent->client->pers.stats.coopStarted;
		timer.elapsedMs = saveWall - ent->client->pers.stats.startTime;
		timer.levelElapsedMs = level.time - ent->client->pers.stats.startLevelTime;
		timer.secondaryElapsedMs = G_FlyingRespawn_GetSecondaryElapsed(&ent->client->pers.flyingRespawn, level.time);
		timer.respawnCount = ent->client->pers.flyingRespawn.respawnCount;
		timer.startLag = ent->client->pers.startLag;
		timer.displacement = ent->client->pers.stats.displacement;
		timer.displacementSamples = ent->client->pers.stats.displacementSamples;
		timer.topSpeed = ent->client->pers.stats.topSpeed;
	}

	ent->client->pers.savePosTimer = timer;
	if (announce) trap->SendServerCommand(ent - g_entities, "print \"Position saved.\n\"");
	return qtrue;
}

/* Rewind saved practice clocks at entry, then restore the saved state at GO. */
static int RestorePracticeTimer(gentity_t *ent, int rewindMs)
{
	int wall = trap->Milliseconds();
	practiceTimerSnapshot_t *timer;
	if (!ent->client || !ent->client->pers.practice ||
		!ent->client->pers.flyingRespawn.replay.manualReplay) return wall;
	timer = &ent->client->pers.savePosTimer;
	ent->client->pers.flyingRespawn.respawnCount = timer->respawnCount;
	if (timer->running) {
		ent->client->pers.stats.coopStarted         = timer->coopStarted;
		ent->client->pers.stats.startTime           = wall - timer->elapsedMs + rewindMs;
		ent->client->pers.stats.startLevelTime      = level.time - timer->levelElapsedMs + rewindMs;
		ent->client->pers.startLag                  = timer->startLag;
		ent->client->pers.stats.displacement        = timer->displacement;
		ent->client->pers.stats.displacementSamples = timer->displacementSamples;
		ent->client->pers.stats.topSpeed            = timer->topSpeed;
		ent->client->pers.stats.checkpoints         = ent->client->pers.flyingRespawn.manualSave.snapshot.movement.checkpoints;
		ent->client->pers.stats.lastCheckpointTime  = 0;
		ent->client->ps.duelTime                    = level.time - timer->levelElapsedMs + rewindMs;
		G_FlyingRespawn_ResetSecondarySegment(&ent->client->pers.flyingRespawn, level.time, timer->secondaryElapsedMs - rewindMs);
	} else {
		// timer was not running at savepos — clear any run that started after the save
		// (can't use ResetSpecificPlayerTimers: it clears velocity and overrides force for some styles)
		ent->client->pers.stats.startTime      = 0;
		ent->client->pers.stats.coopStarted    = qfalse;
		ent->client->pers.stats.startLevelTime = 0;
		ent->client->ps.duelTime               = 0;
		ent->client->pers.flyingRespawn.secondaryRunning = qfalse;
		ent->client->pers.flyingRespawn.secondaryBaseMs = 0;
	}
	return wall;
}

void G_PracticeBookmark_RestoreAtEntry(gentity_t *ent, int rewindMs)
{
	RestorePracticeTimer(ent, rewindMs);
}

void G_PracticeBookmark_RestoreAtRelease(gentity_t *ent)
{
	int wall = RestorePracticeTimer(ent, 0);
	int64_t commandTime = (int64_t)wall + ent->client->pers.savePosTimer.commandWallOffset;
	ent->client->ps.commandTime = commandTime > INT_MAX ? INT_MAX : commandTime < INT_MIN ? INT_MIN : (int)commandTime;
}
