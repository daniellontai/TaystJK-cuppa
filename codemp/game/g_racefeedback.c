#include "g_local.h"

qboolean G_RaceFeedback_Sample(const gentity_t *source, raceFeedbackSample_t *sample)
{
	const clientPersistant_t *pers;
	int wall, endLag, diffLag;
	if (!source || !source->client || !sample) return qfalse;
	pers = &source->client->pers;
	if (!pers->stats.startTime) return qfalse;
	wall = trap->Milliseconds();
	endLag = wall - level.frameStartTime + level.time - pers->cmd.serverTime;
	diffLag = pers->startLag - endLag;
	sample->elapsedMs = wall - pers->stats.startTime;
	if (diffLag > 0) sample->elapsedMs += diffLag;
	if (sample->elapsedMs < 1) sample->elapsedMs = 1;
	sample->averageSpeed = pers->stats.displacementSamples
		? floorf((pers->stats.displacement * sv_fps.value) / pers->stats.displacementSamples + 0.5f)
		: pers->stats.topSpeed;
	sample->topSpeed = (int)(pers->stats.topSpeed + 0.5f);
	return qtrue;
}

void G_RaceFeedback_Send(const gentity_t *recipient,
	const raceFeedbackSample_t *sample, raceFeedbackKind_t kind,
	int requiredCheckpoint, unsigned int allowedChannels)
{
	const clientPersistant_t *pers;
	char label[64];
	float seconds;
	if (!recipient || !recipient->client || !sample) return;
	pers = &recipient->client->pers;
	seconds = sample->elapsedMs * 0.001f;
	if (kind == RACE_FEEDBACK_REQUIRED_CHECKPOINT)
		Com_sprintf(label, sizeof(label), "Required Checkpoint %i", requiredCheckpoint);
	else
		Q_strncpyz(label, kind == RACE_FEEDBACK_SAVEPOINT ? "Savepoint" : "Checkpoint", sizeof(label));
	if ((allowedChannels & RACE_FEEDBACK_CENTER) && pers->showCenterCP) {
		if (kind == RACE_FEEDBACK_REQUIRED_CHECKPOINT)
			trap->SendServerCommand(recipient - g_entities, va("cp \"^5%s:\n^3%.3fs^5, avg ^3%i^5u, max ^3%i^5u\n\n\n\n\n\n\n\n\n\"", label, seconds, sample->averageSpeed, sample->topSpeed));
		else
			trap->SendServerCommand(recipient - g_entities, va("cp \"^3%.3fs^5, avg ^3%i^5u, max ^3%i^5u\n\n\n\n\n\n\n\n\n\n\"", seconds, sample->averageSpeed, sample->topSpeed));
	}
	if ((allowedChannels & RACE_FEEDBACK_CONSOLE) && pers->showConsoleCP)
		trap->SendServerCommand(recipient - g_entities, va("print \"^5%s: ^3%.3f^5, avg ^3%i^5, max ^3%i^5 ups\n\"", label, seconds, sample->averageSpeed, sample->topSpeed));
	else if ((allowedChannels & RACE_FEEDBACK_CHAT) && pers->showChatCP)
		trap->SendServerCommand(recipient - g_entities, va("chat \"^5%s: ^3%.3f^5, avg ^3%i^5, max ^3%i^5 ups\"", label, seconds, sample->averageSpeed, sample->topSpeed));
}
